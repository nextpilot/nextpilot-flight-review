//! ps-abi：App 与内核之间唯一的门（D9 / D16）。
//! 所有跨语言调用都在这里收口，`src/app` 只 include `ps_kernel.h`。
//! Rust 端 panic 不允许穿过 FFI，全部 catch 成错误码。

use std::ffi::CString;
use std::os::raw::c_char;
use std::sync::OnceLock;

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
}

/// C 侧看到的不透明数据集。真正的通道数组由 `ps_ndf::Dataset` 持有；
/// 这里只缓存 C 视图和 CString 名，避免每次取通道都重新分配。
pub struct PsDataset {
    data: ps_ndf::Dataset,
    infos: Vec<PsChannelInfo>,
    names: Vec<CString>,
    units: Vec<CString>,
}

impl PsDataset {
    fn new() -> Self {
        Self { data: ps_ndf::Dataset::new(), infos: Vec::new(), names: Vec::new(), units: Vec::new() }
    }

    fn rebuild_infos(&mut self) {
        self.names = self.data.channels.iter().map(|c| CString::new(c.name.as_str()).unwrap_or_default()).collect();
        self.units = self.data.channels.iter().map(|c| CString::new(c.unit.as_str()).unwrap_or_default()).collect();
        self.infos = self.data.channels.iter().enumerate().map(|(i, c)| PsChannelInfo {
            name: self.names[i].as_ptr(),
            unit: self.units[i].as_ptr(),
            point_count: c.t.len().min(c.v.len()) as u32,
            time: c.t.as_ptr(),
            value: c.v.as_ptr(),
        }).collect();
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
    d.data.add_channel(ps_ndf::Series { name: name.to_string(), unit: unit.to_string(), t, v })?;
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
    let t = d.data.channels[idx].t.clone();
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
    let times: Vec<f64> = d.data.channels[channels[0]].t.clone();
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
    (*ds).infos.len() as u32
}

#[no_mangle]
pub unsafe extern "C" fn ps_dataset_channel(ds: *const PsDataset, index: u32) -> *const PsChannelInfo {
    if ds.is_null() {
        return std::ptr::null();
    }
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

/// 实时流：固定容量、按通道追加。0 全收；1 容量不足截断；2 参数错误。
#[no_mangle]
pub extern "C" fn ps_stream_new(capacity: u32) -> *mut ps_stream::Stream {
    Box::into_raw(Box::new(ps_stream::Stream::new(capacity as usize)))
}

#[no_mangle]
pub unsafe extern "C" fn ps_stream_free(stream: *mut ps_stream::Stream) {
    if !stream.is_null() {
        drop(Box::from_raw(stream));
    }
}

#[no_mangle]
pub unsafe extern "C" fn ps_stream_push(
    stream: *mut ps_stream::Stream,
    name: *const c_char,
    unit: *const c_char,
    time: *const f64,
    value: *const f64,
    count: u32,
) -> i32 {
    if stream.is_null() || name.is_null() || (count > 0 && (time.is_null() || value.is_null())) {
        return 2;
    }
    let n = count as usize;
    let t = std::slice::from_raw_parts(time, n);
    let v = std::slice::from_raw_parts(value, n);
    (*stream).push(&c_name(name), &c_name(unit), t, v)
}

#[no_mangle]
pub unsafe extern "C" fn ps_stream_channel_count(stream: *const ps_stream::Stream) -> u32 {
    if stream.is_null() { 0 } else { (*stream).channel_count() as u32 }
}

/// 返回的视图与 PsChannelInfo 同布局；指针指向流内部，下一次追加/释放后失效。
#[no_mangle]
pub unsafe extern "C" fn ps_stream_channel(
    stream: *mut ps_stream::Stream,
    index: u32,
) -> *const PsChannelInfo {
    if stream.is_null() { return std::ptr::null(); }
    (*stream).channel(index as usize).map_or(std::ptr::null(), |v| v as *const _ as *const PsChannelInfo)
}

/// 对某条流通道做视口 LOD；返回写入的三元组数。
#[no_mangle]
pub unsafe extern "C" fn ps_stream_lod(
    stream: *mut ps_stream::Stream,
    name: *const c_char,
    t0: f64,
    t1: f64,
    px: u32,
    out: *mut f64,
    cap_triples: u32,
) -> u32 {
    if stream.is_null() || out.is_null() || cap_triples == 0 {
        return 0;
    }
    let out = std::slice::from_raw_parts_mut(out, cap_triples as usize * 3);
    (*stream).lod_into(&c_name(name), t0, t1, px as usize, out) as u32
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
                t,
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


