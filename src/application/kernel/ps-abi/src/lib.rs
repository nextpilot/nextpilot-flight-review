//! ps-abi：App 与内核之间唯一的门（D9 / D16）。
//! 所有跨语言调用都在这里收口，`src/app` 只 include `ps_kernel.h`。
//! Rust 端 panic 不允许穿过 FFI，全部 catch 成错误码。

use std::ffi::CString;
use std::os::raw::c_char;
use std::sync::{Arc, OnceLock};

/// 与 `ps_kernel.h` 的 `PS_ABI_VERSION` 必须一致；CI 有 golden 校验。
pub const ABI_VERSION: u32 = 2;

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
    if ABI_VERSION != 2 {
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
    let got = ps_ops::align::linear(&t, &v, &[1.0]);
    if (got[0] - 50.0).abs() > 1e-9 {
        return 4;
    }
    0
}

// ===== 数据面：这里只做 C ABI 封装，数据模型在 ps-ndf =====

use std::ffi::CStr;

/// 与 `ps_kernel.h` 的 `PsChannelInfo` 一致。指针指向数据集内部，数据集释放后失效。
#[repr(C)]
pub struct PsChannelInfo {
    pub name: *const c_char,
    pub unit: *const c_char,
    pub point_count: u32,
    pub time: *const f64,
    pub value: *const f64,
    pub table: *const c_char,
    pub column: *const c_char,
    pub dataset: *const std::ffi::c_void,
    pub generation: u64,
}

/// C 侧看到的不透明数据集。真正的通道数组由 `ps_ndf::Dataset` 持有；
/// 这里只缓存 C 视图和 CString 名，避免每次取通道都重新分配。
pub struct PsDataset {
    data: ps_ndf::Dataset,
    typed: ps_ndf::typed::Dataset,
    infos: Vec<PsChannelInfo>,
    names: Vec<CString>,
    units: Vec<CString>,
    // typed 表只保留每表一份共享 f64 时间轴；列值不再复制 f64，走 ps_dataset_value_at。
    typed_times: Vec<Vec<f64>>,
    typed_names: Vec<CString>,
    typed_units: Vec<CString>,
    typed_tables: Vec<CString>,
    typed_columns: Vec<CString>,
    infos_dirty: bool,
}

impl PsDataset {
    fn new() -> Self {
        Self {
            data: ps_ndf::Dataset::new(),
            typed: ps_ndf::typed::Dataset::new(),
            infos: Vec::new(),
            names: Vec::new(),
            units: Vec::new(),
            typed_times: Vec::new(),
            typed_names: Vec::new(),
            typed_units: Vec::new(),
            typed_tables: Vec::new(),
            typed_columns: Vec::new(),
            infos_dirty: true,
        }
    }

    fn rebuild_infos(&mut self) {
        self.names = self.data.channels.iter().map(|c| CString::new(c.name.as_str()).unwrap_or_default()).collect();
        self.units = self.data.channels.iter().map(|c| CString::new(c.unit.as_str()).unwrap_or_default()).collect();

        // typed 表只补一份共享 f64 时间轴，供旧 UI 读 point_count/时间；列值不再复制。
        self.typed_times.clear();
        self.typed_names.clear();
        self.typed_units.clear();
        self.typed_tables.clear();
        self.typed_columns.clear();
        for table in &self.typed.tables {
            let rows = table.row_count();
            let mut time = Vec::with_capacity(rows);
            for &t in table.time_us.iter() {
                time.push(t as f64 * 1e-6);
            }
            for col in &table.columns {
                self.typed_names.push(CString::new(format!("{}.{}", table.name, col.name)).unwrap_or_default());
                let unit = CString::new(col.unit.as_str()).unwrap_or_default();
                self.typed_units.push(unit);
                self.typed_tables.push(CString::new(table.name.as_str()).unwrap_or_default());
                self.typed_columns.push(CString::new(col.name.as_str()).unwrap_or_default());
            }
            self.typed_times.push(time);
        }

        self.infos = self.data.channels.iter().enumerate().map(|(i, c)| PsChannelInfo {
            name: self.names[i].as_ptr(),
            unit: self.units[i].as_ptr(),
            point_count: c.t.len().min(c.v.len()) as u32,
            time: c.t.as_ptr(),
            value: c.v.as_ptr(),
            table: std::ptr::null(),
            column: std::ptr::null(),
            dataset: std::ptr::null(),
            generation: 0,
        }).collect();
        let mut typed_infos = Vec::new();
        for (ti, table) in self.typed.tables.iter().enumerate() {
            for _ in 0..table.columns.len() {
                typed_infos.push(PsChannelInfo {
                    name: self.typed_names[typed_infos.len()].as_ptr(),
                    unit: self.typed_units[typed_infos.len()].as_ptr(),
                    point_count: table.row_count() as u32,
                    time: self.typed_times[ti].as_ptr(),
                    value: std::ptr::null(),
                    table: self.typed_tables[typed_infos.len()].as_ptr(),
                    column: self.typed_columns[typed_infos.len()].as_ptr(),
                    dataset: self as *const PsDataset as *const std::ffi::c_void,
                    generation: 0,
                });
            }
        }
        self.infos.extend(typed_infos);
        self.infos_dirty = false;
    }

    fn ensure_infos(&mut self) {
        if self.infos_dirty {
            self.rebuild_infos();
        }
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
fn add_channel(d: &mut PsDataset, name: &str, unit: &str, t: Vec<f64>, v: Vec<f64>) -> Result<(), String> {
    d.data.add_channel(ps_ndf::Series { name: name.to_string(), unit: unit.to_string(), t: Arc::new(t), v })?;
    d.rebuild_infos();
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
    ds: *mut PsDataset,
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
    let Some((idx, _)) = d.data.find(&input) else {
        write_err(err, err_cap, &format!("找不到输入通道：{input}"));
        return 3;
    };
    let t = d.data.channels[idx].t.as_ref().clone();
    let v = d.data.channels[idx].v.clone();
    let unit = d.data.channels[idx].unit.clone();
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
    ds: *const PsDataset,
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
    let Some((_, ch)) = d.data.find(&input) else {
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

/// 对任意内存序列应用 PJ4 过滤器，结果写入调用方缓冲区。
/// 返回 0 成功；6 缓冲区不足（out_n 仍给出实际长度）；其余同 apply_filter。
/// 供 plotting 曲线级 filter 与 datasource 预览共用同一套算子实现。
#[no_mangle]
pub unsafe extern "C" fn ps_filter_apply_series(
    filter_id: *const c_char,
    params_json: *const c_char,
    t: *const f64,
    v: *const f64,
    n: u32,
    out_t: *mut f64,
    out_v: *mut f64,
    cap: u32,
    out_n: *mut u32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if out_t.is_null() || out_v.is_null() || out_n.is_null() {
        return 1;
    }
    if n > 0 && (t.is_null() || v.is_null()) {
        return 1;
    }
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
    let tv: &[f64] = if n == 0 { &[] } else { std::slice::from_raw_parts(t, n as usize) };
    let vv: &[f64] = if n == 0 { &[] } else { std::slice::from_raw_parts(v, n as usize) };
    let (ot, ov) = match ps_ops::filters::apply(filter_id, tv, vv, &params) {
        Ok(x) => x,
        Err(e) => { write_err(err, err_cap, &e); return 5; }
    };
    let out_len = ot.len().min(cap as usize);
    *out_n = ot.len() as u32;
    for i in 0..out_len {
        *out_t.add(i) = ot[i];
        *out_v.add(i) = ov[i];
    }
    if out_len < ot.len() { return 6; }
    0
}

/// 四元数 w/x/y/z → `out_topic.roll,pitch,yaw` 三条普通通道。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_apply_quat_rpy(
    ds: *mut PsDataset,
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
        let Some((idx, _)) = d.data.find(&name) else {
            write_err(err, err_cap, &format!("找不到四元数通道：{name}"));
            return 3;
        };
        channels.push(idx);
    }
    let len = d.data.channels[channels[0]].t.len();
    if channels.iter().any(|i| d.data.channels[*i].t.len() != len) {
        write_err(err, err_cap, "四元数四个通道点数不一致");
        return 5;
    }
    let times: Vec<f64> = d.data.channels[channels[0]].t.as_ref().clone();
    for i in &channels[1..] {
        if d.data.channels[*i].t.iter().zip(&times).any(|(a, b)| (a - b).abs() > 1e-6) {
            write_err(err, err_cap, "四元数四个通道时间戳不一致");
            return 5;
        }
    }
    let wv = d.data.channels[channels[0]].v.clone();
    let xv = d.data.channels[channels[1]].v.clone();
    let yv = d.data.channels[channels[2]].v.clone();
    let zv = d.data.channels[channels[3]].v.clone();
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
        if d.data.find(name).is_some() {
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
pub unsafe extern "C" fn ps_dataset_remove_channel(ds: *mut PsDataset, name: *const c_char) -> i32 {
    if ds.is_null() {
        return 1;
    }
    let name = match require_name(name, "通道") {
        Ok(s) => s,
        Err(_) => return 2,
    };
    let d = &mut *ds;
    if !d.data.remove_channel(&name) {
        return 3;
    }
    d.rebuild_infos();
    0
}

#[no_mangle]
pub unsafe extern "C" fn ps_dataset_channel_count(ds: *const PsDataset) -> u32 {
    if ds.is_null() {
        return 0;
    }
    let ds = ds as *mut PsDataset;
    (*ds).ensure_infos();
    (*ds).infos.len() as u32
}

#[no_mangle]
pub unsafe extern "C" fn ps_dataset_channel(ds: *const PsDataset, index: u32) -> *const PsChannelInfo {
    if ds.is_null() {
        return std::ptr::null();
    }
    let ds = ds as *mut PsDataset;
    (*ds).ensure_infos();
    (&(*ds).infos).get(index as usize).map_or(std::ptr::null(), |i| i as *const _)
}

#[no_mangle]
pub unsafe extern "C" fn ps_dataset_free(ds: *mut PsDataset) {
    if !ds.is_null() {
        drop(Box::from_raw(ds));
    }
}

/// 新建空数据集。缓存层用它把磁盘上的通道数组还原成数据集，不重新解析源文件。
#[no_mangle]
pub extern "C" fn ps_dataset_new() -> *mut PsDataset {
    Box::into_raw(Box::new(PsDataset::new()))
}

// ===== P3：缓存与实时流机制 =====

fn c_path(path: *const c_char) -> Result<std::path::PathBuf, String> {
    if path.is_null() {
        return Err("路径为空".into());
    }
    let s = unsafe { CStr::from_ptr(path) }.to_str().map_err(|_| "路径不是 UTF-8".to_string())?;
    Ok(std::path::PathBuf::from(s))
}

fn c_name(ptr: *const c_char) -> String {
    if ptr.is_null() {
        return String::new();
    }
    unsafe { CStr::from_ptr(ptr) }.to_string_lossy().into_owned()
}

/// 把数据集按源文件指纹写入 `path`。0 成功；1 参数错误；2 IO/格式错误。
#[no_mangle]
pub unsafe extern "C" fn ps_cache_save_dataset(
    ds: *const PsDataset,
    path: *const c_char,
    source_size: i64,
    source_mtime: i64,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if ds.is_null() {
        write_err(err, err_cap, "数据集为空");
        return 1;
    }
    let p = match c_path(path) {
        Ok(p) => p,
        Err(e) => { write_err(err, err_cap, &e); return 1; }
    };
    if !(*ds).typed.tables.is_empty() {
        return match ps_cache::save_typed_dataset(&p, &(*ds).typed, source_size, source_mtime) {
            Ok(()) => 0,
            Err(e) => { write_err(err, err_cap, &e); 2 }
        };
    }
    match ps_cache::save_dataset(&p, &(*ds).data, source_size, source_mtime) {
        Ok(()) => 0,
        Err(e) => { write_err(err, err_cap, &e); 2 }
    }
}

/// 读回缓存。0 命中；1 不存在/指纹过期；2 文件损坏或参数错误。
#[no_mangle]
pub unsafe extern "C" fn ps_cache_load_dataset(
    path: *const c_char,
    source_size: i64,
    source_mtime: i64,
    out: *mut *mut PsDataset,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if out.is_null() {
        write_err(err, err_cap, "输出指针为空");
        return 2;
    }
    *out = std::ptr::null_mut();
    let p = match c_path(path) {
        Ok(p) => p,
        Err(e) => { write_err(err, err_cap, &e); return 2; }
    };
    match ps_cache::load_typed_dataset(&p, source_size, source_mtime) {
        Ok(Some(typed)) => {
            let mut ds = PsDataset::new();
            ds.typed = typed;
            ds.rebuild_infos();
            *out = Box::into_raw(Box::new(ds));
            return 0;
        }
        Ok(None) => {}
        Err(e) => { write_err(err, err_cap, &e); return 2; }
    }
    match ps_cache::load_dataset(&p, source_size, source_mtime) {
        Ok(None) => 1,
        Ok(Some(data)) => {
            let mut ds = PsDataset::new();
            ds.data = data;
            ds.rebuild_infos();
            *out = Box::into_raw(Box::new(ds));
            0
        }
        Err(e) => { write_err(err, err_cap, &e); 2 }
    }
}

/// 参数 JSON 缓存。0 成功；1 参数错误；2 IO 错误。
#[no_mangle]
pub unsafe extern "C" fn ps_cache_save_params(
    path: *const c_char,
    json: *const c_char,
    source_size: i64,
    source_mtime: i64,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if json.is_null() {
        write_err(err, err_cap, "参数 JSON 为空");
        return 1;
    }
    let p = match c_path(path) {
        Ok(p) => p,
        Err(e) => { write_err(err, err_cap, &e); return 1; }
    };
    let text = c_name(json);
    match ps_cache::save_params(&p, &text, source_size, source_mtime) {
        Ok(()) => 0,
        Err(e) => { write_err(err, err_cap, &e); 2 }
    }
}

/// 读参数缓存。0 命中；1 不存在/指纹过期；2 损坏或参数错误。
#[no_mangle]
pub unsafe extern "C" fn ps_cache_load_params(
    path: *const c_char,
    source_size: i64,
    source_mtime: i64,
    out: *mut c_char,
    cap: u32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if out.is_null() || cap == 0 {
        write_err(err, err_cap, "输出缓冲区为空");
        return 2;
    }
    let p = match c_path(path) {
        Ok(p) => p,
        Err(e) => { write_err(err, err_cap, &e); return 2; }
    };
    match ps_cache::load_params(&p, source_size, source_mtime) {
        Ok(None) => 1,
        Ok(Some(text)) => {
            if text.len() >= cap as usize {
                write_err(err, err_cap, "参数超过输出缓冲区");
                return 2;
            }
            let bytes = text.as_bytes();
            std::ptr::copy_nonoverlapping(bytes.as_ptr(), out as *mut u8, bytes.len());
            *out.add(bytes.len()) = 0;
            0
        }
        Err(e) => { write_err(err, err_cap, &e); 2 }
    }
}

/// 往数据集追加一条通道。name/unit 为 UTF-8；time/value 由内核复制。
/// 返回 0 成功；1 数据集为空；2 名称非法；3 数组为空但点数非 0；4 内存不足。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_add_channel(
    ds: *mut PsDataset,
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
            (*ds).data.channels.push(ps_ndf::Series {
                name: name.to_string_lossy().into_owned(),
                unit: unit.to_string_lossy().into_owned(),
                t: Arc::new(t),
                v,
            });
            (*ds).rebuild_infos();
            0
        }
        _ => {
            write_err(err, err_cap, "通道名含 NUL");
            2
        }
    }
}

/// 新建一张 topic 表；时间轴只复制一次，同表所有列共享。
/// 返回 0 成功；1 数据集为空；2 表名非法；3 时间数组为空；4 表已存在。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_add_table(
    ds: *mut PsDataset,
    name: *const c_char,
    time: *const f64,
    count: u32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if ds.is_null() {
        write_err(err, err_cap, "数据集为空");
        return 1;
    }
    let name = match require_name(name, "表名") {
        Ok(s) if !s.is_empty() => s.to_string(),
        Ok(_) => { write_err(err, err_cap, "表名为空"); return 2; }
        Err(e) => { write_err(err, err_cap, &e); return 2; }
    };
    let n = count as usize;
    if n > 0 && time.is_null() {
        write_err(err, err_cap, "时间数组为空");
        return 3;
    }
    let t = if n == 0 { Vec::new() } else { std::slice::from_raw_parts(time, n).to_vec() };
    match (*ds).data.add_table(name, t) {
        Ok(_) => 0,
        Err(e) => { write_err(err, err_cap, &e); 4 }
    }
}

/// 往 topic 表追加一列；列只存 values，时间轴引用表。
/// 返回 0 成功；1 数据集为空；2 名称非法；3 值数组为空；4 表不存在；5 点数不一致或重名。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_add_table_channel(
    ds: *mut PsDataset,
    table: *const c_char,
    name: *const c_char,
    unit: *const c_char,
    value: *const f64,
    count: u32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if ds.is_null() {
        write_err(err, err_cap, "数据集为空");
        return 1;
    }
    let table = match require_name(table, "表名") {
        Ok(s) => s.to_string(),
        Err(e) => { write_err(err, err_cap, &e); return 2; }
    };
    let name = match require_name(name, "通道名") {
        Ok(s) if !s.is_empty() => s.to_string(),
        Ok(_) => { write_err(err, err_cap, "通道名为空"); return 2; }
        Err(e) => { write_err(err, err_cap, &e); return 2; }
    };
    let unit = if unit.is_null() {
        String::new()
    } else {
        std::ffi::CStr::from_ptr(unit).to_string_lossy().into_owned()
    };
    let n = count as usize;
    if n > 0 && value.is_null() {
        write_err(err, err_cap, "值数组为空");
        return 3;
    }
    let v = if n == 0 { Vec::new() } else { std::slice::from_raw_parts(value, n).to_vec() };
    let Some(table_idx) = (*ds).data.tables.iter().position(|t| t.name == table) else {
        write_err(err, err_cap, &format!("表不存在：{table}"));
        return 4;
    };
    match (*ds).data.add_channel_to_table(table_idx, name, unit, v) {
        Ok(()) => {
            (*ds).rebuild_infos();
            0
        }
        Err(e) => { write_err(err, err_cap, &e); 5 }
    }
}

/// 表级 typed 构造：UTC 微秒时间轴，列类型由 `ps_dataset_add_table_column_typed` 决定。
/// 返回 0 成功；1 数据集为空；2 表名非法；3 时间数组为空；4 表已存在。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_add_table_typed(
    ds: *mut PsDataset,
    table: *const c_char,
    time_us: *const i64,
    count: u32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if ds.is_null() {
        write_err(err, err_cap, "数据集为空");
        return 1;
    }
    let name = match require_name(table, "表名") {
        Ok(s) if !s.is_empty() => s.to_string(),
        Ok(_) => { write_err(err, err_cap, "表名为空"); return 2; }
        Err(e) => { write_err(err, err_cap, &e); return 2; }
    };
    let n = count as usize;
    if n > 0 && time_us.is_null() {
        write_err(err, err_cap, "时间数组为空");
        return 3;
    }
    let t = if n == 0 { Vec::new() } else { std::slice::from_raw_parts(time_us, n).to_vec() };
    match (*ds).typed.add_table(name, t) {
        Ok(_) => { (*ds).infos_dirty = true; 0 }
        Err(e) => { write_err(err, err_cap, &e); 4 }
    }
}

fn map_value_type(raw: u32) -> Option<ps_ndf::typed::ValueType> {
    use ps_ndf::typed::ValueType::*;
    Some(match raw {
        0 => F64,
        1 => F32,
        2 => I64,
        3 => U64,
        4 => I32,
        5 => U32,
        6 => I16,
        7 => U16,
        8 => I8,
        9 => U8,
        10 => Bool,
        11 => Text,
        _ => return None,
    })
}

unsafe fn typed_column_data(
    raw_type: u32,
    values: *const std::ffi::c_void,
    n: usize,
) -> Result<ps_ndf::typed::ColumnData, String> {
    use ps_ndf::typed::ColumnData;
    if n > 0 && values.is_null() {
        return Err("值数组为空".into());
    }
    if n == 0 {
        return Ok(match raw_type {
            0 => ColumnData::F64(Vec::new()),
            1 => ColumnData::F32(Vec::new()),
            2 => ColumnData::I64(Vec::new()),
            3 => ColumnData::U64(Vec::new()),
            4 => ColumnData::I32(Vec::new()),
            5 => ColumnData::U32(Vec::new()),
            6 => ColumnData::I16(Vec::new()),
            7 => ColumnData::U16(Vec::new()),
            8 => ColumnData::I8(Vec::new()),
            9 => ColumnData::U8(Vec::new()),
            10 => ColumnData::Bool(Vec::new()),
            11 => ColumnData::Text(Vec::new()),
            _ => return Err("未知列类型".into()),
        });
    }
    let data = match raw_type {
        0 => ColumnData::F64(std::slice::from_raw_parts(values as *const f64, n).to_vec()),
        1 => ColumnData::F32(std::slice::from_raw_parts(values as *const f32, n).to_vec()),
        2 => ColumnData::I64(std::slice::from_raw_parts(values as *const i64, n).to_vec()),
        3 => ColumnData::U64(std::slice::from_raw_parts(values as *const u64, n).to_vec()),
        4 => ColumnData::I32(std::slice::from_raw_parts(values as *const i32, n).to_vec()),
        5 => ColumnData::U32(std::slice::from_raw_parts(values as *const u32, n).to_vec()),
        6 => ColumnData::I16(std::slice::from_raw_parts(values as *const i16, n).to_vec()),
        7 => ColumnData::U16(std::slice::from_raw_parts(values as *const u16, n).to_vec()),
        8 => ColumnData::I8(std::slice::from_raw_parts(values as *const i8, n).to_vec()),
        9 => ColumnData::U8(std::slice::from_raw_parts(values as *const u8, n).to_vec()),
        10 => ColumnData::Bool(
            std::slice::from_raw_parts(values as *const u8, n)
                .iter()
                .map(|x| *x != 0)
                .collect(),
        ),
        11 => {
            let ptr = values as *const *const c_char;
            let mut out = Vec::with_capacity(n);
            for i in 0..n {
                let p = *ptr.add(i);
                out.push(if p.is_null() {
                    String::new()
                } else {
                    CStr::from_ptr(p).to_string_lossy().into_owned()
                });
            }
            ColumnData::Text(out)
        }
        _ => return Err("未知列类型".into()),
    };
    Ok(data)
}

/// 往 typed 表追加一列。`values` 按 `value_type` 解释；`nulls` 为可选位图（1=null）。
/// 返回 0 成功；1 数据集为空；2 名称/类型非法；3 值数组为空；4 表不存在；5 点数/NULL 非法/重名。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_add_table_column_typed(
    ds: *mut PsDataset,
    table: *const c_char,
    name: *const c_char,
    unit: *const c_char,
    value_type: u32,
    values: *const std::ffi::c_void,
    nulls: *const u8,
    count: u32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if ds.is_null() {
        write_err(err, err_cap, "数据集为空");
        return 1;
    }
    let table = match require_name(table, "表名") {
        Ok(s) => s.to_string(),
        Err(e) => { write_err(err, err_cap, &e); return 2; }
    };
    let name = match require_name(name, "列名") {
        Ok(s) if !s.is_empty() => s.to_string(),
        Ok(_) => { write_err(err, err_cap, "列名为空"); return 2; }
        Err(e) => { write_err(err, err_cap, &e); return 2; }
    };
    let unit = if unit.is_null() {
        String::new()
    } else {
        CStr::from_ptr(unit).to_string_lossy().into_owned()
    };
    let Some(vt) = map_value_type(value_type) else {
        write_err(err, err_cap, &format!("未知列类型：{value_type}"));
        return 2;
    };
    let n = count as usize;
    let Some(table_idx) = (*ds).typed.table_index(&table) else {
        write_err(err, err_cap, &format!("表不存在：{table}"));
        return 4;
    };
    if (&(*ds).typed.tables)[table_idx].row_count() != n {
        write_err(err, err_cap, "列点数与表行数不一致");
        return 5;
    }
    let data = match typed_column_data(value_type, values, n) {
        Ok(x) => x,
        Err(e) => { write_err(err, err_cap, &e); return 3; }
    };
    let null_bits = if nulls.is_null() {
        ps_ndf::typed::NullBits::new(n)
    } else {
        ps_ndf::typed::NullBits::from_raw(std::slice::from_raw_parts(nulls, n.div_ceil(8)), n)
    };
    let column = match ps_ndf::typed::Column::with_nulls(name, unit, data, null_bits) {
        Ok(c) => c,
        Err(e) => { write_err(err, err_cap, &e); return 5; }
    };
    if column.value_type() != vt {
        write_err(err, err_cap, "列类型与数据不一致");
        return 5;
    }
    match (*ds).typed.add_column(table_idx, column) {
        Ok(()) => { (*ds).infos_dirty = true; 0 }
        Err(e) => { write_err(err, err_cap, &e); 5 }
    }
}

#[repr(C)]
pub struct PsFrameColumn {
    pub name: *const c_char,
    pub unit: *const c_char,
    pub value_type: u32,
    pub values: *const std::ffi::c_void,
    pub nulls: *const u8,
}

fn typed_nulls(nulls: *const u8, n: usize) -> ps_ndf::typed::NullBits {
    if nulls.is_null() {
        ps_ndf::typed::NullBits::new(n)
    } else {
        ps_ndf::typed::NullBits::from_raw(unsafe { std::slice::from_raw_parts(nulls, n.div_ceil(8)) }, n)
    }
}

/// 设置 live ring 字节预算。`table == NULL` 表示 Dataset 默认预算，作用于现有和新建表；
/// 指定表名只改该表。`bytes == 0` 表示取消 ring（恢复不裁剪）。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_set_ring_budget(
    ds: *mut PsDataset,
    table: *const c_char,
    bytes: u64,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if ds.is_null() {
        write_err(err, err_cap, "数据集为空");
        return 1;
    }
    let table_name = if table.is_null() {
        None
    } else {
        match require_name(table, "表名") {
            Ok(s) if s.is_empty() => None,
            Ok(s) => Some(s.to_string()),
            Err(e) => { write_err(err, err_cap, &e); return 2; }
        }
    };
    let budget = if bytes == 0 { None } else { Some(bytes as usize) };
    match (*ds).typed.set_ring_budget(table_name.as_deref(), budget) {
        Ok(()) => 0,
        Err(e) => { write_err(err, err_cap, &e); 3 }
    }
}

#[no_mangle]
pub unsafe extern "C" fn ps_dataset_table_rows(ds: *mut PsDataset, table: *const c_char) -> u64 {
    if ds.is_null() || table.is_null() {
        return 0;
    }
    let Ok(name) = CStr::from_ptr(table).to_str() else {
        return 0;
    };
    (*ds).typed.find_table(name).map(|t| t.row_count() as u64).unwrap_or(0)
}

/// typed 表的视口 LOD：按列名取列，在 `[t0_us, t1_us]` 内分桶输出 `(time_sec, min, max)`。
/// `out` 长度至少 `cap_triples * 3`；返回实际写入的三元组数。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_lod_minmax(
    ds: *mut PsDataset,
    table: *const c_char,
    column: *const c_char,
    t0_us: i64,
    t1_us: i64,
    px: u32,
    out: *mut f64,
    cap_triples: u32,
) -> u32 {
    if ds.is_null() || table.is_null() || column.is_null() || out.is_null() {
        return 0;
    }
    let (Ok(table_name), Ok(column_name)) = (CStr::from_ptr(table).to_str(), CStr::from_ptr(column).to_str()) else {
        return 0;
    };
    let Some(table) = (*ds).typed.find_table(table_name) else {
        return 0;
    };
    let Some(col_idx) = table.column_index(column_name) else {
        return 0;
    };
    if cap_triples == 0 {
        return 0;
    }
    let mut tmp = vec![0.0f64; cap_triples as usize * 3];
    let n = table.lod_minmax(col_idx, t0_us, t1_us, px as usize, &mut tmp);
    let take = n.min(cap_triples as usize);
    std::ptr::copy_nonoverlapping(tmp.as_ptr(), out, take * 3);
    take as u32
}

/// frame_batch 落库：表不存在则按首批列建表，存在则校验列顺序后追加；seq 表内自增。
/// 返回 0 成功；1 数据集为空；2 表名/列名/类型非法；3 值数组为空；4 行数为 0；5 列不一致。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_append_frame(
    ds: *mut PsDataset,
    table: *const c_char,
    time_us: *const i64,
    rows: u32,
    columns: *const PsFrameColumn,
    column_count: u32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if ds.is_null() {
        write_err(err, err_cap, "数据集为空");
        return 1;
    }
    let table = match require_name(table, "表名") {
        Ok(s) if !s.is_empty() => s.to_string(),
        Ok(_) => { write_err(err, err_cap, "表名为空"); return 2; }
        Err(e) => { write_err(err, err_cap, &e); return 2; }
    };
    if rows == 0 {
        write_err(err, err_cap, "frame_batch 至少需要 1 行");
        return 4;
    }
    if time_us.is_null() {
        write_err(err, err_cap, "时间数组为空");
        return 3;
    }
    if column_count > 0 && columns.is_null() {
        write_err(err, err_cap, "列数组为空");
        return 2;
    }
    let times = std::slice::from_raw_parts(time_us, rows as usize).to_vec();
    let mut cols = Vec::with_capacity(column_count as usize);
    for i in 0..column_count as usize {
        let col = &*columns.add(i);
        let name = match require_name(col.name, "列名") {
            Ok(s) if !s.is_empty() => s,
            _ => { write_err(err, err_cap, "列名为空"); return 2; }
        };
        let unit = if col.unit.is_null() {
            String::new()
        } else {
            CStr::from_ptr(col.unit).to_string_lossy().into_owned()
        };
        let Some(vt) = map_value_type(col.value_type) else {
            write_err(err, err_cap, &format!("未知列类型：{}", col.value_type));
            return 2;
        };
        let data = match typed_column_data(col.value_type, col.values, rows as usize) {
            Ok(x) => x,
            Err(e) => { write_err(err, err_cap, &e); return 3; }
        };
        if data.value_type() != vt {
            write_err(err, err_cap, "列类型与数据不一致");
            return 2;
        }
        cols.push((name.to_string(), unit, data, typed_nulls(col.nulls, rows as usize)));
    }
    match (*ds).typed.append_frame(&table, times, cols) {
        Ok(()) => { (*ds).infos_dirty = true; 0 }
        Err(e) => { write_err(err, err_cap, &e); 5 }
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


/// 批量视口抽稀：一次 FFI 处理多个通道，通道数足够多时用 std::thread 并行。
/// 每个通道的 `(x,min,max)` 写入 `out + c*out_stride`，实际个数写 `out_counts[c]`。
#[no_mangle]
pub unsafe extern "C" fn ps_lod_minmax_many(
    time: *const *const f64,
    value: *const *const f64,
    count: *const u32,
    channels: u32,
    t0: f64,
    t1: f64,
    px: u32,
    out: *mut f64,
    out_stride: u32,
    out_counts: *mut u32,
) -> u32 {
    if time.is_null() || value.is_null() || count.is_null() || out.is_null()
        || out_counts.is_null() || out_stride < 3
    {
        return 0;
    }
    let n = channels as usize;
    if n == 0 {
        return 0;
    }
    let stride = out_stride as usize;

    unsafe fn fill(
        time: *const *const f64,
        value: *const *const f64,
        count: *const u32,
        c: usize,
        t0: f64,
        t1: f64,
        px: u32,
        out: *mut f64,
        out_counts: *mut u32,
        stride: usize,
    ) {
        let tp = *time.add(c);
        let vp = *value.add(c);
        let np = *count.add(c);
        if tp.is_null() || vp.is_null() || np == 0 {
            *out_counts.add(c) = 0;
            return;
        }
        let out_slice = std::slice::from_raw_parts_mut(out.add(c * stride), stride);
        let got = ps_ndf::lod_minmax_into(
            std::slice::from_raw_parts(tp, np as usize),
            std::slice::from_raw_parts(vp, np as usize),
            t0,
            t1,
            px as usize,
            out_slice,
        );
        *out_counts.add(c) = got as u32;
    }

    // 少量通道不值得起线程；200 通道这种场景才并行。
    let workers = std::thread::available_parallelism()
        .map(|p| p.get())
        .unwrap_or(1)
        .min(n);
    if n < 8 || workers <= 1 {
        for c in 0..n {
            fill(time, value, count, c, t0, t1, px, out, out_counts, stride);
        }
        return channels;
    }

    let time_addr = time as usize;
    let value_addr = value as usize;
    let count_addr = count as usize;
    let out_addr = out as usize;
    let counts_addr = out_counts as usize;
    let chunk = (n + workers - 1) / workers;
    std::thread::scope(|scope| {
        for w in 0..workers {
            let start = w * chunk;
            let end = (start + chunk).min(n);
            if start >= end {
                break;
            }
            scope.spawn(move || {
                let time = time_addr as *const *const f64;
                let value = value_addr as *const *const f64;
                let count = count_addr as *const u32;
                let out = out_addr as *mut f64;
                let out_counts = counts_addr as *mut u32;
                for c in start..end {
                    unsafe {
                        fill(time, value, count, c, t0, t1, px, out, out_counts, stride);
                    }
                }
            });
        }
    });
    channels
}

/* ---- 分析类算子：stats / fft / psd / iir / decimate ----
 * 统一约定：结果写进调用方缓冲区，cap 不足返回 6 并回填实际长度。 */

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PsStats {
    pub count: u32,
    pub mean: f64,
    pub std_dev: f64,
    pub min: f64,
    pub max: f64,
    pub rms: f64,
    pub median: f64,
}

fn write_len(out_n: *mut u32, len: usize) {
    if !out_n.is_null() {
        unsafe { *out_n = len as u32 };
    }
}

/// 单通道统计。`with_median` 非 0 时多一次排序。
#[no_mangle]
pub unsafe extern "C" fn ps_stats_series(
    v: *const f64,
    n: u32,
    with_median: i32,
    out: *mut PsStats,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if v.is_null() || out.is_null() {
        write_err(err, err_cap, "输入或输出为空");
        return 1;
    }
    let data = std::slice::from_raw_parts(v, n as usize);
    let st = ps_ops::stats::series(data, with_median != 0);
    *out = PsStats {
        count: st.count,
        mean: st.mean,
        std_dev: st.std_dev,
        min: st.min,
        max: st.max,
        rms: st.rms,
        median: st.median,
    };
    0
}

/// 单边幅度谱。返回长度 = nfft/2+1（nfft 为补零后的 2 的幂）。
#[no_mangle]
pub unsafe extern "C" fn ps_fft_series(
    v: *const f64,
    n: u32,
    fs: f64,
    window: i32,
    out_freq: *mut f64,
    out_mag: *mut f64,
    cap: u32,
    out_m: *mut u32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if v.is_null() {
        write_err(err, err_cap, "输入为空");
        return 1;
    }
    if fs <= 0.0 {
        write_err(err, err_cap, "采样率必须为正");
        return 2;
    }
    let data = std::slice::from_raw_parts(v, n as usize);
    let (freq, mag) = ps_ops::fft::amplitude(data, fs, ps_ops::fft::Window::from_code(window));
    write_len(out_m, freq.len());
    if freq.len() > cap as usize {
        return 6;
    }
    if freq.is_empty() {
        return 0;
    }
    if out_freq.is_null() || out_mag.is_null() {
        write_err(err, err_cap, "输出缓冲区为空");
        return 1;
    }
    std::ptr::copy_nonoverlapping(freq.as_ptr(), out_freq, freq.len());
    std::ptr::copy_nonoverlapping(mag.as_ptr(), out_mag, mag.len());
    0
}

/// Welch 功率谱密度。`nfft` 为 0 时取 256；`overlap` 常用 0.5。
#[no_mangle]
pub unsafe extern "C" fn ps_psd_series(
    v: *const f64,
    n: u32,
    fs: f64,
    nfft: u32,
    overlap: f64,
    window: i32,
    out_freq: *mut f64,
    out_psd: *mut f64,
    cap: u32,
    out_m: *mut u32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if v.is_null() {
        write_err(err, err_cap, "输入为空");
        return 1;
    }
    if fs <= 0.0 {
        write_err(err, err_cap, "采样率必须为正");
        return 2;
    }
    let data = std::slice::from_raw_parts(v, n as usize);
    let (freq, psd) = ps_ops::psd::welch(
        data,
        fs,
        nfft as usize,
        overlap,
        ps_ops::fft::Window::from_code(window),
    );
    write_len(out_m, freq.len());
    if freq.len() > cap as usize {
        return 6;
    }
    if freq.is_empty() {
        return 0;
    }
    if out_freq.is_null() || out_psd.is_null() {
        write_err(err, err_cap, "输出缓冲区为空");
        return 1;
    }
    std::ptr::copy_nonoverlapping(freq.as_ptr(), out_freq, freq.len());
    std::ptr::copy_nonoverlapping(psd.as_ptr(), out_psd, psd.len());
    0
}

/// 巴特沃斯 IIR。kind：0 低通、1 高通、2 带通（带通用 `bandwidth` 定带宽比例）。
#[no_mangle]
pub unsafe extern "C" fn ps_iir_series(
    v: *const f64,
    n: u32,
    fs: f64,
    fc: f64,
    order: u32,
    kind: i32,
    bandwidth: f64,
    out: *mut f64,
    cap: u32,
    out_n: *mut u32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if v.is_null() {
        write_err(err, err_cap, "输入为空");
        return 1;
    }
    if fs <= 0.0 || fc <= 0.0 {
        write_err(err, err_cap, "采样率与截止频率必须为正");
        return 2;
    }
    let data = std::slice::from_raw_parts(v, n as usize);
    let y = ps_ops::iir::butterworth(
        data,
        fs,
        fc,
        order as usize,
        ps_ops::iir::Kind::from_code(kind),
        bandwidth,
    );
    write_len(out_n, y.len());
    if y.len() > cap as usize {
        return 6;
    }
    if y.is_empty() {
        return 0;
    }
    if out.is_null() {
        write_err(err, err_cap, "输出缓冲区为空");
        return 1;
    }
    std::ptr::copy_nonoverlapping(y.as_ptr(), out, y.len());
    0
}

/// 抗混叠抽取。时间轴与值同步抽取，长度约 n/factor。
#[no_mangle]
pub unsafe extern "C" fn ps_decimate_series(
    t: *const f64,
    v: *const f64,
    n: u32,
    factor: u32,
    out_t: *mut f64,
    out_v: *mut f64,
    cap: u32,
    out_n: *mut u32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if t.is_null() || v.is_null() {
        write_err(err, err_cap, "输入为空");
        return 1;
    }
    if factor == 0 {
        write_err(err, err_cap, "抽取因子必须 >= 1");
        return 2;
    }
    let ts = std::slice::from_raw_parts(t, n as usize);
    let vs = std::slice::from_raw_parts(v, n as usize);
    let (ot, ov) = ps_ops::decimate::decimate(ts, vs, factor as usize);
    write_len(out_n, ot.len());
    if ot.len() > cap as usize {
        return 6;
    }
    if ot.is_empty() {
        return 0;
    }
    if out_t.is_null() || out_v.is_null() {
        write_err(err, err_cap, "输出缓冲区为空");
        return 1;
    }
    std::ptr::copy_nonoverlapping(ot.as_ptr(), out_t, ot.len());
    std::ptr::copy_nonoverlapping(ov.as_ptr(), out_v, ov.len());
    0
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

    #[test]
    fn typed_table_and_null_roundtrip() {
        unsafe {
            let ds = ps_dataset_new();
            let table = CString::new("ATTITUDE").unwrap();
            let times = [0i64, 1_000_000, 2_000_000];
            let rc = ps_dataset_add_table_typed(
                ds,
                table.as_ptr(),
                times.as_ptr(),
                times.len() as u32,
                std::ptr::null_mut(),
                0,
            );
            assert_eq!(rc, 0);

            let name = CString::new("roll").unwrap();
            let unit = CString::new("deg").unwrap();
            let values = [1.0f32, 2.0, 3.0];
            // row 1 = null
            let nulls = [0b10u8, 0u8];
            let rc = ps_dataset_add_table_column_typed(
                ds,
                table.as_ptr(),
                name.as_ptr(),
                unit.as_ptr(),
                1, // F32
                values.as_ptr() as *const std::ffi::c_void,
                nulls.as_ptr(),
                values.len() as u32,
                std::ptr::null_mut(),
                0,
            );
            assert_eq!(rc, 0);

            let d = &*ds;
            assert_eq!(d.typed.tables.len(), 1);
            let t = &d.typed.tables[0];
            assert_eq!(t.name, "ATTITUDE");
            assert_eq!(t.row_count(), 3);
            assert_eq!(t.columns.len(), 1);
            assert_eq!(t.columns[0].name, "roll");
            assert_eq!(t.columns[0].unit, "deg");
            assert_eq!(t.columns[0].value_type(), ps_ndf::typed::ValueType::F32);
            assert_eq!(t.columns[0].value_as_f64(0), Some(1.0));
            assert_eq!(t.columns[0].value_as_f64(1), None);
            assert_eq!(t.columns[0].value_as_f64(2), Some(3.0));

            ps_dataset_free(ds);
        }
    }

    #[test]
    fn append_frame_creates_then_extends() {
        unsafe {
            let ds = ps_dataset_new();
            let table = CString::new("ATTITUDE").unwrap();
            let name = CString::new("roll").unwrap();
            let unit = CString::new("deg").unwrap();
            let t1 = [0i64, 1_000];
            let v1 = [1.0f32, 2.0];
            let n1 = [0b10u8, 0u8];
            let c1 = PsFrameColumn {
                name: name.as_ptr(),
                unit: unit.as_ptr(),
                value_type: 1,
                values: v1.as_ptr() as *const std::ffi::c_void,
                nulls: n1.as_ptr(),
            };
            let rc = ps_dataset_append_frame(
                ds,
                table.as_ptr(),
                t1.as_ptr(),
                2,
                &c1,
                1,
                std::ptr::null_mut(),
                0,
            );
            assert_eq!(rc, 0);

            let t2 = [2_000i64];
            let v2 = [3.0f32];
            let n2 = [0b1u8];
            let c2 = PsFrameColumn {
                name: name.as_ptr(),
                unit: unit.as_ptr(),
                value_type: 1,
                values: v2.as_ptr() as *const std::ffi::c_void,
                nulls: n2.as_ptr(),
            };
            let rc = ps_dataset_append_frame(
                ds,
                table.as_ptr(),
                t2.as_ptr(),
                1,
                &c2,
                1,
                std::ptr::null_mut(),
                0,
            );
            assert_eq!(rc, 0);

            let d = &*ds;
            let t = d.typed.find_table("ATTITUDE").unwrap();
            assert_eq!(t.row_count(), 3);
            assert_eq!(t.seq, vec![0, 1, 2]);
            assert_eq!(t.time_us.as_slice(), &[0, 1_000, 2_000]);
            assert_eq!(t.columns[0].value_as_f64(0), Some(1.0));
            assert_eq!(t.columns[0].value_as_f64(1), None);
            assert_eq!(t.columns[0].value_as_f64(2), None);
            ps_dataset_free(ds);
        }
    }

    #[test]
    fn ring_budget_drops_oldest_via_abi() {
        unsafe {
            let ds = ps_dataset_new();
            let rc = ps_dataset_set_ring_budget(ds, std::ptr::null(), 125, std::ptr::null_mut(), 0);
            assert_eq!(rc, 0);

            let table = CString::new("ATTITUDE").unwrap();
            let name = CString::new("roll").unwrap();
            let unit = CString::new("deg").unwrap();
            let times: Vec<i64> = (0..10).map(|i| i * 1_000).collect();
            let values: Vec<f64> = (0..10).map(|i| i as f64).collect();
            let nulls = [0u8; 2];
            let col = PsFrameColumn {
                name: name.as_ptr(),
                unit: unit.as_ptr(),
                value_type: 0,
                values: values.as_ptr() as *const std::ffi::c_void,
                nulls: nulls.as_ptr(),
            };
            let rc = ps_dataset_append_frame(
                ds,
                table.as_ptr(),
                times.as_ptr(),
                times.len() as u32,
                &col,
                1,
                std::ptr::null_mut(),
                0,
            );
            assert_eq!(rc, 0);

            let d = &*ds;
            let t = d.typed.find_table("ATTITUDE").unwrap();
            assert_eq!(t.row_count(), 5);
            assert_eq!(t.time_us.as_slice(), &[5_000, 6_000, 7_000, 8_000, 9_000]);
            assert_eq!(t.seq, vec![5, 6, 7, 8, 9]);
            assert_eq!(t.columns[0].value_as_f64(0), Some(5.0));
            ps_dataset_free(ds);
        }
    }

    #[test]
    fn typed_lod_from_dataset() {
        unsafe {
            let ds = ps_dataset_new();
            let table = CString::new("ATTITUDE").unwrap();
            let name = CString::new("roll").unwrap();
            let unit = CString::new("deg").unwrap();
            let times = [0i64, 1_000, 2_000, 3_000];
            let values = [0.0f64, 1.0, 2.0, 3.0];
            let col = PsFrameColumn {
                name: name.as_ptr(),
                unit: unit.as_ptr(),
                value_type: 0,
                values: values.as_ptr() as *const std::ffi::c_void,
                nulls: std::ptr::null(),
            };
            assert_eq!(
                ps_dataset_append_frame(ds, table.as_ptr(), times.as_ptr(), 4, &col, 1, std::ptr::null_mut(), 0),
                0
            );
            let mut out = [0.0f64; 6];
            let n = ps_dataset_lod_minmax(
                ds, table.as_ptr(), name.as_ptr(), 0, 3_000, 2, out.as_mut_ptr(), 2
            );
            assert_eq!(n, 2);
            assert_eq!(out[3], 0.002);
            assert_eq!(out[4], 2.0);
            assert_eq!(out[5], 3.0);
            ps_dataset_free(ds);
        }
    }
}



/// typed 表按行取一个标量值。返回 0 成功，1 越界，2 该行为 null。
/// 时间输出为秒，值输出为 f64（原类型在表内仍按 typed 存储）。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_value_at(
    ds: *mut PsDataset,
    table: *const c_char,
    column: *const c_char,
    row: u64,
    out_time: *mut f64,
    out_value: *mut f64,
) -> i32 {
    if ds.is_null() || table.is_null() || column.is_null() {
        return 1;
    }
    let ds = &*ds;
    let tname = CStr::from_ptr(table).to_string_lossy();
    let cname = CStr::from_ptr(column).to_string_lossy();
    let Some(t) = ds.typed.tables.iter().find(|t| t.name == tname) else { return 1; };
    let Some(c) = t.columns.iter().find(|c| c.name == cname) else { return 1; };
    let idx = row as usize;
    if idx >= t.row_count() || idx >= c.len() {
        return 1;
    }
    if !out_time.is_null() {
        *out_time = t.time_us[idx] as f64 * 1e-6;
    }
    if !out_value.is_null() {
        *out_value = c.value_as_f64(idx).unwrap_or(f64::NAN);
    }
    if c.is_null(idx) { 2 } else { 0 }
}

/// typed 表按时间取最近行；t_us 为 UTC 微秒。返回 0 成功，1 空表/找不到。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_value_near(
    ds: *mut PsDataset,
    table: *const c_char,
    column: *const c_char,
    t_us: i64,
    out_time: *mut f64,
    out_value: *mut f64,
) -> i32 {
    if ds.is_null() || table.is_null() || column.is_null() {
        return 1;
    }
    let ds = &*ds;
    let tname = CStr::from_ptr(table).to_string_lossy();
    let cname = CStr::from_ptr(column).to_string_lossy();
    let Some(t) = ds.typed.tables.iter().find(|t| t.name == tname) else { return 1; };
    let Some(c) = t.columns.iter().find(|c| c.name == cname) else { return 1; };
    if t.time_us.is_empty() {
        return 1;
    }
    let idx = match t.time_us.binary_search(&t_us) {
        Ok(i) => i,
        Err(0) => 0,
        Err(i) if i >= t.time_us.len() => t.time_us.len() - 1,
        Err(i) => {
            if (t.time_us[i] - t_us).abs() < (t.time_us[i - 1] - t_us).abs() { i } else { i - 1 }
        }
    };
    if !out_time.is_null() {
        *out_time = t.time_us[idx] as f64 * 1e-6;
    }
    if !out_value.is_null() {
        *out_value = c.value_as_f64(idx).unwrap_or(f64::NAN);
    }
    0
}

/// typed 表按时间窗口批量取值；超过 cap 时等步长抽稀。输出 time 秒 + f64 兼容值。
/// 返回实际写入的行数。Null 值输出 NaN。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_read_window(
    ds: *mut PsDataset,
    table: *const c_char,
    column: *const c_char,
    t0_us: i64,
    t1_us: i64,
    out_time: *mut f64,
    out_value: *mut f64,
    cap: u32,
) -> u32 {
    if ds.is_null() || table.is_null() || column.is_null() || out_time.is_null() || out_value.is_null() || cap == 0 {
        return 0;
    }
    let ds = &*ds;
    let tname = CStr::from_ptr(table).to_string_lossy();
    let cname = CStr::from_ptr(column).to_string_lossy();
    let Some(t) = ds.typed.tables.iter().find(|t| t.name == tname) else { return 0; };
    let Some(c) = t.columns.iter().find(|c| c.name == cname) else { return 0; };
    let lo = t.time_us.partition_point(|&x| x < t0_us);
    let hi = t.time_us.partition_point(|&x| x <= t1_us);
    if hi <= lo {
        return 0;
    }
    let rows = hi - lo;
    let step = ((rows as u64 + cap as u64 - 1) / cap as u64).max(1) as usize;
    let mut n = 0u32;
    let mut i = lo;
    while i < hi && n < cap {
        *out_time.add(n as usize) = t.time_us[i] as f64 * 1e-6;
        *out_value.add(n as usize) = c.value_as_f64(i).unwrap_or(f64::NAN);
        n += 1;
        i += step;
    }
    n
}
