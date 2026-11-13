//! ps-abi：App 与内核之间唯一的门（D9 / D16）。
//! 所有跨语言调用都在这里收口，`src/app` 只 include `ps_kernel.h`。
//! Rust 端 panic 不允许穿过 FFI，全部 catch 成错误码。

use std::ffi::CString;
use std::os::raw::c_char;
use std::sync::OnceLock;

/// 与 `ps_kernel.h` 的 `PS_ABI_VERSION` 必须一致；CI 有 golden 校验。
pub const ABI_VERSION: u32 = 1;

const VERSION: &str = env!("CARGO_PKG_VERSION");

fn version_cstr() -> &'static CString {
    static V: OnceLock<CString> = OnceLock::new();
    V.get_or_init(|| CString::new(VERSION).expect("version 里不允许出现 NUL"))
}

#[no_mangle]
pub extern "C" fn ps_abi_version() -> u32 {
    ABI_VERSION
}

#[no_mangle]
pub extern "C" fn ps_kernel_version() -> *const c_char {
    version_cstr().as_ptr()
}

/// 0 = 通过。非 0 为具体失败码：1 版本、2 NDF、3 算子、4 对齐。
#[no_mangle]
pub extern "C" fn ps_abi_selftest() -> i32 {
    if ABI_VERSION != 1 {
        return 1;
    }
    let mut s = ps_ndf::Series::new("selftest");
    s.push(0.0, 1.0);
    s.push(1.0, 3.0);
    match s.min_max() {
        Some((lo, hi)) if (lo - 1.0).abs() < f64::EPSILON && (hi - 3.0).abs() < f64::EPSILON => {}
        _ => return 2,
    }
    let sum = ps_ops::add(&[1.0, 2.0], &[3.0, 4.0]);
    if sum != vec![4.0, 6.0] {
        return 3;
    }
    let t = [0.0f64, 2.0];
    let v = [0.0f64, 100.0];
    let got = ps_align::linear(&t, &v, &[1.0]);
    if (got[0] - 50.0).abs() > 1e-9 {
        return 4;
    }
    0
}

// ===== 数据面（M0）：CSV → 数据集，LOD 在 Rust 侧算，UI 只拿三元组 =====

use std::ffi::CStr;

use std::path::Path;

/// 与 `ps_kernel.h` 的 `PsChannelInfo` 一致。指针指向数据集内部，数据集释放后失效。
#[repr(C)]
pub struct PsChannelInfo {
    pub name: *const c_char,
    pub unit: *const c_char,
    pub point_count: u32,
    pub time: *const f64,
    pub value: *const f64,
}

struct Channel {
    name: CString,
    unit: CString,
    t: Vec<f64>,
    v: Vec<f64>,
}

/// 不透明数据集：C 侧只持有指针。
pub struct Dataset {
    channels: Vec<Channel>,
    infos: Vec<PsChannelInfo>,
}

impl Dataset {
    fn rebuild_infos(&mut self) {
        self.infos = self
            .channels
            .iter()
            .map(|c| PsChannelInfo {
                name: c.name.as_ptr(),
                unit: c.unit.as_ptr(),
                point_count: c.t.len().min(c.v.len()) as u32,
                time: c.t.as_ptr(),
                value: c.v.as_ptr(),
            })
            .collect();
    }
}

/// 从表头里抠单位：`acc_x [m/s^2]` / `battery_v(V)` → 括号内容。
fn unit_of(header: &str) -> CString {
    let unit = header
        .split_once('[')
        .and_then(|(_, r)| r.split_once(']'))
        .map(|(u, _)| u)
        .or_else(|| header.split_once('(').and_then(|(_, r)| r.split_once(')')).map(|(u, _)| u))
        .unwrap_or("")
        .trim();
    CString::new(unit).unwrap_or_default()
}

unsafe fn write_err(err: *mut c_char, cap: u32, msg: &str) {
    if err.is_null() || cap == 0 {
        return;
    }
    let bytes = msg.as_bytes();
    let n = bytes.len().min(cap as usize - 1);
    std::ptr::copy_nonoverlapping(bytes.as_ptr(), err as *mut u8, n);
    *((err as *mut u8).add(n)) = 0;
}

/// 把 (名称, 时间, 值) 列表装成数据集。CSV / ULog 共用。
fn dataset_from_series(series: Vec<(String, Vec<f64>, Vec<f64>)>) -> Dataset {
    let mut ds = Dataset { channels: Vec::new(), infos: Vec::new() };
    for (name, t, v) in series {
        ds.channels.push(Channel {
            name: CString::new(name.as_str()).unwrap_or_else(|_| CString::new("channel").unwrap()),
            unit: CString::default(),
            t,
            v,
        });
    }
    ds.rebuild_infos();
    ds
}

/// 带单位版本：ArduPilot 的 FMTU 能给出单位，ULog 没有。
fn dataset_from_series_units(series: Vec<(String, String, Vec<f64>, Vec<f64>)>) -> Dataset {
    let mut ds = Dataset { channels: Vec::new(), infos: Vec::new() };
    for (name, unit, t, v) in series {
        ds.channels.push(Channel {
            name: CString::new(name.as_str()).unwrap_or_else(|_| CString::new("channel").unwrap()),
            unit: CString::new(unit.as_str()).unwrap_or_default(),
            t,
            v,
        });
    }
    ds.rebuild_infos();
    ds
}

/// 读 CSV → 数据集。成功返回 0 并写 `out`；失败返回非 0，`err` 里给原因。
fn csv_table_to_dataset(table: &ps_parse::csv::Table) -> Dataset {
    let mut ds = Dataset { channels: Vec::new(), infos: Vec::new() };
    let t = table.columns.first().cloned().unwrap_or_default();
    for (i, header) in table.headers.iter().enumerate().skip(1) {
        let v = table.columns.get(i).cloned().unwrap_or_default();
        ds.channels.push(Channel {
            name: CString::new(header.as_str()).unwrap_or_else(|_| CString::new("channel").unwrap()),
            unit: unit_of(header),
            t: t.clone(),
            v,
        });
    }
    ds.rebuild_infos();
    ds
}

#[no_mangle]
pub unsafe extern "C" fn ps_csv_probe(
    path: *const c_char,
    delimiter_override: c_char,
    out_json: *mut c_char,
    cap: u32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if path.is_null() || out_json.is_null() || cap == 0 {
        return 1;
    }
    let path_str = match CStr::from_ptr(path).to_str() {
        Ok(s) => s,
        Err(_) => {
            write_err(err, err_cap, "路径不是合法 UTF-8");
            return 2;
        }
    };
    let text = match std::fs::read_to_string(path_str) {
        Ok(t) => t,
        Err(e) => {
            write_err(err, err_cap, format!("读取失败: {}", e).as_str());
            return 3;
        }
    };
    let forced = match delimiter_override as u8 {
        0 => None,
        c => Some(c as char),
    };
    let probe = match ps_parse::csv::probe_with(&text, forced) {
        Ok(p) => p,
        Err(e) => {
            write_err(err, err_cap, e.as_str());
            return 4;
        }
    };
    let json = match serde_json::to_string(&probe) {
        Ok(j) => j,
        Err(e) => {
            write_err(err, err_cap, e.to_string().as_str());
            return 5;
        }
    };
    let bytes = json.as_bytes();
    if bytes.len() + 1 > cap as usize {
        write_err(err, err_cap, "探测结果太大，缓冲区不够");
        return 6;
    }
    std::ptr::copy_nonoverlapping(bytes.as_ptr() as *const u8, out_json as *mut u8, bytes.len());
    *out_json.add(bytes.len()) = 0;
    bytes.len() as i32
}

#[no_mangle]
pub unsafe extern "C" fn ps_csv_load_opts(
    path: *const c_char,
    opts_json: *const c_char,
    out: *mut *mut Dataset,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    use ps_parse::csv::TimeMode;
    if path.is_null() || opts_json.is_null() || out.is_null() {
        return 1;
    }
    let (path_str, opts_str) = match (CStr::from_ptr(path).to_str(), CStr::from_ptr(opts_json).to_str()) {
        (Ok(a), Ok(b)) => (a, b),
        _ => {
            write_err(err, err_cap, "参数不是合法 UTF-8");
            return 2;
        }
    };
    let v: serde_json::Value = match serde_json::from_str(opts_str) {
        Ok(v) => v,
        Err(e) => {
            write_err(err, err_cap, format!("opts JSON 解析失败: {}", e).as_str());
            return 3;
        }
    };
    let delimiter = v
        .get("delimiter")
        .and_then(|d| d.as_str())
        .and_then(|d| d.chars().next())
        .unwrap_or(',');
    let has_header = v.get("has_header").and_then(|b| b.as_bool()).unwrap_or(true);
    let skip_rows = v.get("skip_rows").and_then(|n| n.as_u64()).unwrap_or(0) as usize;
    let time = match v.get("time") {
        Some(t) => {
            let mode = t.get("mode").and_then(|m| m.as_str()).unwrap_or("row");
            match mode {
                "column" => TimeMode::Column(t.get("index").and_then(|i| i.as_u64()).unwrap_or(0) as usize),
                "combined" => TimeMode::Combined(
                    t.get("date").and_then(|i| i.as_u64()).unwrap_or(0) as usize,
                    t.get("time").and_then(|i| i.as_u64()).unwrap_or(1) as usize,
                ),
                _ => TimeMode::RowNumber,
            }
        }
        None => TimeMode::RowNumber,
    };
    let text = match std::fs::read_to_string(path_str) {
        Ok(t) => t,
        Err(e) => {
            write_err(err, err_cap, format!("读取失败: {}", e).as_str());
            return 4;
        }
    };
    let table = match ps_parse::csv::parse_with(
        &text,
        &ps_parse::csv::Options { delimiter, has_header, skip_rows, time },
    ) {
        Ok(t) => t,
        Err(e) => {
            write_err(err, err_cap, e.as_str());
            return 5;
        }
    };
    *out = Box::into_raw(Box::new(csv_table_to_dataset(&table)));
    0
}

#[no_mangle]
pub unsafe extern "C" fn ps_csv_load(
    path: *const c_char,
    out: *mut *mut Dataset,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if path.is_null() || out.is_null() {
        return 1;
    }
    let path_str = match CStr::from_ptr(path).to_str() {
        Ok(s) => s,
        Err(_) => {
            write_err(err, err_cap, "路径不是合法 UTF-8");
            return 2;
        }
    };
    let table = match ps_parse::csv::parse_file(Path::new(path_str)) {
        Ok(t) => t,
        Err(e) => {
            write_err(err, err_cap, e.as_str());
            return 3;
        }
    };

    *out = Box::into_raw(Box::new(csv_table_to_dataset(&table)));
    0
}

/// ASC / BLF 共用：解析 → 帧 → 曲线 → dataset
unsafe fn can_load(
    path: *const c_char,
    out: *mut *mut Dataset,
    err: *mut c_char,
    err_cap: u32,
    f: fn(&Path) -> Result<ps_parse::can::CanLog, String>,
) -> i32 {
    if path.is_null() || out.is_null() {
        return 1;
    }
    let path_str = match CStr::from_ptr(path).to_str() {
        Ok(s) => s,
        Err(_) => {
            write_err(err, err_cap, "路径不是合法 UTF-8");
            return 2;
        }
    };
    let log = match f(Path::new(path_str)) {
        Ok(l) => l,
        Err(e) => {
            write_err(err, err_cap, e.as_str());
            return 3;
        }
    };
    let series = ps_parse::can::frames_to_series(&log.frames);
    if series.is_empty() {
        write_err(err, err_cap, "所有字节都恒定，没有可画的通道");
        return 4;
    }
    *out = Box::into_raw(Box::new(dataset_from_series(series)));
    0
}

#[no_mangle]
pub unsafe extern "C" fn ps_asc_load(
    path: *const c_char,
    out: *mut *mut Dataset,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    can_load(path, out, err, err_cap, ps_parse::asc::parse_file)
}

#[no_mangle]
pub unsafe extern "C" fn ps_blf_load(
    path: *const c_char,
    out: *mut *mut Dataset,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    can_load(path, out, err, err_cap, ps_parse::blf::parse_file)
}

/// 在数据集里追加一条派生通道；同名拒绝。
fn add_channel(ds: &mut Dataset, name: &str, unit: &str, t: Vec<f64>, v: Vec<f64>) -> Result<(), String> {
    if name.is_empty() {
        return Err("输出通道名为空".into());
    }
    if ds.channels.iter().any(|c| c.name.to_string_lossy() == name) {
        return Err(format!("通道已存在：{name}"));
    }
    ds.channels.push(Channel {
        name: CString::new(name).map_err(|_| "通道名含 NUL")?,
        unit: CString::new(unit).unwrap_or_default(),
        t,
        v,
    });
    ds.rebuild_infos();
    Ok(())
}

fn require_name<'a>(ptr: *const c_char, what: &str) -> Result<&'a str, String> {
    if ptr.is_null() {
        return Err(format!("{what}为空"));
    }
    unsafe { CStr::from_ptr(ptr).to_str().map_err(|_| format!("{what}不是合法 UTF-8")) }
}

/// 对一条通道应用 PJ4 过滤器，结果作为普通通道写回数据集。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_apply_filter(
    ds: *mut Dataset,
    input: *const c_char,
    filter_id: *const c_char,
    params_json: *const c_char,
    output: *const c_char,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if ds.is_null() {
        return 1;
    }
    let input = match require_name(input, "输入通道") {
        Ok(s) => s,
        Err(e) => { write_err(err, err_cap, &e); return 2; }
    };
    let filter_id = match require_name(filter_id, "过滤器") {
        Ok(s) => s,
        Err(e) => { write_err(err, err_cap, &e); return 2; }
    };
    let params = if params_json.is_null() {
        serde_json::Value::Object(Default::default())
    } else {
        let text = match CStr::from_ptr(params_json).to_str() {
            Ok(s) => s,
            Err(_) => { write_err(err, err_cap, "参数不是合法 UTF-8"); return 2; }
        };
        if text.trim().is_empty() { serde_json::Value::Object(Default::default()) }
        else {
            match serde_json::from_str(text) {
                Ok(v) => v,
                Err(e) => { write_err(err, err_cap, &format!("参数 JSON：{e}")); return 5; }
            }
        }
    };
    let d = &mut *ds;
    let Some(idx) = d.channels.iter().position(|c| c.name.to_string_lossy() == input) else {
        write_err(err, err_cap, &format!("找不到输入通道：{input}"));
        return 3;
    };
    let t = d.channels[idx].t.clone();
    let v = d.channels[idx].v.clone();
    let unit = d.channels[idx].unit.to_string_lossy().to_string();
    let out_name = match require_name(output, "输出通道") {
        Ok(s) if !s.is_empty() => s.to_string(),
        Ok(_) => format!("{input}.{filter_id}"),
        Err(_) => format!("{input}.{filter_id}"),
    };
    let (ot, ov) = match ps_ops::filters::apply(filter_id, &t, &v, &params) {
        Ok(x) => x,
        Err(e) => { write_err(err, err_cap, &e); return 5; }
    };
    match add_channel(d, &out_name, &unit, ot, ov) {
        Ok(()) => 0,
        Err(e) => { write_err(err, err_cap, &e); return 4; }
    }
}

/// 只读预览：对通道应用过滤器，结果写进调用方缓冲区，不改数据集。
/// 返回 0 成功；6 缓冲区不足（out_n 仍给出实际长度）；其余同 apply_filter。
#[no_mangle]
pub unsafe extern "C" fn ps_preview_filter(
    ds: *const Dataset,
    input: *const c_char,
    filter_id: *const c_char,
    params_json: *const c_char,
    out_t: *mut f64,
    out_v: *mut f64,
    cap: u32,
    out_n: *mut u32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if ds.is_null() || out_t.is_null() || out_v.is_null() || out_n.is_null() {
        return 1;
    }
    let input = match require_name(input, "输入通道") {
        Ok(s) => s,
        Err(e) => { write_err(err, err_cap, &e); return 2; }
    };
    let filter_id = match require_name(filter_id, "过滤器") {
        Ok(s) => s,
        Err(e) => { write_err(err, err_cap, &e); return 2; }
    };
    let params = if params_json.is_null() {
        serde_json::Value::Object(Default::default())
    } else {
        let text = match CStr::from_ptr(params_json).to_str() {
            Ok(s) => s,
            Err(_) => { write_err(err, err_cap, "参数不是合法 UTF-8"); return 2; }
        };
        if text.trim().is_empty() { serde_json::Value::Object(Default::default()) }
        else {
            match serde_json::from_str(text) {
                Ok(v) => v,
                Err(e) => { write_err(err, err_cap, &format!("参数 JSON：{e}")); return 5; }
            }
        }
    };
    let d = &*ds;
    let Some(ch) = d.channels.iter().find(|c| c.name.to_string_lossy() == input) else {
        write_err(err, err_cap, &format!("找不到输入通道：{input}"));
        return 3;
    };
    let (ot, ov) = match ps_ops::filters::apply(filter_id, &ch.t, &ch.v, &params) {
        Ok(x) => x,
        Err(e) => { write_err(err, err_cap, &e); return 5; }
    };
    let n = ot.len().min(cap as usize);
    *out_n = ot.len() as u32;
    for i in 0..n {
        *out_t.add(i) = ot[i];
        *out_v.add(i) = ov[i];
    }
    if n < ot.len() { return 6; }
    0
}

/// 四元数 w/x/y/z → `out_topic.roll,pitch,yaw` 三条普通通道。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_apply_quat_rpy(
    ds: *mut Dataset,
    w: *const c_char,
    x: *const c_char,
    y: *const c_char,
    z: *const c_char,
    out_topic: *const c_char,
    degrees: u8,
    unwrap: u8,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if ds.is_null() {
        return 1;
    }
    let names = [("w", w), ("x", x), ("y", y), ("z", z)];
    let mut channels = Vec::new();
    let d = &*ds;
    for (label, ptr) in names {
        let name = match require_name(ptr, label) {
            Ok(s) => s,
            Err(e) => { write_err(err, err_cap, &e); return 2; }
        };
        let Some(idx) = d.channels.iter().position(|c| c.name.to_string_lossy() == name) else {
            write_err(err, err_cap, &format!("找不到四元数通道：{name}"));
            return 3;
        };
        channels.push(idx);
    }
    let len = d.channels[channels[0]].t.len();
    if channels.iter().any(|i| d.channels[*i].t.len() != len) {
        write_err(err, err_cap, "四元数四个通道点数不一致");
        return 5;
    }
    let times: Vec<f64> = d.channels[channels[0]].t.clone();
    for i in &channels[1..] {
        if d.channels[*i].t.iter().zip(&times).any(|(a, b)| (a - b).abs() > 1e-6) {
            write_err(err, err_cap, "四元数四个通道时间戳不一致");
            return 5;
        }
    }
    let wv = d.channels[channels[0]].v.clone();
    let xv = d.channels[channels[1]].v.clone();
    let yv = d.channels[channels[2]].v.clone();
    let zv = d.channels[channels[3]].v.clone();
    let (r, p, yaw) = match ps_ops::quat::to_rpy(&wv, &xv, &yv, &zv, degrees != 0, unwrap != 0) {
        Ok(x) => x,
        Err(e) => { write_err(err, err_cap, &e); return 5; }
    };
    let topic = match require_name(out_topic, "输出主题") {
        Ok(s) if !s.is_empty() => s.to_string(),
        _ => "rpy".to_string(),
    };
    let unit = if degrees != 0 { "deg" } else { "rad" };
    let names = [format!("{topic}.roll"), format!("{topic}.pitch"), format!("{topic}.yaw")];
    for name in &names {
        if d.channels.iter().any(|c| c.name.to_string_lossy() == *name) {
            write_err(err, err_cap, &format!("通道已存在：{name}"));
            return 4;
        }
    }
    let d = &mut *ds;
    for (name, vals) in [
        (format!("{topic}.roll"), r),
        (format!("{topic}.pitch"), p),
        (format!("{topic}.yaw"), yaw),
    ] {
        if let Err(e) = add_channel(d, &name, unit, times.clone(), vals) {
            write_err(err, err_cap, &e);
            return 4;
        }
    }
    0
}

/// 删除派生通道；原始通道也可删，由 UI 自己约束。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_remove_channel(ds: *mut Dataset, name: *const c_char) -> i32 {
    if ds.is_null() {
        return 1;
    }
    let name = match require_name(name, "通道") {
        Ok(s) => s,
        Err(_) => return 2,
    };
    let d = &mut *ds;
    let Some(idx) = d.channels.iter().position(|c| c.name.to_string_lossy() == name) else {
        return 3;
    };
    d.channels.remove(idx);
    d.rebuild_infos();
    0
}

#[no_mangle]
pub unsafe extern "C" fn ps_dataset_channel_count(ds: *const Dataset) -> u32 {
    if ds.is_null() {
        return 0;
    }
    (*ds).infos.len() as u32
}

#[no_mangle]
pub unsafe extern "C" fn ps_dataset_channel(ds: *const Dataset, index: u32) -> *const PsChannelInfo {
    if ds.is_null() {
        return std::ptr::null();
    }
    (&(*ds).infos).get(index as usize).map_or(std::ptr::null(), |i| i as *const _)
}

#[no_mangle]
pub unsafe extern "C" fn ps_dataset_free(ds: *mut Dataset) {
    if !ds.is_null() {
        drop(Box::from_raw(ds));
    }
}

/// 视口抽稀：写 `(x, min, max)` 三元组，返回写入的三元组个数。
#[no_mangle]
pub unsafe extern "C" fn ps_lod_minmax(
    t: *const f64,
    v: *const f64,
    n: u32,
    t0: f64,
    t1: f64,
    px: u32,
    out: *mut f64,
    cap_triples: u32,
) -> u32 {
    if t.is_null() || v.is_null() || out.is_null() {
        return 0;
    }
    let tt = std::slice::from_raw_parts(t, n as usize);
    let vv = std::slice::from_raw_parts(v, n as usize);
    let lod = ps_ndf::lod_minmax(tt, vv, t0, t1, px as usize);
    let take = lod.len().min(cap_triples as usize);
    for (i, (x, lo, hi)) in lod.into_iter().take(take).enumerate() {
        *out.add(i * 3) = x;
        *out.add(i * 3 + 1) = lo;
        *out.add(i * 3 + 2) = hi;
    }
    take as u32
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn selftest_passes() {
        assert_eq!(ps_abi_selftest(), 0);
    }

    #[test]
    fn version_is_exposed() {
        assert_eq!(ps_abi_version(), ABI_VERSION);
        assert!(!ps_kernel_version().is_null());
    }
}


/// 读 ULog → 数据集。成功返回 0 并写 `out`；失败返回非 0，`err` 里给原因。
/// ULog 参数表：`P`/`Q` 消息解出的 `[{name,value}]` JSON 数组（M1 参数页用）。
/// 参数表与数据面分开暴露，避免把非等长序列塞进 `PsDataset`。
#[no_mangle]
pub unsafe extern "C" fn ps_ulog_params(
    path: *const c_char,
    out_json: *mut c_char,
    cap: u32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if path.is_null() || out_json.is_null() || cap == 0 {
        return 1;
    }
    let path_str = match CStr::from_ptr(path).to_str() {
        Ok(s) => s,
        Err(_) => {
            write_err(err, err_cap, "路径不是合法 UTF-8");
            return 2;
        }
    };
    let log = match ps_parse::ulog::parse_file(Path::new(path_str)) {
        Ok(l) => l,
        Err(e) => {
            write_err(err, err_cap, e.as_str());
            return 3;
        }
    };
    let arr: Vec<serde_json::Value> = log
        .params
        .iter()
        .map(|(k, v)| serde_json::json!({ "name": k, "value": v }))
        .collect();
    let text = serde_json::to_string(&arr).unwrap_or_else(|_| "[]".to_string());
    if text.len() >= cap as usize {
        write_err(err, err_cap, "参数表超过输出缓冲区");
        return 4;
    }
    let bytes = text.as_bytes();
    let n = bytes.len().min(cap as usize - 1);
    std::ptr::copy_nonoverlapping(bytes.as_ptr(), out_json as *mut u8, n);
    *((out_json as *mut u8).add(n)) = 0;
    0
}

#[no_mangle]
pub unsafe extern "C" fn ps_ulog_load(
    path: *const c_char,
    out: *mut *mut Dataset,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if path.is_null() || out.is_null() {
        return 1;
    }
    let path_str = match CStr::from_ptr(path).to_str() {
        Ok(s) => s,
        Err(_) => {
            write_err(err, err_cap, "路径不是合法 UTF-8");
            return 2;
        }
    };
    let log = match ps_parse::ulog::parse_file(Path::new(path_str)) {
        Ok(l) => l,
        Err(e) => {
            write_err(err, err_cap, e.as_str());
            return 3;
        }
    };
    let series = log
        .channels
        .into_iter()
        .map(|c| (c.name, c.t, c.v))
        .collect::<Vec<_>>();
    *out = Box::into_raw(Box::new(dataset_from_series(series)));
    0
}

#[no_mangle]
pub unsafe extern "C" fn ps_apbin_load(
    path: *const c_char,
    out: *mut *mut Dataset,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if path.is_null() || out.is_null() {
        return 1;
    }
    let path_str = match CStr::from_ptr(path).to_str() {
        Ok(s) => s,
        Err(_) => {
            write_err(err, err_cap, "路径不是合法 UTF-8");
            return 2;
        }
    };
    let log = match ps_parse::apbin::parse_file(Path::new(path_str)) {
        Ok(l) => l,
        Err(e) => {
            write_err(err, err_cap, e.as_str());
            return 3;
        }
    };
    let series = log
        .channels
        .into_iter()
        .map(|c| (c.name, c.unit, c.t, c.v))
        .collect::<Vec<_>>();
    *out = Box::into_raw(Box::new(dataset_from_series_units(series)));
    0
}

#[no_mangle]
pub unsafe extern "C" fn ps_apbin_params(
    path: *const c_char,
    out_json: *mut c_char,
    cap: u32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if path.is_null() || out_json.is_null() || cap == 0 {
        return 1;
    }
    let path_str = match CStr::from_ptr(path).to_str() {
        Ok(s) => s,
        Err(_) => {
            write_err(err, err_cap, "路径不是合法 UTF-8");
            return 2;
        }
    };
    let log = match ps_parse::apbin::parse_file(Path::new(path_str)) {
        Ok(l) => l,
        Err(e) => {
            write_err(err, err_cap, e.as_str());
            return 3;
        }
    };
    let arr: Vec<serde_json::Value> = log
        .params
        .iter()
        .map(|(k, v)| serde_json::json!({ "name": k, "value": v }))
        .collect();
    let text = serde_json::to_string(&arr).unwrap_or_else(|_| "[]".to_string());
    if text.len() >= cap as usize {
        write_err(err, err_cap, "参数表超过输出缓冲区");
        return 4;
    }
    let bytes = text.as_bytes();
    let n = bytes.len().min(cap as usize - 1);
    std::ptr::copy_nonoverlapping(bytes.as_ptr(), out_json as *mut u8, n);
    *((out_json as *mut u8).add(n)) = 0;
    0
}
