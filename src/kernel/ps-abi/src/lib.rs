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

unsafe fn write_err(err: *mut c_char, cap: u32, msg: &str) {
    if err.is_null() || cap == 0 {
        return;
    }
    let bytes = msg.as_bytes();
    let n = bytes.len().min(cap as usize - 1);
    std::ptr::copy_nonoverlapping(bytes.as_ptr(), err as *mut u8, n);
    *((err as *mut u8).add(n)) = 0;
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

/// 新建空数据集。缓存层用它把磁盘上的通道数组还原成数据集，不重新解析源文件。
#[no_mangle]
pub extern "C" fn ps_dataset_new() -> *mut Dataset {
    Box::into_raw(Box::new(Dataset { channels: Vec::new(), infos: Vec::new() }))
}

/// 往数据集追加一条通道。name/unit 为 UTF-8；time/value 由内核复制。
/// 返回 0 成功；1 数据集为空；2 名称非法；3 数组为空但点数非 0；4 内存不足。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_add_channel(
    ds: *mut Dataset,
    name: *const c_char,
    unit: *const c_char,
    time: *const f64,
    value: *const f64,
    count: u32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if ds.is_null() {
        write_err(err, err_cap, "数据集为空");
        return 1;
    }
    if name.is_null() {
        write_err(err, err_cap, "通道名为空");
        return 2;
    }
    let n = count as usize;
    if n > 0 && (time.is_null() || value.is_null()) {
        write_err(err, err_cap, "通道数组为空");
        return 3;
    }
    let name = std::ffi::CStr::from_ptr(name).to_string_lossy().into_owned();
    let unit = if unit.is_null() {
        String::new()
    } else {
        std::ffi::CStr::from_ptr(unit).to_string_lossy().into_owned()
    };
    let t = if n == 0 { Vec::new() } else { std::slice::from_raw_parts(time, n).to_vec() };
    let v = if n == 0 { Vec::new() } else { std::slice::from_raw_parts(value, n).to_vec() };
    match (CString::new(name), CString::new(unit)) {
        (Ok(name), Ok(unit)) => {
            (*ds).channels.push(Channel { name, unit, t, v });
            (*ds).rebuild_infos();
            0
        }
        _ => {
            write_err(err, err_cap, "通道名含 NUL");
            2
        }
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


