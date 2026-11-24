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
    /// BJ1/BJ2/BJ3：按 topic 组织的时间索引 + 读语义层。只存 (time, uid) -> handle，
    /// payload 仍在 `typed.objects()` 池里；与 blob API 正交，互不干扰。
    /// `ObjectReads` 内嵌 `ObjectIndex` 并转发其入口，BJ1/BJ2 的 ABI 调用不变。
    object_reads: ps_ndf::object_read::ObjectReads,
}

/// 读守卫：`typed` 只能是 [`MatRef`]——ps-ndf 已保证「先归并、后发放」，
/// 所以拿着 Read 就等于拿着完整有序数据。其余字段为拆借用只读视图。
pub(crate) struct Read<'a> {
    pub typed: ps_ndf::typed::MatRef<'a>,
    pub data: &'a ps_ndf::Dataset,
    pub infos: &'a [PsChannelInfo],
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
            object_reads: ps_ndf::object_read::ObjectReads::new(),
        }
    }

    /// 表名无法预先确定时（通道名可能是 `T.col`，也可能落在旧 data 通道上）全表物化。
    fn materialize_all(&mut self) {
        self.typed.materialize_all();
    }

    /// 读取视图的统一入口：先归并 pending 补传段，再交出只读守卫。
    ///
    /// 这是「忘记物化」的唯一防线——所有读 ABI 都必须经此拿守卫，
    /// 而不是自己写 `let d = (*ds).read();` 再祈祷前面调过 materialize。
    /// 历史教训：D2（table_rows 少报）、D3（find_series 少行）、
    /// D6（cache 存盘丢行）、D7（preview 少行）全部是漏调物化导致。
    /// BJ4a：守卫内 `typed` 是 `MatRef`，表数据无 `&self` 绕过面。
    #[inline]
    fn read(&mut self) -> Read<'_> {
        let typed = self.typed.read();
        // 拆借用：typed 已可变借用，其余字段逐字段只读借用，互不相交。
        Read {
            typed,
            data: &self.data,
            infos: &self.infos,
        }
    }

    /// 同上，只归并指定表。有表名的读入口用它，省掉全表遍历。
    #[inline]
    fn read_table(&mut self, name: &str) -> Read<'_> {
        let typed = self.typed.read_table(name);
        Read {
            typed,
            data: &self.data,
            infos: &self.infos,
        }
    }

    /// 需要通道信息（infos）的读入口用它：归并 + 按需重建 infos。
    /// infos 本身也要读主表行数/时间轴，所以必须先归并。
    #[inline]
    fn read_infos(&mut self) -> Read<'_> {
        self.typed.materialize_all();
        self.ensure_infos();
        let typed = self.typed.read();
        Read {
            typed,
            data: &self.data,
            infos: &self.infos,
        }
    }

    /// 读取过程中需要惰性建缓存（LOD 金字塔等，内部要 `&mut`）的入口用它。
    #[inline]
    fn read_table_mut(&mut self, name: &str) -> &mut Self {
        self.typed.materialize_table(name);
        self
    }

    fn rebuild_infos(&mut self) {
        // infos 的 point_count/时间轴来自物理主表；先把补传段归并，避免少算。
        self.materialize_all();
        self.names = self.data.channels.iter().map(|c| CString::new(c.name.as_str()).unwrap_or_default()).collect();
        self.units = self.data.channels.iter().map(|c| CString::new(c.unit.as_str()).unwrap_or_default()).collect();

        // typed 表只补一份共享 f64 时间轴，供旧 UI 读 point_count/时间；列值不再复制。
        self.typed_times.clear();
        self.typed_names.clear();
        self.typed_units.clear();
        self.typed_tables.clear();
        self.typed_columns.clear();
        let typed = self.typed.read();
        for table in typed.tables() {
            let rows = table.row_count();
            let mut time = Vec::with_capacity(rows);
            for &t in table.time_ns.iter() {
                time.push(t as f64 * 1e-9);
            }
            for col in &table.columns {
                let full = if col.name == "value" {
                    table.name.clone()
                } else {
                    format!("{}.{}", table.name, col.name)
                };
                self.typed_names.push(CString::new(full).unwrap_or_default());
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
            generation: 0, // legacy 通道不参与 typed 追加代际，保持 0
        }).collect();
        let mut typed_infos = Vec::new();
        let generation = self.typed.generation();
        let self_ptr = self as *const PsDataset as *const std::ffi::c_void;
        let typed_view = self.typed.read();
        for (ti, table) in typed_view.tables().iter().enumerate() {
            for _ in 0..table.columns.len() {
                typed_infos.push(PsChannelInfo {
                    name: self.typed_names[typed_infos.len()].as_ptr(),
                    unit: self.typed_units[typed_infos.len()].as_ptr(),
                    point_count: table.row_count() as u32,
                    time: self.typed_times[ti].as_ptr(),
                    value: std::ptr::null(),
                    table: self.typed_tables[typed_infos.len()].as_ptr(),
                    column: self.typed_columns[typed_infos.len()].as_ptr(),
                    dataset: self_ptr,
                    generation, // typed 通道携带数据代际，读侧据此判断快照是否过期
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

/// 按通道全名取序列：先查派生/旧通道，再查 typed 表列。null → NaN。
fn find_series(d: &Read<'_>, name: &str) -> Option<(Vec<f64>, Vec<f64>, String, Vec<bool>)> {
    if let Some((_, ch)) = d.data.find(name) {
        return Some((ch.t.as_ref().clone(), ch.v.clone(), ch.unit.clone(),
                     vec![false; ch.v.len()]));
    }
    for table in d.typed.tables() {
        for col in &table.columns {
            let full = if table.name.is_empty() {
                col.name.clone()
            } else if col.name == "value" {
                table.name.clone()
            } else {
                format!("{}.{}", table.name, col.name)
            };
            if full != name {
                continue;
            }
            let t = table.time_ns.iter().map(|&us| us as f64 * 1e-9).collect();
            let v = (0..table.row_count())
                .map(|i| col.value_as_f64(i).unwrap_or(f64::NAN))
                .collect();
            let nulls = (0..table.row_count()).map(|i| col.is_null(i)).collect();
            return Some((t, v, col.unit.clone(), nulls));
        }
    }
    None
}

fn require_name<'a>(ptr: *const c_char, what: &str) -> Result<&'a str, String> {
    if ptr.is_null() {
        return Err(format!("{what}为空"));
    }
    unsafe { CStr::from_ptr(ptr).to_str().map_err(|_| format!("{what}不是合法 UTF-8")) }
}

/// 解析算子的 params_json：空指针 / 空白 → 空对象；非法 JSON 或非 UTF-8 → 写 err 并失败。
unsafe fn parse_params(
    params_json: *const c_char,
    err: *mut c_char,
    err_cap: u32,
) -> Result<serde_json::Value, ()> {
    if params_json.is_null() {
        return Ok(serde_json::Value::Object(Default::default()));
    }
    match CStr::from_ptr(params_json).to_str() {
        Ok(s) if s.trim().is_empty() => Ok(serde_json::Value::Object(Default::default())),
        Ok(s) => serde_json::from_str(s).map_err(|e| {
            write_err(err, err_cap, &format!("参数 JSON：{e}"));
        }),
        Err(_) => {
            write_err(err, err_cap, "参数不是合法 UTF-8");
            Err(())
        }
    }
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
    let d = (*ds).read();
    let Some((t, v, unit, input_nulls)) = find_series(&d, &input) else {
        write_err(err, err_cap, &format!("找不到输入通道：{input}"));
        return 3;
    };
    let out_name = match require_name(output, "输出通道") {
        Ok(s) if !s.is_empty() => s.to_string(),
        Ok(_) => format!("{input}.{filter_id}"),
        Err(_) => format!("{input}.{filter_id}"),
    };
    let (ot, ov) = match ps_ops::filters::apply(filter_id, &t, &v, &params) {
        Ok(x) => x,
        Err(e) => { write_err(err, err_cap, &e); return 5; }
    };
    // 输入序列已拷出，读借用在此结束；写回新列需要可变引用。
    let d = &mut *ds;
    let typed_ok = if let Some((table_name, _)) = input.rsplit_once('.') {
        if let Some(tidx) = d.typed.table_index(table_name) {
            let out_col = out_name.strip_prefix(&format!("{table_name}.")).unwrap_or(&out_name).to_string();
            let mut nulls = ps_ndf::typed::NullBits::new(ov.len());
            for i in 0..ov.len() {
                if input_nulls.get(i).copied().unwrap_or(false) || ov[i].is_nan() {
                    nulls.set_null(i, true);
                }
            }
            let col = match ps_ndf::typed::Column::with_nulls(out_col, &unit,
                ps_ndf::typed::ColumnData::F64(ov.clone()), nulls) {
                Ok(c) => c,
                Err(e) => { write_err(err, err_cap, &e); return 4; }
            };
            d.typed.table_mut(tidx).map(|t| t.add_column(col).map_err(|e| write_err(err, err_cap, &e)).is_ok()).unwrap_or(false)
        } else { false }
    } else { false };
    if typed_ok { return 0; }
    match add_channel(d, &out_name, &unit, ot, ov) {
        Ok(()) => 0,
        Err(e) => { write_err(err, err_cap, &e); return 4; }
    }
}

/// 只读预览：对通道应用过滤器，结果写进调用方缓冲区，不改数据集。
/// 返回 0 成功；6 缓冲区不足（out_n 仍给出实际长度）；其余同 apply_filter。
#[no_mangle]
pub unsafe extern "C" fn ps_preview_filter(
    ds: *mut PsDataset,
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
    // 预览走 find_series（读主表）；未归并会漏掉补传行（D7）。
    let d = (*ds).read();
    let Some((time, values, _, _)) = find_series(&d, &input) else {
        write_err(err, err_cap, &format!("找不到输入通道：{input}"));
        return 3;
    };
    let (ot, ov) = match ps_ops::filters::apply(filter_id, &time, &values, &params) {
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
    let d = (*ds).read();
    let mut series = Vec::new();
    for (label, ptr) in names {
        let name = match require_name(ptr, label) {
            Ok(s) => s,
            Err(e) => { write_err(err, err_cap, &e); return 2; }
        };
        let Some(s) = find_series(&d, name) else {
            write_err(err, err_cap, &format!("找不到四元数通道：{name}"));
            return 3;
        };
        series.push(s);
    }
    let len = series[0].0.len();
    if series.iter().any(|s| s.0.len() != len || s.1.len() != len) {
        write_err(err, err_cap, "四元数四个通道点数不一致");
        return 5;
    }
    let times = &series[0].0;
    for s in &series[1..] {
        if s.0.iter().zip(times).any(|(a, b)| (a - b).abs() > 1e-6) {
            write_err(err, err_cap, "四元数四个通道时间戳不一致");
            return 5;
        }
    }
    let wv = series[0].1.clone();
    let xv = series[1].1.clone();
    let yv = series[2].1.clone();
    let zv = series[3].1.clone();
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
pub unsafe extern "C" fn ps_dataset_channel_count(ds: *mut PsDataset) -> u32 {
    if ds.is_null() {
        return 0;
    }
    let d = (*ds).read_infos();
    d.infos.len() as u32
}

#[no_mangle]
pub unsafe extern "C" fn ps_dataset_channel(ds: *mut PsDataset, index: u32) -> *const PsChannelInfo {
    if ds.is_null() {
        return std::ptr::null();
    }
    let d = (*ds).read_infos();
    d.infos.get(index as usize).map_or(std::ptr::null(), |i| i as *const _)
}

/// 数据代际：每次成功追加（含建表）递增，ds=NULL 返回 0。
/// 读侧在追加会话前后比对，未变则派生视图无需重算（ generation 快照隔离）。
/// 纯标量读，不触碰表数据，因此保持 `*const` 且不物化。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_generation(ds: *const PsDataset) -> u64 {
    if ds.is_null() {
        return 0;
    }
    (*ds).typed.generation()
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
    ds: *mut PsDataset,
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
    // 存盘前必须归并：save_typed_dataset 按 row_count() 写行，补传段未归并会丢行（D6）。
    let d = (*ds).read();
    if d.typed.tables().is_empty() {
        write_err(err, err_cap, "数据集没有 typed 表，不能写 PSC1");
        return 2;
    }
    match ps_cache::save_typed_dataset(&p, &d.typed, source_size, source_mtime) {
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
    write_err(err, err_cap, "不是 PSC2 缓存");
    2
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

/// 表级 typed 构造：纳秒时间轴，列类型由 `ps_dataset_add_table_column_typed` 决定。
/// 返回 0 成功；1 数据集为空；2 表名非法；3 时间数组为空；4 表已存在。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_add_table_typed(
    ds: *mut PsDataset,
    table: *const c_char,
    time_ns: *const i64,
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
    if n > 0 && time_ns.is_null() {
        write_err(err, err_cap, "时间数组为空");
        return 3;
    }
    let t = if n == 0 { Vec::new() } else { std::slice::from_raw_parts(time_ns, n).to_vec() };
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
        12 => Object,
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
            12 => ColumnData::Object(Vec::new()),
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
        12 => {
            // 对象列：values 是 u64 句柄数组（由 ps_dataset_blob_put 先写入池获得）。
            // 这里只复制句柄；句柄有效性由 ObjectStore 在 get 时校验，不在此处裸指针解引用。
            ColumnData::Object(std::slice::from_raw_parts(values as *const u64, n).to_vec())
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
    // 行数校验必须看归并后的真实行数：否则补传后调用方按真实行数构造列会被误判不一致（D8）。
    let (table_idx, rows) = {
        let d = (*ds).read_table(&table);
        let Some(ti) = d.typed.table_index(&table) else {
            write_err(err, err_cap, &format!("表不存在：{table}"));
            return 4;
        };
        (ti, d.typed.tables()[ti].row_count())
    };
    if rows != n {
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
    // 行数必须含补传段里的行；否则调用方拿它作循环上界会漏读（BJ4-fix / D2）。
    let d = (*ds).read_table(name);
    d.typed.find_table(name).map(|t| t.row_count() as u64).unwrap_or(0)
}

/// typed 表的视口 LOD：按列名取列，在 `[t0_ns, t1_ns]` 内分桶输出 `(time_sec, min, max)`。
/// `out` 长度至少 `cap_triples * 3`；返回实际写入的三元组数。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_lod_minmax(
    ds: *mut PsDataset,
    table: *const c_char,
    column: *const c_char,
    t0_ns: i64,
    t1_ns: i64,
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
    // lod_minmax 需要可变引用（惰性建金字塔 + pending 归并）。
    let ds_ref = (*ds).read_table_mut(table_name);
    let Some(tidx) = ds_ref.typed.table_index(table_name) else { return 0; };
    let Some(t) = ds_ref.typed.table_mut(tidx) else { return 0; };
    let Some(col_idx) = t.column_index(column_name) else {
        return 0;
    };
    if cap_triples == 0 {
        return 0;
    }
    let mut tmp = vec![0.0f64; cap_triples as usize * 3];
    let n = t.lod_minmax(col_idx, t0_ns, t1_ns, px as usize, &mut tmp);
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
    time_ns: *const i64,
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
    if time_ns.is_null() {
        write_err(err, err_cap, "时间数组为空");
        return 3;
    }
    if column_count > 0 && columns.is_null() {
        write_err(err, err_cap, "列数组为空");
        return 2;
    }
    let times = std::slice::from_raw_parts(time_ns, rows as usize).to_vec();
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
    fn backfill_segments_are_materialized_before_abi_reads() {
        unsafe {
            let ds = ps_dataset_new();
            let table = CString::new("t").unwrap();
            let col = CString::new("v").unwrap();
            let append = |times: &[i64], vals: &[f64]| {
                let c = PsFrameColumn {
                    name: col.as_ptr(), unit: std::ptr::null(), value_type: 0,
                    values: vals.as_ptr() as *const std::ffi::c_void, nulls: std::ptr::null(),
                };
                assert_eq!(ps_dataset_append_frame(ds, table.as_ptr(), times.as_ptr(),
                    times.len() as u32, &c, 1, std::ptr::null_mut(), 0), 0);
            };
            append(&[100, 300, 500], &[1.0, 3.0, 5.0]);
            // 中部补传：批首 200 < 表尾 500，写入进 pending 段。
            append(&[400, 200], &[4.0, 2.0]);
            assert_eq!((*ds).typed.pending_stats("t").unwrap(), (1, 2));

            // read_window 读全区间：应归并 pending，返回完整有序 5 行。
            let mut ot = [0.0f64; 8];
            let mut ov = [0.0f64; 8];
            let n = ps_dataset_read_window(ds, table.as_ptr(), col.as_ptr(), 0, 1_000_000_000,
                ot.as_mut_ptr(), ov.as_mut_ptr(), 8);
            assert_eq!(n, 5);
            assert_eq!(ov[..5], [1.0, 2.0, 3.0, 4.0, 5.0]);
            assert_eq!((*ds).typed.pending_stats("t").unwrap(), (0, 0));

            // 再补一段后按行号直读：value_at 也应先物化再返回。
            append(&[150], &[1.5]);
            let mut t = 0.0f64;
            let mut v = 0.0f64;
            assert_eq!(ps_dataset_value_at(ds, table.as_ptr(), col.as_ptr(), 1, &mut t, &mut v), 0);
            assert!((v - 1.5).abs() < 1e-9, "value_at 应看到归并后的第 1 行 1.5，实际 {v}");
            assert!((t - 150e-9).abs() < 1e-12);
            ps_dataset_free(ds);
        }
    }

    /// BJ4-fix 回归：补传段未归并时加列，曾让后续 materialize 按新列数遍历段内数组而越界
    /// （`extern "C"` 不可 unwind → 整个进程 abort）。加列前先归并即消除该不变量破坏。
    #[test]
    fn backfill_then_add_column_does_not_abort_on_next_read() {
        unsafe {
            let ds = ps_dataset_new();
            let table = CString::new("t").unwrap();
            let col = CString::new("v").unwrap();
            let append = |times: &[i64], vals: &[f64]| {
                let c = PsFrameColumn {
                    name: col.as_ptr(), unit: std::ptr::null(), value_type: 0,
                    values: vals.as_ptr() as *const std::ffi::c_void, nulls: std::ptr::null(),
                };
                assert_eq!(ps_dataset_append_frame(ds, table.as_ptr(), times.as_ptr(),
                    times.len() as u32, &c, 1, std::ptr::null_mut(), 0), 0);
            };
            append(&[100, 300, 500], &[1.0, 3.0, 5.0]);
            append(&[400, 200], &[4.0, 2.0]);
            assert_eq!((*ds).typed.pending_stats("t").unwrap(), (1, 2));

            // 行数必须含补传行，调用方据此构造新列长度。
            let rows = ps_dataset_table_rows(ds, table.as_ptr());
            assert_eq!(rows, 5, "table_rows 应含补传行");

            // 按真实行数加一列（5 行），加列会先归并 pending。
            let abs = CString::new("abs").unwrap();
            let extra = [10.0f64, 20.0, 30.0, 40.0, 50.0];
            let mut err = [0i8; 256];
            let rc = ps_dataset_add_table_column_typed(
                ds, table.as_ptr(), abs.as_ptr(), std::ptr::null(), 0,
                extra.as_ptr() as *const std::ffi::c_void, std::ptr::null(), 5,
                err.as_mut_ptr(), 256,
            );
            assert_eq!(rc, 0, "加列应成功");
            assert_eq!((*ds).typed.pending_stats("t").unwrap(), (0, 0),
                "加列后不应残留未归并段");

            // 关键：读新列（旧实现此处 abort）。
            let abs_col = CString::new("abs").unwrap();
            let mut ot = [0.0f64; 8];
            let mut ov = [0.0f64; 8];
            let n = ps_dataset_read_window(ds, table.as_ptr(), abs_col.as_ptr(),
                0, 1_000_000_000, ot.as_mut_ptr(), ov.as_mut_ptr(), 8);
            assert_eq!(n, 5);
            assert_eq!(ov[..5], [10.0, 20.0, 30.0, 40.0, 50.0]);

            // 原列仍是归并后的完整有序序列。
            let mut ov2 = [0.0f64; 8];
            let n2 = ps_dataset_read_window(ds, table.as_ptr(), col.as_ptr(),
                0, 1_000_000_000, ot.as_mut_ptr(), ov2.as_mut_ptr(), 8);
            assert_eq!(n2, 5);
            assert_eq!(ov2[..5], [1.0, 2.0, 3.0, 4.0, 5.0]);

            ps_dataset_free(ds);
        }
    }

    /// BJ4-fix 回归：`find_series` 走主表行，补传段未归并时派生结果会少行。
    #[test]
    fn backfill_then_apply_filter_covers_pending_rows() {
        unsafe {
            let ds = ps_dataset_new();
            let table = CString::new("t").unwrap();
            let col = CString::new("v").unwrap();
            let append = |times: &[i64], vals: &[f64]| {
                let c = PsFrameColumn {
                    name: col.as_ptr(), unit: std::ptr::null(), value_type: 0,
                    values: vals.as_ptr() as *const std::ffi::c_void, nulls: std::ptr::null(),
                };
                assert_eq!(ps_dataset_append_frame(ds, table.as_ptr(), times.as_ptr(),
                    times.len() as u32, &c, 1, std::ptr::null_mut(), 0), 0);
            };
            append(&[100, 300, 500], &[1.0, 3.0, 5.0]);
            append(&[400, 200], &[4.0, 2.0]);

            let input = CString::new("t.v").unwrap();
            let filt = CString::new("absolute").unwrap();
            let out = CString::new("t.abs").unwrap();
            let params = CString::new("{}").unwrap();
            let mut err = [0i8; 256];
            let rc = ps_dataset_apply_filter(ds, input.as_ptr(), filt.as_ptr(),
                params.as_ptr(), out.as_ptr(), err.as_mut_ptr(), 256);
            assert_eq!(rc, 0, "过滤应成功");
            assert_eq!(ps_dataset_table_rows(ds, table.as_ptr()), 5);

            let abs = CString::new("abs").unwrap();
            let mut ot = [0.0f64; 8];
            let mut ov = [0.0f64; 8];
            let n = ps_dataset_read_window(ds, table.as_ptr(), abs.as_ptr(),
                0, 1_000_000_000, ot.as_mut_ptr(), ov.as_mut_ptr(), 8);
            assert_eq!(n, 5, "派生列应覆盖补传行");
            assert_eq!(ov[..5], [1.0, 2.0, 3.0, 4.0, 5.0]);

            ps_dataset_free(ds);
        }
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

            let d = (*ds).read();
            assert_eq!(d.typed.tables().len(), 1);
            let t = &d.typed.tables()[0];
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

            let d = (*ds).read();
            let t = d.typed.find_table("ATTITUDE").unwrap();
            assert_eq!(t.row_count(), 3);
            assert_eq!(t.seq, vec![0, 1, 2]);
            assert_eq!(t.time_ns.as_slice(), &[0, 1_000, 2_000]);
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

            let d = (*ds).read();
            let t = d.typed.find_table("ATTITUDE").unwrap();
            assert_eq!(t.row_count(), 5);
            assert_eq!(t.time_ns.as_slice(), &[5_000, 6_000, 7_000, 8_000, 9_000]);
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
            // x = 桶几何中心：时间轴为纳秒，第二桶中心 2250ns = 2.25e-6s。
            assert_eq!(out[3], 2.25e-6);
            assert_eq!(out[4], 2.0);
            assert_eq!(out[5], 3.0);
            ps_dataset_free(ds);
        }
    }

    // ===== M2.7 新 ABI =====

    /// 造一张 3 行 F32 表（row1 null），返回 ds 指针。
    unsafe fn fixture_f32_table() -> (*mut PsDataset, CString, CString) {
        let ds = ps_dataset_new();
        let table = CString::new("ATTITUDE").unwrap();
        let name = CString::new("roll").unwrap();
        let unit = CString::new("deg").unwrap();
        let times = [0i64, 1_000, 2_000];
        let values = [1.0f32, 2.0, 3.0];
        let nulls = [0b10u8, 0u8];
        let col = PsFrameColumn {
            name: name.as_ptr(),
            unit: unit.as_ptr(),
            value_type: 1,
            values: values.as_ptr() as *const std::ffi::c_void,
            nulls: nulls.as_ptr(),
        };
        assert_eq!(
            ps_dataset_append_frame(ds, table.as_ptr(), times.as_ptr(), 3, &col, 1, std::ptr::null_mut(), 0),
            0
        );
        (ds, table, name)
    }

    #[test]
    fn column_raw_borrows_typed_buffer() {
        unsafe {
            let (ds, table, name) = fixture_f32_table();
            let mut ptr: *const std::ffi::c_void = std::ptr::null();
            let mut count: u64 = 0;
            let mut ty: u32 = 99;
            let mut nulls: *const u8 = std::ptr::null();
            let rc = ps_dataset_column_raw(
                ds, table.as_ptr(), name.as_ptr(), &mut ptr, &mut count, &mut ty, &mut nulls,
            );
            assert_eq!(rc, 0);
            assert_eq!(count, 3);
            assert_eq!(ty, 1); // F32
            let v = std::slice::from_raw_parts(ptr as *const f32, 3);
            assert_eq!(v, &[1.0, 2.0, 3.0]);
            // null 位图：row1 = null。
            assert!(!nulls.is_null());
            assert_eq!(*nulls & 0b10, 0b10);
            // 不存在的列。
            let bad = CString::new("nope").unwrap();
            assert_eq!(
                ps_dataset_column_raw(ds, table.as_ptr(), bad.as_ptr(), &mut ptr, &mut count, &mut ty, std::ptr::null_mut()),
                4
            );
            ps_dataset_free(ds);
        }
    }

    #[test]
    fn slice_creates_independent_dataset() {
        unsafe {
            let (ds, table, name) = fixture_f32_table();
            let cols = [name.as_ptr()];
            let mut out: *mut PsDataset = std::ptr::null_mut();
            // 时间范围 [1000, 2000] → 行 [1,2]。
            let rc = ps_dataset_slice(
                ds, table.as_ptr(), cols.as_ptr(), 1, 1_000, 2_000, &mut out,
                std::ptr::null_mut(), 0,
            );
            assert_eq!(rc, 0);
            let sliced = (*out).read();
            assert_eq!(sliced.typed.tables().len(), 1);
            let t = &sliced.typed.tables()[0];
            assert_eq!(t.name, "ATTITUDE");
            assert_eq!(t.row_count(), 2);
            assert_eq!(t.time_ns.as_slice(), &[1_000, 2_000]);
            assert_eq!(t.seq, vec![1, 2]); // seq 可溯源
            // 源 row1 是 null → 切片后仍为 null（None）。
            assert_eq!(t.columns[0].value_as_f64(0), None);
            assert_eq!(t.columns[0].value_as_f64(1), Some(3.0));
            // 切片集独立：释放源集后仍可读。
            ps_dataset_free(ds);
            assert_eq!(t.columns[0].value_as_f64(1), Some(3.0));
            ps_dataset_free(out);
        }
    }

    #[test]
    fn slice_all_columns_and_empty_window() {
        unsafe {
            let (ds, table, _name) = fixture_f32_table();
            let mut out: *mut PsDataset = std::ptr::null_mut();
            // 全列切片。
            assert_eq!(
                ps_dataset_slice(ds, table.as_ptr(), std::ptr::null(), 0, 0, 2_000, &mut out,
                                 std::ptr::null_mut(), 0),
                0
            );
            assert_eq!((*out).read().typed.tables()[0].columns.len(), 1);
            ps_dataset_free(out);
            // 空窗口（t0 > t1 区间无行）→ 空表，仍成功。
            assert_eq!(
                ps_dataset_slice(ds, table.as_ptr(), std::ptr::null(), 0, 5_000, 6_000, &mut out,
                                 std::ptr::null_mut(), 0),
                0
            );
            assert_eq!((*out).read().typed.tables()[0].row_count(), 0);
            ps_dataset_free(out);
            // 不存在的列名。
            let bad = CString::new("nope").unwrap();
            let bad_cols = [bad.as_ptr()];
            assert_eq!(
                ps_dataset_slice(ds, table.as_ptr(), bad_cols.as_ptr(), 1, 0, 2_000, &mut out,
                                 std::ptr::null_mut(), 0),
                3
            );
            ps_dataset_free(ds);
        }
    }

    #[test]
    fn read_window_multi_aligns_columns() {
        unsafe {
            let ds = ps_dataset_new();
            let table = CString::new("ATT").unwrap();
            let times: Vec<i64> = (0..6).map(|i| (i * 1_000) as i64).collect();
            assert_eq!(
                ps_dataset_add_table_typed(ds, table.as_ptr(), times.as_ptr(), 6,
                                           std::ptr::null_mut(), 0),
                0
            );
            let a = CString::new("a").unwrap();
            let b = CString::new("b").unwrap();
            let unit = CString::new("").unwrap();
            let va: Vec<f64> = (0..6).map(|i| i as f64).collect();
            let vb: Vec<f64> = (0..6).map(|i| (i * 10) as f64).collect();
            let na: Vec<u8> = vec![0b10]; // row1 null
            assert_eq!(
                ps_dataset_add_table_column_typed(ds, table.as_ptr(), a.as_ptr(), unit.as_ptr(),
                                                  0, va.as_ptr() as *const std::ffi::c_void,
                                                  na.as_ptr(), 6, std::ptr::null_mut(), 0),
                0
            );
            assert_eq!(
                ps_dataset_add_table_column_typed(ds, table.as_ptr(), b.as_ptr(), unit.as_ptr(),
                                                  0, vb.as_ptr() as *const std::ffi::c_void,
                                                  std::ptr::null(), 6, std::ptr::null_mut(), 0),
                0
            );
            let cols = [a.as_ptr(), b.as_ptr()];
            let mut time = [0.0f64; 6];
            let mut bufa = [0.0f64; 6];
            let mut bufb = [0.0f64; 6];
            let out_bufs: [*mut f64; 2] = [bufa.as_mut_ptr(), bufb.as_mut_ptr()];
            let n = ps_dataset_read_window_multi(
                ds, table.as_ptr(), cols.as_ptr(), 2, 0, 5_000,
                time.as_mut_ptr(), out_bufs.as_ptr(), 6,
            );
            assert_eq!(n, 6);
            // 行严格对齐：第 2 行 time=2000ns=2e-6s，a=2，b=20；null → NaN。
            assert!((time[2] - 2e-6).abs() < 1e-9);
            assert!((bufa[2] - 2.0).abs() < 1e-9);
            assert!((bufb[2] - 20.0).abs() < 1e-9);
            assert!(bufa[1].is_nan());
            assert_eq!(bufb[1], 10.0);
            ps_dataset_free(ds);
        }
    }

    #[test]
    fn stats_quantiles_abi() {
        unsafe {
            let v = [1.0f64, 2.0, 3.0, 4.0];
            let probs = [0.5f64, 0.95];
            let mut out = [f64::NAN; 2];
            let rc = ps_stats_quantiles(
                v.as_ptr(), v.len() as u32, probs.as_ptr(), probs.len() as u32,
                out.as_mut_ptr(), out.len() as u32, std::ptr::null_mut(), 0,
            );
            assert_eq!(rc, 0);
            assert!((out[0] - 2.5).abs() < 1e-12);
            assert!((out[1] - 3.85).abs() < 1e-12);
            // 缓冲不足拒绝。
            let rc = ps_stats_quantiles(
                v.as_ptr(), v.len() as u32, probs.as_ptr(), 2,
                out.as_mut_ptr(), 1, std::ptr::null_mut(), 0,
            );
            assert_eq!(rc, 2);
        }
    }

    // S3：generation 随追加单调递增；PsChannelInfo.generation 与查询一致。
    #[test]
    fn generation_increments_on_append() {
        unsafe {
            let ds = ps_dataset_new();
            assert_eq!(ps_dataset_generation(ds), 0);

            let table = CString::new("T").unwrap();
            let name = CString::new("v").unwrap();
            let unit = CString::new("").unwrap();
            let times = [0i64, 1_000, 2_000];
            let vals = [1.0f64, 2.0, 3.0];
            let col = PsFrameColumn {
                name: name.as_ptr(), unit: unit.as_ptr(), value_type: 0,
                values: vals.as_ptr() as *const std::ffi::c_void, nulls: std::ptr::null(),
            };
            // 建表 = 第 1 代。
            assert_eq!(ps_dataset_append_frame(ds, table.as_ptr(), times.as_ptr(), 3,
                &col, 1, std::ptr::null_mut(), 0), 0);
            assert_eq!(ps_dataset_generation(ds), 1);
            // info 的 generation 与查询一致。
            let ch = ps_dataset_channel(ds, 0);
            assert!(!ch.is_null());
            assert_eq!((*ch).generation, 1);

            // 第二次追加 = 第 2 代。
            let times2 = [3_000i64, 4_000];
            let vals2 = [4.0f64, 5.0];
            let col2 = PsFrameColumn {
                name: name.as_ptr(), unit: unit.as_ptr(), value_type: 0,
                values: vals2.as_ptr() as *const std::ffi::c_void, nulls: std::ptr::null(),
            };
            assert_eq!(ps_dataset_append_frame(ds, table.as_ptr(), times2.as_ptr(), 2,
                &col2, 1, std::ptr::null_mut(), 0), 0);
            assert_eq!(ps_dataset_generation(ds), 2);

            // 失败的追加（列类型不一致）不应递增代际。
            let ts3 = [5_000i64];
            let vs3 = [6.0f64];
            let bad = PsFrameColumn {
                name: name.as_ptr(), unit: unit.as_ptr(), value_type: 4, // PS_VALUE_I32，与 F64 不符
                values: vs3.as_ptr() as *const std::ffi::c_void, nulls: std::ptr::null(),
            };
            let _ = ts3;
            assert_ne!(ps_dataset_append_frame(ds, table.as_ptr(), ts3.as_ptr(), 1,
                &bad, 1, std::ptr::null_mut(), 0), 0);
            assert_eq!(ps_dataset_generation(ds), 2);
            ps_dataset_free(ds);
        }
    }

    // S3：generation 快照隔离——追加前后取到的代际不同，可据此判定派生视图需刷新。
    #[test]
    fn generation_snapshot_isolation() {
        unsafe {
            let ds = ps_dataset_new();
            let table = CString::new("T").unwrap();
            let name = CString::new("v").unwrap();
            let unit = CString::new("").unwrap();
            let col = PsFrameColumn {
                name: name.as_ptr(), unit: unit.as_ptr(), value_type: 0,
                values: [1.0f64].as_ptr() as *const std::ffi::c_void, nulls: std::ptr::null(),
            };
            assert_eq!(ps_dataset_append_frame(ds, table.as_ptr(), [0i64].as_ptr(), 1,
                &col, 1, std::ptr::null_mut(), 0), 0);
            let snap_before = ps_dataset_generation(ds); // 读侧快照

            let col2 = PsFrameColumn {
                name: name.as_ptr(), unit: unit.as_ptr(), value_type: 0,
                values: [2.0f64].as_ptr() as *const std::ffi::c_void, nulls: std::ptr::null(),
            };
            assert_eq!(ps_dataset_append_frame(ds, table.as_ptr(), [1_000i64].as_ptr(), 1,
                &col2, 1, std::ptr::null_mut(), 0), 0);
            let snap_after = ps_dataset_generation(ds);
            assert_eq!(snap_after, snap_before + 1);
            ps_dataset_free(ds);
        }
    }

    // S3：ring 长跑内存有界 + seq 单调不归零。
    #[test]
    fn ring_long_run_keeps_seq_monotonic() {
        unsafe {
            let ds = ps_dataset_new();
            // 每行约 25B 预算，容纳约 4 行。
            assert_eq!(ps_dataset_set_ring_budget(ds, std::ptr::null(), 100,
                std::ptr::null_mut(), 0), 0);
            let table = CString::new("T").unwrap();
            let name = CString::new("v").unwrap();
            let unit = CString::new("").unwrap();
            // 连续追加 200 批，每批 1 行；内存始终有界，seq 单调递增。
            let mut last_seq = 0u64;
            for i in 0..200 {
                let t = (i as i64) * 1_000;
                let v = [i as f64];
                let col = PsFrameColumn {
                    name: name.as_ptr(), unit: unit.as_ptr(), value_type: 0,
                    values: v.as_ptr() as *const std::ffi::c_void, nulls: std::ptr::null(),
                };
                assert_eq!(ps_dataset_append_frame(ds, table.as_ptr(), &t, 1,
                    &col, 1, std::ptr::null_mut(), 0), 0);
                let d = (*ds).read();
                let tb = d.typed.find_table("T").unwrap();
                assert!(tb.row_count() <= 4, "行数为 {}，超出预算上限", tb.row_count());
                let cur = *tb.seq.last().unwrap();
                assert!(cur >= last_seq, "seq 回退: {} -> {}", last_seq, cur);
                last_seq = cur;
            }
            assert_eq!(last_seq, 199); // 末行 seq = 总追加行数 - 1，从未归零
            ps_dataset_free(ds);
        }
    }

    // ---------- S4 对象列 C ABI 句柄 ----------
    #[test]
    fn blob_handle_cross_boundary_roundtrip_and_no_dangle() {
        unsafe {
            let ds = ps_dataset_new();
            let payload = b"point-cloud-bytes";
            let h = ps_dataset_blob_put(ds, payload.as_ptr() as *const _, payload.len() as u32,
                                        42, std::ptr::null_mut(), 0);
            assert!(h > 0, "blob_put 应返回正句柄");

            // get：字节级一致 + type_tag 透传。
            let mut out = [0u8; 64];
            let mut out_len = 0u32;
            let mut tag = 0u32;
            assert_eq!(ps_dataset_blob_get(ds, h, out.as_mut_ptr() as *mut _, out.len() as u32,
                                           &mut out_len, &mut tag), 0);
            assert_eq!(out_len as usize, payload.len());
            assert_eq!(tag, 42);
            assert_eq!(&out[..out_len as usize], payload);

            // 缓冲区不足：返回 3，out_len 填所需字节数。
            let mut small = [0u8; 4];
            assert_eq!(ps_dataset_blob_get(ds, h, small.as_mut_ptr() as *mut _, small.len() as u32,
                                           &mut out_len, std::ptr::null_mut()), 3);
            assert_eq!(out_len as usize, payload.len());

            // retain 后 release 一次仍有效；再 release 归零后句柄失效（无悬垂）。
            assert_eq!(ps_dataset_blob_retain(ds, h), 0);
            assert_eq!(ps_dataset_blob_release(ds, h), 0);
            let mut o2 = [0u8; 64];
            assert_eq!(ps_dataset_blob_get(ds, h, o2.as_mut_ptr() as *mut _, o2.len() as u32,
                                           &mut out_len, std::ptr::null_mut()), 0);
            assert_eq!(ps_dataset_blob_release(ds, h), 0);
            assert_eq!(ps_dataset_blob_get(ds, h, o2.as_mut_ptr() as *mut _, o2.len() as u32,
                                           &mut out_len, std::ptr::null_mut()), 2, "释放后必须读不到");
            assert_eq!(ps_dataset_blob_release(ds, h), 2, "重复释放应失败");
            ps_dataset_free(ds);
        }
    }

    // ---------- BJ1 时间索引对象 C ABI ----------
    #[test]
    fn object_index_abi_out_of_order_insert_and_cursors() {
        unsafe {
            let ds = ps_dataset_new();
            let topic = CString::new("/tf").unwrap();
            let p = b"tf";
            // 三个句柄（时间 300、100、200，故意乱序到达）。
            let h1 = ps_dataset_blob_put(ds, p.as_ptr() as *const _, p.len() as u32, 1,
                                         std::ptr::null_mut(), 0);
            let h2 = ps_dataset_blob_put(ds, p.as_ptr() as *const _, p.len() as u32, 1,
                                         std::ptr::null_mut(), 0);
            let h3 = ps_dataset_blob_put(ds, p.as_ptr() as *const _, p.len() as u32, 1,
                                         std::ptr::null_mut(), 0);
            let u1 = ps_object_insert(ds, topic.as_ptr(), 300, h1, std::ptr::null_mut(), 0);
            let u2 = ps_object_insert(ds, topic.as_ptr(), 100, h2, std::ptr::null_mut(), 0);
            let u3 = ps_object_insert(ds, topic.as_ptr(), 200, h3, std::ptr::null_mut(), 0);
            assert!(u1 > 0 && u2 > u1 && u3 > u2, "UID 按到达序递增");

            // stats：1 topic / 3 条目。
            let mut tc = 0u32;
            let mut ec = 0u32;
            assert_eq!(ps_object_index_stats(ds, &mut tc, &mut ec), 0);
            assert_eq!((tc, ec), (1, 3));

            // latest_at：时间为序，不是到达序——<=250 命中 time=200 的 h3。
            let mut out_h = 0u64;
            let mut out_u = 0u64;
            assert_eq!(ps_object_latest_at(ds, topic.as_ptr(), 250, &mut out_h, &mut out_u), 0);
            assert_eq!((out_h, out_u), (h3, u3));
            // <=99 无匹配。
            assert_eq!(ps_object_latest_at(ds, topic.as_ptr(), 99, &mut out_h, &mut out_u), 2);

            // drain_new_since(u1)：到达序，取到 u2/u3（含时间更早的 h2）。
            let mut rows = [0u64; 9];
            let mut n = 0u32;
            assert_eq!(ps_object_drain_new_since(ds, topic.as_ptr(), u1, rows.as_mut_ptr(), 3, &mut n), 0);
            assert_eq!(n, 2);
            assert_eq!((rows[0], rows[1], rows[2]), (u2, 100, h2));
            assert_eq!((rows[3], rows[4], rows[5]), (u3, 200, h3));

            // range_by_time：闭区间 [100,200]，按时间升序。
            assert_eq!(ps_object_range_by_time(ds, topic.as_ptr(), 100, 200, rows.as_mut_ptr(), 3, &mut n), 0);
            assert_eq!(n, 2);
            assert_eq!((rows[0], rows[1], rows[2]), (u2, 100, h2));
            assert_eq!((rows[3], rows[4], rows[5]), (u3, 200, h3));

            // 缓冲不足：返回 2，out_len 填所需。
            let mut tiny = [0u64; 3];
            assert_eq!(ps_object_drain_new_since(ds, topic.as_ptr(), 0, tiny.as_mut_ptr(), 1, &mut n), 2);
            assert_eq!(n, 3);

            // 不存在的 topic。
            let missing = CString::new("/none").unwrap();
            assert_eq!(ps_object_latest_at(ds, missing.as_ptr(), 999, &mut out_h, &mut out_u), 2);
            ps_dataset_free(ds);
        }
    }

    #[test]
    fn object_retention_abi_jump_eviction_and_handle_release() {
        unsafe {
            let ds = ps_dataset_new();
            let topic = CString::new("/tf").unwrap();
            let p = b"tf";
            // 5 个 blob 句柄登记到 t=0..400。
            let mut handles = Vec::new();
            let mut uids = Vec::new();
            for i in 0..5u64 {
                let h = ps_dataset_blob_put(ds, p.as_ptr() as *const _, p.len() as u32, 1,
                                            std::ptr::null_mut(), 0);
                handles.push(h);
                uids.push(ps_object_insert(ds, topic.as_ptr(), i * 100, h, std::ptr::null_mut(), 0));
            }
            // 设 max_entries=3，应用驱逐 → 逐最旧 2 条。
            assert_eq!(ps_object_set_retention(ds, topic.as_ptr(), 0, 3), 0);
            let mut evicted = [0u64; 8];
            let mut n = 0u32;
            assert_eq!(ps_object_apply_retention(ds, topic.as_ptr(), evicted.as_mut_ptr(), 8, &mut n), 0);
            assert_eq!(n, 2);
            assert_eq!(&evicted[..2], &handles[..2], "逐最旧两个 handle");

            // 被逐 handle 仍有效（索引驱逐 ≠ 销毁 payload），可显式 release。
            let mut out = [0u8; 8];
            let mut out_len = 0u32;
            assert_eq!(ps_dataset_blob_get(ds, evicted[0], out.as_mut_ptr() as *mut _, 8,
                                           &mut out_len, std::ptr::null_mut()), 0);
            assert_eq!(ps_dataset_blob_release(ds, evicted[0]), 0);

            // 缓冲不足：返回 2，out_len 填所需，且不驱逐（handle 不丢失）。
            assert_eq!(ps_object_set_retention(ds, topic.as_ptr(), 0, 1), 0);
            let mut tiny = [0u64; 1];
            assert_eq!(ps_object_apply_retention(ds, topic.as_ptr(), tiny.as_mut_ptr(), 1, &mut n), 2);
            assert_eq!(n, 2, "需逐 2 条（3→1），缓冲只够 1");
            // 确认未驱逐：仍有 3 条。
            let mut tc = 0u32;
            let mut ec = 0u32;
            ps_object_index_stats(ds, &mut tc, &mut ec);
            assert_eq!(ec, 3, "缓冲不足时索引不变，避免 handle 泄漏");

            // 缓冲充足再逐：逐 2 条成功。
            assert_eq!(ps_object_apply_retention(ds, topic.as_ptr(), evicted.as_mut_ptr(), 8, &mut n), 0);
            assert_eq!(n, 2);
            ps_object_index_stats(ds, &mut tc, &mut ec);
            assert_eq!(ec, 1, "3→1 剩 1 条");

            // 驱逐水位 + 游标检测：first_uid 前进。
            let mut water = 0u64;
            assert_eq!(ps_object_evicted_before(ds, topic.as_ptr(), &mut water), 0);
            assert!(water >= 300, "水位应反映被逐最大时间");

            // 幂等：再逐一次为空。
            assert_eq!(ps_object_apply_retention(ds, topic.as_ptr(), evicted.as_mut_ptr(), 8, &mut n), 0);
            assert_eq!(n, 0);
            // 不存在 topic → 3。
            let missing = CString::new("/none").unwrap();
            assert_eq!(ps_object_apply_retention(ds, missing.as_ptr(), evicted.as_mut_ptr(), 8, &mut n), 3);
            ps_dataset_free(ds);
        }
    }

    #[test]
    fn object_read_abi_zero_copy_warm_cache_and_semantics() {
        unsafe {
            let ds = ps_dataset_new();
            let topic = CString::new("/img").unwrap();
            let payload = b"packed-frame-bytes";
            // 三个句柄，含乱序到达（time 100、300、200）。
            let h1 = ps_dataset_blob_put(ds, payload.as_ptr() as *const _, payload.len() as u32,
                                         7, std::ptr::null_mut(), 0);
            let u1 = ps_object_insert(ds, topic.as_ptr(), 100, h1, std::ptr::null_mut(), 0);
            let h2 = ps_dataset_blob_put(ds, payload.as_ptr() as *const _, payload.len() as u32,
                                         7, std::ptr::null_mut(), 0);
            let u2 = ps_object_insert(ds, topic.as_ptr(), 300, h2, std::ptr::null_mut(), 0);
            let h3 = ps_dataset_blob_put(ds, payload.as_ptr() as *const _, payload.len() as u32,
                                         7, std::ptr::null_mut(), 0);
            let u3 = ps_object_insert(ds, topic.as_ptr(), 200, h3, std::ptr::null_mut(), 0);
            assert!(u1 < u2 && u2 < u3);

            // 零拷贝视图：指针直接指向内核字节，内容一致。
            let mut vp: *const u8 = std::ptr::null();
            let mut vlen = 0u32;
            let mut vtag = 0u32;
            assert_eq!(ps_object_payload_view(ds, h1, &mut vp, &mut vlen, &mut vtag), 0);
            assert_eq!(vlen as usize, payload.len());
            assert_eq!(vtag, 7);
            assert_eq!(std::slice::from_raw_parts(vp, vlen as usize), payload);
            // 无效句柄 → 2。
            assert_eq!(ps_object_payload_view(ds, 99999, &mut vp, &mut vlen, &mut vtag), 2);

            // warm latestAt：time<=250 命中 time200 的 h3，第一次 miss、第二次 hit。
            let mut wh = 0u64;
            let mut wu = 0u64;
            let mut hit = 0u32;
            assert_eq!(ps_object_warm_latest_at(ds, topic.as_ptr(), 250, &mut wh, &mut wu, &mut hit), 0);
            assert_eq!((wh, wu, hit), (h3, u3, 0));
            assert_eq!(ps_object_warm_latest_at(ds, topic.as_ptr(), 250, &mut wh, &mut wu, &mut hit), 0);
            assert_eq!((wh, wu, hit), (h3, u3, 1), "同 UID 重复请求命中缓存");

            // 高水位：time<=400 的最大 UID 是 u3（迟到但 uid 最大），非 latestAt 的 u2。
            let mut high = 0u64;
            assert_eq!(ps_object_max_uid_at_or_before(ds, topic.as_ptr(), 400, &mut high), 0);
            assert_eq!(high, u3);

            // 迟到检测：消费者算到 u2(time300) 后，u3(time200, uid 更大) 是迟到到达。
            let mut ts = 0u64;
            let mut found = 0u32;
            assert_eq!(ps_object_oldest_arrival_after(ds, topic.as_ptr(), u2, &mut ts, &mut found), 0);
            assert_eq!(found, 1);
            assert_eq!(ts, 200, "u3 时间 200 < u2 的 300，属迟到");
            // u3 之后无更多条目。
            assert_eq!(ps_object_oldest_arrival_after(ds, topic.as_ptr(), u3, &mut ts, &mut found), 0);
            assert_eq!(found, 0);

            // 内容身份：append/insert 已推进 content 与 rewrite。
            let mut id = [0u64; 3];
            assert_eq!(ps_object_content_identity(ds, topic.as_ptr(), id.as_mut_ptr()), 0);
            assert!(id[0] >= 3 && id[1] >= 1, "content 每次插入推进，rewrite 因乱序推进");

            // 分池配置：容量 1MB、small 保留 256KB。
            assert_eq!(ps_object_set_pool(ds, topic.as_ptr(), 1_000_000, 256_000), 0);
            ps_dataset_free(ds);
        }
    }

    #[test]
    fn object_column_via_abi_aligned_with_scalar_and_typed_as_object() {
        unsafe {
            let ds = ps_dataset_new();
            let table = CString::new("lidar").unwrap();
            let vname = CString::new("intensity").unwrap();
            let oname = CString::new("frame").unwrap();
            let unit = CString::new("").unwrap();
            let p0 = b"frame0";
            let p1 = b"frame1";
            let h0 = ps_dataset_blob_put(ds, p0.as_ptr() as *const _, p0.len() as u32, 1,
                                         std::ptr::null_mut(), 0);
            let h1 = ps_dataset_blob_put(ds, p1.as_ptr() as *const _, p1.len() as u32, 1,
                                         std::ptr::null_mut(), 0);
            let times = [1_000i64, 2_000];
            let vals = [10.0f64, 20.0];
            let handles = [h0, h1];
            let cv = PsFrameColumn {
                name: vname.as_ptr(), unit: unit.as_ptr(), value_type: 0,
                values: vals.as_ptr() as *const _, nulls: std::ptr::null(),
            };
            let co = PsFrameColumn {
                name: oname.as_ptr(), unit: unit.as_ptr(), value_type: 12,
                values: handles.as_ptr() as *const _, nulls: std::ptr::null(),
            };
            let cols = [cv, co];
            assert_eq!(ps_dataset_append_frame(ds, table.as_ptr(), times.as_ptr(), 2,
                cols.as_ptr(), 2, std::ptr::null_mut(), 0), 0);

            // 对象列类型码 = 12；整列裸读被拒（返回 3）。
            assert_eq!(ps_dataset_column_type(ds, table.as_ptr(), oname.as_ptr()), 12);
            let mut ptr = std::ptr::null();
            let mut cnt = 0u64;
            let mut ty = 0u32;
            assert_eq!(ps_dataset_column_raw(ds, table.as_ptr(), oname.as_ptr(),
                &mut ptr, &mut cnt, &mut ty, std::ptr::null_mut()), 3);

            // 同一 t 双取：t=2ms 行，标量=20.0，对象=frame1。
            let mut tv = 0.0f64;
            let mut vv = 0.0f64;
            assert_eq!(ps_dataset_value_at(ds, table.as_ptr(), vname.as_ptr(), 1, &mut tv, &mut vv), 0);
            assert_eq!(vv, 20.0);
            let obj_handles = {
                let d = (*ds).read();
                let t = d.typed.find_table("lidar").unwrap();
                match &t.column("frame").unwrap().data {
                    ps_ndf::typed::ColumnData::Object(v) => v.clone(),
                    _ => panic!("期望 Object 列"),
                }
            };
            let mut out = [0u8; 32];
            let mut out_len = 0u32;
            assert_eq!(ps_dataset_blob_get(ds, obj_handles[1], out.as_mut_ptr() as *mut _,
                out.len() as u32, &mut out_len, std::ptr::null_mut()), 0);
            assert_eq!(&out[..out_len as usize], p1);
            ps_dataset_free(ds);
        }
    }

    #[test]
    fn column_encoding_roundtrip_via_abi() {
        unsafe {
            let ds = ps_dataset_new();
            let table = CString::new("STATES").unwrap();
            let name = CString::new("mode").unwrap();
            let unit = CString::new("").unwrap();
            let times = [0i64, 1_000_000, 2_000_000, 3_000_000];
            assert_eq!(ps_dataset_add_table_typed(ds, table.as_ptr(), times.as_ptr(), times.len() as u32,
                std::ptr::null_mut(), 0), 0);
            // I32 状态码，低基数。
            let vals: [i32; 4] = [7, 7, 3, 7];
            assert_eq!(ps_dataset_add_table_column_typed(ds, table.as_ptr(), name.as_ptr(), unit.as_ptr(),
                4, vals.as_ptr() as *const _, std::ptr::null(), vals.len() as u32,
                std::ptr::null_mut(), 0), 0);

            // 编码前：is_encoded=0，column_raw 可借出。
            assert_eq!(ps_dataset_column_is_encoded(ds, table.as_ptr(), name.as_ptr()), 0);
            let mut ptr = std::ptr::null();
            let mut cnt = 0u64;
            let mut ty = 0u32;
            assert_eq!(ps_dataset_column_raw(ds, table.as_ptr(), name.as_ptr(),
                &mut ptr, &mut cnt, &mut ty, std::ptr::null_mut()), 0);
            assert_eq!(ty, 4);

            // 开启字典编码（codec=2）。
            let mut err = [0i8; 128];
            assert_eq!(ps_dataset_column_encode(ds, table.as_ptr(), name.as_ptr(), 2,
                err.as_mut_ptr(), err.len() as u32), 0);
            assert_eq!(ps_dataset_column_is_encoded(ds, table.as_ptr(), name.as_ptr()), 1);
            // 逻辑类型码不变（I32=4）。
            assert_eq!(ps_dataset_column_type(ds, table.as_ptr(), name.as_ptr()), 4);
            // 压缩列 column_raw 返回 3，不借出裸指针。
            ptr = std::ptr::null(); cnt = 0; ty = 99;
            assert_eq!(ps_dataset_column_raw(ds, table.as_ptr(), name.as_ptr(),
                &mut ptr, &mut cnt, &mut ty, std::ptr::null_mut()), 3);
            assert!(ptr.is_null(), "失败时不得写出指针");

            // decode_f64 逐位一致。
            let mut out = [0.0f64; 4];
            assert_eq!(ps_dataset_column_decode_f64(ds, table.as_ptr(), name.as_ptr(), out.as_mut_ptr(), 4), 4);
            assert_eq!(out, [7.0, 7.0, 3.0, 7.0]);

            // 重复编码返回 3。
            assert_eq!(ps_dataset_column_encode(ds, table.as_ptr(), name.as_ptr(), 2,
                err.as_mut_ptr(), err.len() as u32), 3);
            ps_dataset_free(ds);
        }
    }

    #[test]
    fn column_encode_rejects_bad_codec_and_unsuitable_column() {
        unsafe {
            let ds = ps_dataset_new();
            let table = CString::new("T").unwrap();
            let name = CString::new("v").unwrap();
            let unit = CString::new("").unwrap();
            let times = [0i64, 1_000_000];
            assert_eq!(ps_dataset_add_table_typed(ds, table.as_ptr(), times.as_ptr(), 2,
                std::ptr::null_mut(), 0), 0);
            let vals = [1.5f64, 2.5];
            assert_eq!(ps_dataset_add_table_column_typed(ds, table.as_ptr(), name.as_ptr(), unit.as_ptr(),
                0, vals.as_ptr() as *const _, std::ptr::null(), 2, std::ptr::null_mut(), 0), 0);
            let mut err = [0i8; 128];
            // 非法 codec 返回 1。
            assert_eq!(ps_dataset_column_encode(ds, table.as_ptr(), name.as_ptr(), 99,
                err.as_mut_ptr(), err.len() as u32), 1);
            // F64 列不适合 Constant（值不同）返回 4。
            assert_eq!(ps_dataset_column_encode(ds, table.as_ptr(), name.as_ptr(), 0,
                err.as_mut_ptr(), err.len() as u32), 4);
            ps_dataset_free(ds);
        }
    }

    // S7：DAG 建图/循环检测/增量求值/输出 ABI。
    #[test]
    fn dag_incremental_via_abi() {
        use std::ffi::CString;
        unsafe {
            let dag = ps_dag_create();
            assert!(!dag.is_null());
            let mut err = [0i8; 128];

            let x = CString::new("x").unwrap();
            let t = [0.0f64, 1.0, 2.0, 3.0];
            let v = [-2.0f64, -1.0, 3.0, 4.0];
            assert_eq!(ps_dag_set_input(dag, x.as_ptr(), t.as_ptr(), v.as_ptr(), 4), 0);

            let n_abs = CString::new("abs").unwrap();
            let f_abs = CString::new("absolute").unwrap();
            let params = CString::new("{}").unwrap();
            let id_abs = ps_dag_add_node(dag, n_abs.as_ptr(), f_abs.as_ptr(), params.as_ptr(),
                x.as_ptr(), -1, err.as_mut_ptr(), err.len() as u32);
            assert!(id_abs >= 0, "add abs failed: {}", cstr(err.as_ptr()));

            let n_scale = CString::new("scale").unwrap();
            let f_scale = CString::new("scale").unwrap();
            let p_scale = CString::new("{\"value_scale\":2.0}").unwrap();
            let id_scale = ps_dag_add_node(dag, n_scale.as_ptr(), f_scale.as_ptr(), p_scale.as_ptr(),
                x.as_ptr(), id_abs, err.as_mut_ptr(), err.len() as u32);
            assert!(id_scale >= 0);
            assert_eq!(ps_dag_add_edge(dag, id_abs, id_scale, err.as_mut_ptr(), err.len() as u32), 0);
            // 循环边被拒绝。
            assert_eq!(ps_dag_add_edge(dag, id_scale, id_abs, err.as_mut_ptr(), err.len() as u32), 2);

            assert_eq!(ps_dag_evaluate(dag, 1, err.as_mut_ptr(), err.len() as u32), 0);
            assert_eq!(ps_dag_output_count(dag), 2);
            let mut ot = [0f64; 8];
            let mut ov = [0f64; 8];
            let mut on = 0u32;
            assert_eq!(ps_dag_output(dag, id_scale as u32, ot.as_mut_ptr(), ov.as_mut_ptr(), 8, &mut on), 0);
            assert_eq!(on, 4);
            assert_eq!(&ov[..4], &[4.0, 2.0, 6.0, 8.0]);

            // generation 不变重求值，输出不变；追加新行 + bump generation，尾部增量更新。
            let mut t2 = t.to_vec();
            let mut v2 = v.to_vec();
            t2.push(4.0); v2.push(-5.0);
            assert_eq!(ps_dag_set_input(dag, x.as_ptr(), t2.as_ptr(), v2.as_ptr(), 5), 0);
            assert_eq!(ps_dag_evaluate(dag, 2, err.as_mut_ptr(), err.len() as u32), 0);
            assert_eq!(ps_dag_output(dag, id_scale as u32, ot.as_mut_ptr(), ov.as_mut_ptr(), 8, &mut on), 0);
            assert_eq!(on, 5);
            assert_eq!(&ov[..5], &[4.0, 2.0, 6.0, 8.0, 10.0]);

            ps_dag_free(dag);
        }
    }

    // BJ7 阶段三：分片回放与读屏障。
    #[test]
    fn dag_staged_replay_via_abi() {
        use std::ffi::CString;
        unsafe {
            let dag = ps_dag_create();
            let mut err = [0i8; 128];
            let x = CString::new("x").unwrap();
            let t = [0.0f64, 1.0];
            let v = [1.0f64, 2.0];
            assert_eq!(ps_dag_set_input(dag, x.as_ptr(), t.as_ptr(), v.as_ptr(), 2), 0);

            let f = CString::new("scale").unwrap();
            let p = CString::new("{\"value_scale\":2.0}").unwrap();
            let n0 = CString::new("n0").unwrap();
            let id0 = ps_dag_add_node(dag, n0.as_ptr(), f.as_ptr(), p.as_ptr(),
                x.as_ptr(), -1, err.as_mut_ptr(), err.len() as u32);
            assert!(id0 >= 0);
            let n1 = CString::new("n1").unwrap();
            let id1 = ps_dag_add_node(dag, n1.as_ptr(), f.as_ptr(), p.as_ptr(),
                std::ptr::null(), id0, err.as_mut_ptr(), err.len() as u32);
            assert!(id1 >= 0);

            // budget=0：每步只推进 1 个节点，2 个节点不可能一步完成。
            let mut done = 0i32;
            assert_eq!(
                ps_dag_evaluate_step(dag, 1, 0, &mut done, err.as_mut_ptr(), err.len() as u32),
                0
            );
            assert_eq!(done, 0, "只推进了第一个节点");

            // 屏障推完剩余节点。
            assert_eq!(ps_dag_read_barrier(dag, 1, err.as_mut_ptr(), err.len() as u32), 0);
            let mut ot = [0f64; 4];
            let mut ov = [0f64; 4];
            let mut on = 0u32;
            assert_eq!(
                ps_dag_output(dag, id1 as u32, ot.as_mut_ptr(), ov.as_mut_ptr(), 4, &mut on),
                0
            );
            assert_eq!(&ov[..2], &[4.0, 8.0], "1*2*2 与 2*2*2");

            // 参数校验。
            assert_eq!(
                ps_dag_evaluate_step(std::ptr::null_mut(), 1, 0, &mut done, err.as_mut_ptr(), err.len() as u32),
                1
            );
            assert_eq!(
                ps_dag_evaluate_step(dag, 1, 0, std::ptr::null_mut(), err.as_mut_ptr(), err.len() as u32),
                1
            );
            assert_eq!(ps_dag_read_barrier(std::ptr::null_mut(), 1, err.as_mut_ptr(), err.len() as u32), 1);

            ps_dag_free(dag);
        }
    }

    // BJ7 阶段二：算子热替换。
    #[test]
    fn dag_hot_swap_via_abi() {
        use std::ffi::CString;
        unsafe {
            let dag = ps_dag_create();
            let mut err = [0i8; 128];
            let x = CString::new("x").unwrap();
            let t = [0.0f64, 1.0];
            let v = [-1.0f64, 2.0];
            assert_eq!(ps_dag_set_input(dag, x.as_ptr(), t.as_ptr(), v.as_ptr(), 2), 0);

            let n = CString::new("n").unwrap();
            let f_abs = CString::new("absolute").unwrap();
            let p0 = CString::new("{}").unwrap();
            let id = ps_dag_add_node(dag, n.as_ptr(), f_abs.as_ptr(), p0.as_ptr(),
                x.as_ptr(), -1, err.as_mut_ptr(), err.len() as u32);
            assert!(id >= 0);
            assert_eq!(ps_dag_evaluate(dag, 1, err.as_mut_ptr(), err.len() as u32), 0);

            let mut ot = [0f64; 4];
            let mut ov = [0f64; 4];
            let mut on = 0u32;
            assert_eq!(ps_dag_output(dag, id as u32, ot.as_mut_ptr(), ov.as_mut_ptr(), 4, &mut on), 0);
            assert_eq!(&ov[..2], &[1.0, 2.0]);

            // 换成 scale(10)：输出变 [-10, 20]。
            let f_scale = CString::new("scale").unwrap();
            let p1 = CString::new("{\"value_scale\":10.0}").unwrap();
            assert_eq!(ps_dag_replace_node_op(dag, id, f_scale.as_ptr(), p1.as_ptr(),
                err.as_mut_ptr(), err.len() as u32), 0);
            assert_eq!(ps_dag_evaluate(dag, 2, err.as_mut_ptr(), err.len() as u32), 0);
            assert_eq!(ps_dag_output(dag, id as u32, ot.as_mut_ptr(), ov.as_mut_ptr(), 4, &mut on), 0);
            assert_eq!(&ov[..2], &[-10.0, 20.0]);

            // 换成非法 expression（缺 expr）→ 求值失败（3），算子恢复 scale。
            let f_bad = CString::new("expression").unwrap();
            let p2 = CString::new("{}").unwrap();
            assert_eq!(ps_dag_replace_node_op(dag, id, f_bad.as_ptr(), p2.as_ptr(),
                err.as_mut_ptr(), err.len() as u32), 0);
            assert_eq!(ps_dag_evaluate(dag, 3, err.as_mut_ptr(), err.len() as u32), 3);

            // 再求值成功，输出仍是 scale 的语义 → 证明旧算子已恢复。
            assert_eq!(ps_dag_evaluate(dag, 4, err.as_mut_ptr(), err.len() as u32), 0);
            assert_eq!(ps_dag_output(dag, id as u32, ot.as_mut_ptr(), ov.as_mut_ptr(), 4, &mut on), 0);
            assert_eq!(&ov[..2], &[-10.0, 20.0], "失败后旧算子与输出完好");

            // 越界节点 id 被拒绝。
            assert_eq!(ps_dag_replace_node_op(dag, 99, f_scale.as_ptr(), p1.as_ptr(),
                err.as_mut_ptr(), err.len() as u32), 2);

            ps_dag_free(dag);
        }
    }

    // BJ7 阶段一：MIMO 精确时间戳 join。
    #[test]
    fn dag_mimo_join_via_abi() {
        use std::ffi::CString;
        unsafe {
            let dag = ps_dag_create();
            assert!(!dag.is_null());
            let mut err = [0i8; 128];
            let a = CString::new("a").unwrap();
            let b = CString::new("b").unwrap();
            let ta = [0.0f64, 1.0, 2.0];
            let va = [10.0f64, 11.0, 12.0];
            let tb = [1.0f64, 2.0, 4.0];
            let vb = [21.0f64, 22.0, 24.0];
            assert_eq!(ps_dag_set_input(dag, a.as_ptr(), ta.as_ptr(), va.as_ptr(), 3), 0);
            assert_eq!(ps_dag_set_input(dag, b.as_ptr(), tb.as_ptr(), vb.as_ptr(), 3), 0);

            let mn = CString::new("m").unwrap();
            let mf = CString::new("join_equal").unwrap();
            let mp = CString::new("{}").unwrap();
            let chans = [a.as_ptr(), b.as_ptr()];
            let ids = [-1i32, -1];
            let m = ps_dag_add_node_multi(
                dag,
                mn.as_ptr(),
                mf.as_ptr(),
                mp.as_ptr(),
                chans.as_ptr(),
                ids.as_ptr(),
                2,
                err.as_mut_ptr(),
                err.len() as u32,
            );
            assert!(m >= 0, "add mimo failed: {}", cstr(err.as_ptr()));

            assert_eq!(ps_dag_evaluate(dag, 1, err.as_mut_ptr(), err.len() as u32), 0);
            let mut ot = [0f64; 8];
            let mut ov = [0f64; 8];
            let mut on = 0u32;
            assert_eq!(
                ps_dag_output(dag, m as u32, ot.as_mut_ptr(), ov.as_mut_ptr(), 8, &mut on),
                0
            );
            assert_eq!(on, 2, "交集只有 1.0 与 2.0");
            assert_eq!(&ot[..2], &[1.0, 2.0]);
            assert_eq!(&ov[..2], &[11.0, 12.0]);

            // 少于 2 路被拒绝。
            assert_eq!(
                ps_dag_add_node_multi(
                    dag, mn.as_ptr(), mf.as_ptr(), mp.as_ptr(),
                    chans.as_ptr(), ids.as_ptr(), 1,
                    err.as_mut_ptr(), err.len() as u32
                ),
                -1
            );
            // 空句柄被拒绝。
            assert_eq!(
                ps_dag_add_node_multi(
                    std::ptr::null_mut(), mn.as_ptr(), mf.as_ptr(), mp.as_ptr(),
                    chans.as_ptr(), ids.as_ptr(), 2,
                    err.as_mut_ptr(), err.len() as u32
                ),
                -1
            );

            ps_dag_free(dag);
        }
    }

    // BJ6：gap 上报与只读导出（段 id 水位，不看时间戳）。
    #[test]
    fn dag_gap_report_and_read_via_abi() {
        use std::ffi::CString;
        unsafe {
            let dag = ps_dag_create();
            assert!(!dag.is_null());
            let ch = CString::new("tf").unwrap();

            // 首报 [1,3]，水位 0 → 记录。
            assert_eq!(ps_dag_report_gap(dag, ch.as_ptr(), 1, 3, 0.5, 3.5), 0);
            assert_eq!(ps_dag_gap_count(dag), 1);
            assert_eq!(ps_dag_gap_total(dag), 1);

            // 重复上报同一区间：已在水位内，不再记录。
            assert_eq!(ps_dag_report_gap(dag, ch.as_ptr(), 1, 3, 0.5, 3.5), 0);
            assert_eq!(ps_dag_gap_total(dag), 1);

            // 后续区间 [4,5]：from=4 > 水位 3 → 记录。
            assert_eq!(ps_dag_report_gap(dag, ch.as_ptr(), 4, 5, 4.0, 5.0), 0);
            assert_eq!(ps_dag_gap_count(dag), 2);

            // 反向区间非法。
            assert_eq!(ps_dag_report_gap(dag, ch.as_ptr(), 9, 5, 0.0, 0.0), 1);
            assert_eq!(ps_dag_report_gap(dag, std::ptr::null(), 1, 2, 0.0, 0.0), 1);

            // 读回第 0 条：通道名 / 段 id 区间 / 时间提示。
            let mut buf = [0i8; 16];
            let (mut f, mut t2, mut lo, mut hi) = (0u64, 0u64, 0f64, 0f64);
            assert_eq!(
                ps_dag_gap_at(dag, 0, buf.as_mut_ptr(), buf.len() as u32, &mut f, &mut t2, &mut lo, &mut hi),
                0
            );
            assert_eq!(cstr(buf.as_ptr()), "tf");
            assert_eq!((f, t2), (1, 3));
            assert_eq!((lo, hi), (0.5, 3.5));

            // 越界读失败。
            assert_eq!(
                ps_dag_gap_at(dag, 9, buf.as_mut_ptr(), buf.len() as u32, &mut f, &mut t2, &mut lo, &mut hi),
                1
            );

            // 通道名缓冲不足：安全截断且仍 NUL 结尾。
            let mut small = [0x7fi8; 3];
            assert_eq!(
                ps_dag_gap_at(dag, 0, small.as_mut_ptr(), 3, std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut()),
                0
            );
            assert_eq!(cstr(small.as_ptr()), "tf");

            // 容量压到 1：只留最近 1 条，累计数不变。
            ps_dag_set_gap_capacity(dag, 1);
            assert_eq!(ps_dag_gap_count(dag), 1);
            assert_eq!(ps_dag_gap_total(dag), 2);
            assert_eq!(
                ps_dag_gap_at(dag, 0, std::ptr::null_mut(), 0, &mut f, &mut t2, &mut lo, &mut hi),
                0
            );
            assert_eq!((f, t2), (4, 5), "保留的是最近的缺口");

            ps_dag_free(dag);
        }
    }

    fn cstr(p: *const c_char) -> String {
        if p.is_null() { return String::new(); }
        unsafe { CStr::from_ptr(p).to_string_lossy().into_owned() }
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
    let tname = CStr::from_ptr(table).to_string_lossy();
    let cname = CStr::from_ptr(column).to_string_lossy();
    let ds = (*ds).read_table(tname.as_ref());
    let Some(t) = ds.typed.tables().iter().find(|t| t.name == tname) else { return 1; };
    let Some(c) = t.columns.iter().find(|c| c.name == cname) else { return 1; };
    let idx = row as usize;
    if idx >= t.row_count() || idx >= c.len() {
        return 1;
    }
    if !out_time.is_null() {
        *out_time = t.time_ns[idx] as f64 * 1e-9;
    }
    if !out_value.is_null() {
        *out_value = c.value_as_f64(idx).unwrap_or(f64::NAN);
    }
    if c.is_null(idx) { 2 } else { 0 }
}

/// typed 表按行取**文本值**（params/facts 这类 key-value 表的唯一出口）。
///
/// 语义：写入调用方缓冲（NUL 结尾，超出 cap-1 字节截断），`out_len` 回填实际字节数
/// （不含 NUL，便于调用方扩容重试）。返回 0 成功、1 表/列/行不存在、2 非文本列或 null。
/// 数值列同样可用（走 `value_text` 的 to_string），但参数表只走文本列。
///
/// 为什么必须有这条：`ps_dataset_value_at` 只吐 f64，Text 列里的字符串此前在 C++ 侧
/// 完全读不到——参数面板只能显示"3 项"而看不到键值。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_text_at(
    ds: *mut PsDataset,
    table: *const c_char,
    column: *const c_char,
    row: u64,
    out: *mut c_char,
    cap: u32,
    out_len: *mut u32,
) -> i32 {
    if ds.is_null() || table.is_null() || column.is_null() {
        return 1;
    }
    let tname = CStr::from_ptr(table).to_string_lossy();
    let cname = CStr::from_ptr(column).to_string_lossy();
    let ds = (*ds).read_table(tname.as_ref());
    let Some(t) = ds.typed.tables().iter().find(|t| t.name == tname) else { return 1; };
    let Some(c) = t.columns.iter().find(|c| c.name == cname) else { return 1; };
    let idx = row as usize;
    if idx >= t.row_count() || idx >= c.len() {
        return 1;
    }
    if c.value_type() != ps_ndf::typed::ValueType::Text {
        return 2;
    }
    if c.is_null(idx) {
        return 2;
    }
    let text = c.value_text(idx).unwrap_or_default();
    let bytes = text.as_bytes();
    if !out_len.is_null() {
        *out_len = bytes.len() as u32;
    }
    if !out.is_null() && cap > 0 {
        let n = bytes.len().min(cap as usize - 1);
        let dst = std::slice::from_raw_parts_mut(out as *mut u8, cap as usize);
        dst[..n].copy_from_slice(&bytes[..n]);
        dst[n] = 0;
    }
    0
}

/// typed 表按时间取最近行；t_ns 为纳秒。返回 0 成功，1 空表/找不到。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_value_near(
    ds: *mut PsDataset,
    table: *const c_char,
    column: *const c_char,
    t_ns: i64,
    out_time: *mut f64,
    out_value: *mut f64,
) -> i32 {
    if ds.is_null() || table.is_null() || column.is_null() {
        return 1;
    }
    let tname = CStr::from_ptr(table).to_string_lossy();
    let cname = CStr::from_ptr(column).to_string_lossy();
    let ds = (*ds).read_table(tname.as_ref());
    let Some(t) = ds.typed.tables().iter().find(|t| t.name == tname) else { return 1; };
    let Some(c) = t.columns.iter().find(|c| c.name == cname) else { return 1; };
    if t.time_ns.is_empty() {
        return 1;
    }
    let idx = match t.time_ns.binary_search(&t_ns) {
        Ok(i) => i,
        Err(0) => 0,
        Err(i) if i >= t.time_ns.len() => t.time_ns.len() - 1,
        Err(i) => {
            if (t.time_ns[i] - t_ns).abs() < (t.time_ns[i - 1] - t_ns).abs() { i } else { i - 1 }
        }
    };
    if !out_time.is_null() {
        *out_time = t.time_ns[idx] as f64 * 1e-9;
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
    t0_ns: i64,
    t1_ns: i64,
    out_time: *mut f64,
    out_value: *mut f64,
    cap: u32,
) -> u32 {
    if ds.is_null() || table.is_null() || column.is_null() || out_time.is_null() || out_value.is_null() || cap == 0 {
        return 0;
    }
    let tname = CStr::from_ptr(table).to_string_lossy();
    let cname = CStr::from_ptr(column).to_string_lossy();
    let ds = (*ds).read_table(tname.as_ref());
    let Some(t) = ds.typed.tables().iter().find(|t| t.name == tname) else { return 0; };
    let Some(c) = t.columns.iter().find(|c| c.name == cname) else { return 0; };
    let lo = t.time_ns.partition_point(|&x| x < t0_ns);
    let hi = t.time_ns.partition_point(|&x| x <= t1_ns);
    if hi <= lo {
        return 0;
    }
    let rows = hi - lo;
    let step = (rows as u64).div_ceil(cap as u64).max(1) as usize;
    let mut n = 0u32;
    let mut i = lo;
    while i < hi && n < cap {
        *out_time.add(n as usize) = t.time_ns[i] as f64 * 1e-9;
        *out_value.add(n as usize) = c.value_as_f64(i).unwrap_or(f64::NAN);
        n += 1;
        i += step;
    }
    n
}

/// 表内 seq；不存在返回 UINT64_MAX。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_seq_at(ds: *mut PsDataset, table: *const c_char, row: u32) -> u64 {
    if ds.is_null() || table.is_null() { return u64::MAX; }
    let name = CStr::from_ptr(table).to_string_lossy();
    let ds = (*ds).read_table(name.as_ref());
    let Some(t) = ds.typed.tables().iter().find(|t| t.name == name) else { return u64::MAX; };
    t.seq.get(row as usize).copied().unwrap_or(u64::MAX)
}

/// typed 列原始类型，返回 PsValueType；旧通道/不存在返回 -1。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_column_type(
    ds: *mut PsDataset,
    table: *const c_char,
    column: *const c_char,
) -> i32 {
    if ds.is_null() || table.is_null() || column.is_null() {
        return -1;
    }
    let tname = CStr::from_ptr(table).to_string_lossy();
    let cname = CStr::from_ptr(column).to_string_lossy();
    let ds = (*ds).read_table(tname.as_ref());
    let Some(t) = ds.typed.tables().iter().find(|t| t.name == tname) else { return -1; };
    let Some(c) = t.columns.iter().find(|c| c.name == cname) else { return -1; };
    // 压缩列对外仍暴露逻辑类型码（解码后类型不变），不新增公共编码类型码。
    match c.data.value_type() {
        ps_ndf::typed::ValueType::F64 => 0,
        ps_ndf::typed::ValueType::F32 => 1,
        ps_ndf::typed::ValueType::I64 => 2,
        ps_ndf::typed::ValueType::U64 => 3,
        ps_ndf::typed::ValueType::I32 => 4,
        ps_ndf::typed::ValueType::U32 => 5,
        ps_ndf::typed::ValueType::I16 => 6,
        ps_ndf::typed::ValueType::U16 => 7,
        ps_ndf::typed::ValueType::I8 => 8,
        ps_ndf::typed::ValueType::U8 => 9,
        ps_ndf::typed::ValueType::Bool => 10,
        ps_ndf::typed::ValueType::Text => 11,
        ps_ndf::typed::ValueType::Object => 12,
    }
}

/// 四元数 w/x/y/z → roll/pitch/yaw，**数组级**（不写回 dataset）。
///
/// 与 `ps_dataset_apply_quat_rpy`（写回式）并存：NG 的派生语义是"独立派生
/// 字段"——派生结果登记在宿主层派生注册表，原始 dataset 零改动，因此这里
/// 只做纯数值转换，输出写进调用方缓冲区（各 cap = n）。
#[no_mangle]
pub unsafe extern "C" fn ps_quat_to_rpy(
    n: u32,
    w: *const f64,
    x: *const f64,
    y: *const f64,
    z: *const f64,
    degrees: u8,
    unwrap: u8,
    out_roll: *mut f64,
    out_pitch: *mut f64,
    out_yaw: *mut f64,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if n == 0 || w.is_null() || x.is_null() || y.is_null() || z.is_null()
        || out_roll.is_null() || out_pitch.is_null() || out_yaw.is_null()
    {
        return 1;
    }
    let wv = std::slice::from_raw_parts(w, n as usize);
    let xv = std::slice::from_raw_parts(x, n as usize);
    let yv = std::slice::from_raw_parts(y, n as usize);
    let zv = std::slice::from_raw_parts(z, n as usize);
    let (r, p, yaw) = match ps_ops::quat::to_rpy(wv, xv, yv, zv, degrees != 0, unwrap != 0) {
        Ok(v) => v,
        Err(e) => { write_err(err, err_cap, &e); return 5; }
    };
    for i in 0..(n as usize) {
        *out_roll.add(i) = r[i];
        *out_pitch.add(i) = p[i];
        *out_yaw.add(i) = yaw[i];
    }
    0
}

/* ===== M2.7 Rust kernel 优化与功能补全 ===== */

/// 基准钩子（不进 C ABI）：清掉指定 typed 列的 LOD 金字塔，
/// 用于 bench 对比"旧行为=每帧全量重扫"。
///
/// # Safety
///
/// `ds/table/column` 必须指向有效的 PsDataset 与 NUL 结尾的 UTF-8 字符串。
#[doc(hidden)]
pub unsafe fn __m27_invalidate_lod_for_bench(
    ds: *mut PsDataset,
    table: *const c_char,
    column: *const c_char,
) {
    if ds.is_null() || table.is_null() || column.is_null() {
        return;
    }
    let Ok(tname) = CStr::from_ptr(table).to_str() else { return };
    let Ok(cname) = CStr::from_ptr(column).to_str() else { return };
    if let Some(ti) = (*ds).typed.table_index(tname) {
        if let Some(t) = (*ds).typed.table_mut(ti) {
            if let Some(ci) = t.column_index(cname) {
                t.columns[ci].lod = ps_ndf::typed::LodPyramid::default();
            }
        }
    }
}


/// 2.7.1 整列原始读取：借出 typed 列的内部连续缓冲（零拷贝）。
///
/// 返回 0 成功：`out_values` 指向内核内部缓冲（按 `out_type` 解释，元素数 `out_count`），
/// `out_nulls` 指向 null 位图（`(count+7)/8` 字节，1=null；全有效时也可能非空位图，调用方判 0 位）。
/// 指针只在数据集**只读期间**有效：任何写操作（append/remove/free）后立即失效。
/// 返回 1 参数错误；3 该列不支持直读（Text 变长）；4 表/列不存在。
///
/// # Safety
///
/// `ds` 必须为有效的 PsDataset 指针；`table/column` 必须为 NUL 结尾 UTF-8 字符串；
/// `out_*` 必须为可写指针，且在数据集只读期间解引用借出的指针。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_column_raw(
    ds: *mut PsDataset,
    table: *const c_char,
    column: *const c_char,
    out_values: *mut *const std::ffi::c_void,
    out_count: *mut u64,
    out_type: *mut u32,
    out_nulls: *mut *const u8,
) -> i32 {
    if ds.is_null() || table.is_null() || column.is_null()
        || out_values.is_null() || out_count.is_null() || out_type.is_null()
    {
        return 1;
    }
    let Ok(tname) = CStr::from_ptr(table).to_str() else { return 1 };
    let Ok(cname) = CStr::from_ptr(column).to_str() else { return 1 };
    // 归并会整体替换列的 Vec，因此借出的裸指针在物化后的只读期间有效；
    // 任何后续 append/backfill/加列都会使其失效（头文件已声明）。
    let d = (*ds).read_table(tname);
    let Some(t) = d.typed.tables().iter().find(|t| t.name == tname) else { return 4 };
    let Some(c) = t.columns.iter().find(|c| c.name == cname) else { return 4 };
    let (ptr, ty): (*const std::ffi::c_void, u32) = match &c.data {
        ps_ndf::typed::ColumnData::F64(v) => (v.as_ptr() as *const std::ffi::c_void, 0),
        ps_ndf::typed::ColumnData::F32(v) => (v.as_ptr() as *const std::ffi::c_void, 1),
        ps_ndf::typed::ColumnData::I64(v) => (v.as_ptr() as *const std::ffi::c_void, 2),
        ps_ndf::typed::ColumnData::U64(v) => (v.as_ptr() as *const std::ffi::c_void, 3),
        ps_ndf::typed::ColumnData::I32(v) => (v.as_ptr() as *const std::ffi::c_void, 4),
        ps_ndf::typed::ColumnData::U32(v) => (v.as_ptr() as *const std::ffi::c_void, 5),
        ps_ndf::typed::ColumnData::I16(v) => (v.as_ptr() as *const std::ffi::c_void, 6),
        ps_ndf::typed::ColumnData::U16(v) => (v.as_ptr() as *const std::ffi::c_void, 7),
        ps_ndf::typed::ColumnData::I8(v) => (v.as_ptr() as *const std::ffi::c_void, 8),
        ps_ndf::typed::ColumnData::U8(v) => (v.as_ptr() as *const std::ffi::c_void, 9),
        // Vec<bool> 每元素 1 字节（0/1），可按 U8 布局借出。
        ps_ndf::typed::ColumnData::Bool(v) => (v.as_ptr() as *const std::ffi::c_void, 10),
        ps_ndf::typed::ColumnData::Text(_) => return 3,
        // 对象列不支持整列裸指针借出（句柄语义 + 悬垂防护），走 blob_get 逐句柄取。
        ps_ndf::typed::ColumnData::Object(_) => return 3,
        // 压缩列无连续原生数组，不借出裸指针；走 ps_dataset_column_decode_f64。
        ps_ndf::typed::ColumnData::Encoded { .. } => return 3,
    };
    *out_values = ptr;
    *out_count = c.len() as u64;
    *out_type = ty;
    if !out_nulls.is_null() {
        *out_nulls = if c.nulls.has_any_null() { c.nulls.raw().as_ptr() } else { std::ptr::null() };
    }
    0
}

/* ===== S5 选择性列编码压缩 =====
 * 编码按列显式开启，调用方决定哪些列压缩；F64 绘图主通道默认不动。
 * 压缩列不借出裸指针（columnRaw 返回 3），经 decode_f64 取解码后的值。 */

fn map_codec(raw: u32) -> Option<ps_ndf::typed::Codec> {
    use ps_ndf::typed::Codec::*;
    Some(match raw { 0 => Constant, 1 => For, 2 => Dictionary, 3 => BitPack, _ => return None })
}

/// 对指定列开启压缩编码。codec: 0=Constant,1=For,2=Dictionary,3=BitPack。
/// 返回 0 成功；1 参数错误；2 表/列不存在；3 已编码；4 该列/数据不适合此 codec。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_column_encode(
    ds: *mut PsDataset,
    table: *const c_char,
    column: *const c_char,
    codec: u32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if ds.is_null() || table.is_null() || column.is_null() {
        return 1;
    }
    let Some(cd) = map_codec(codec) else { return 1; };
    let Ok(tname) = CStr::from_ptr(table).to_str() else { return 1; };
    let Ok(cname) = CStr::from_ptr(column).to_str() else { return 1; };
    // 编码只覆盖主表列；若段里还有未归并行，会留下「已编码列 + 未编码段」的混态。
    let d = (*ds).read_table(tname);
    let Some(ti) = d.typed.table_index(tname) else { return 2; };
    let Some(ci) = d.typed.tables()[ti].columns.iter().position(|c| c.name == cname) else { return 2; };
    if d.typed.tables()[ti].columns[ci].data.codec().is_some() {
        write_err(err, err_cap, "列已编码");
        return 3;
    }
    let d = &mut *ds;
    let Some(t) = d.typed.table_mut(ti) else { return 2 };
    let Some(col) = t.columns.get_mut(ci) else { return 2 };
    let encoded = match col.data.encode(cd) {
        Ok(x) => x,
        Err(e) => { write_err(err, err_cap, &e); return 4; }
    };
    col.data = encoded;
    d.infos_dirty = true;
    0
}

/// 查询列是否压缩编码：0=原生，1=已编码，-1=表/列不存在。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_column_is_encoded(
    ds: *mut PsDataset,
    table: *const c_char,
    column: *const c_char,
) -> i32 {
    if ds.is_null() || table.is_null() || column.is_null() { return -1; }
    let Ok(tname) = CStr::from_ptr(table).to_str() else { return -1; };
    let Ok(cname) = CStr::from_ptr(column).to_str() else { return -1; };
    let d = (*ds).read_table(tname);
    let Some(t) = d.typed.tables().iter().find(|t| t.name == tname) else { return -1; };
    let Some(c) = t.columns.iter().find(|c| c.name == cname) else { return -1; };
    if c.data.codec().is_some() { 1 } else { 0 }
}

/// 取列解码后的数值（null 行写 NaN）。cap 为 out 容量；返回写入行数（≤cap），
/// 表/列不存在或参数错误返回 0。供 UI/脚本读取压缩列，不经裸指针借出。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_column_decode_f64(
    ds: *mut PsDataset,
    table: *const c_char,
    column: *const c_char,
    out: *mut f64,
    cap: u32,
) -> u32 {
    if ds.is_null() || table.is_null() || column.is_null() || out.is_null() { return 0; }
    let Ok(tname) = CStr::from_ptr(table).to_str() else { return 0; };
    let Ok(cname) = CStr::from_ptr(column).to_str() else { return 0; };
    let d = (*ds).read_table(tname);
    let Some(t) = d.typed.tables().iter().find(|t| t.name == tname) else { return 0; };
    let Some(c) = t.columns.iter().find(|c| c.name == cname) else { return 0; };
    let n = c.len().min(cap as usize);
    for r in 0..n {
        *out.add(r) = if c.is_null(r) { f64::NAN } else { c.data.value_as_f64(r).unwrap_or(f64::NAN) };
    }
    n as u32
}

/* ===== S4 对象列：不透明句柄 + 引用计数（RFC D4） =====
 * payload 存内核 ObjectStore，跨 C ABI 只传 u64 句柄，不传裸指针。
 * 句柄 = slot 索引 + 1，slot 只增不复用，故不会因池 realloc/reuse 而悬垂或串位。
 * 句柄生命周期绑定数据集：数据集 free 后所有句柄失效，调用方不得跨数据集持有。 */

/// 写入一段 payload 到对象池，返回稳定句柄（>0）；失败返回 0 并写 err。
/// `type_tag` 由调用方约定（如 1=点云、2=图像、3=TF），内核只透传不解释。
/// 数据集自身持有第 1 个引用；调用方需要独立生命周期时用 blob_retain。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_blob_put(
    ds: *mut PsDataset,
    payload: *const std::ffi::c_void,
    len: u32,
    type_tag: u32,
    err: *mut c_char,
    err_cap: u32,
) -> u64 {
    if ds.is_null() {
        write_err(err, err_cap, "数据集为空");
        return 0;
    }
    if len > 0 && payload.is_null() {
        write_err(err, err_cap, "payload 为空");
        return 0;
    }
    let bytes: &[u8] = if len == 0 {
        &[]
    } else {
        std::slice::from_raw_parts(payload as *const u8, len as usize)
    };
    (*ds).typed.objects_mut().put(bytes, type_tag)
}

/// 按句柄读 payload：字节级拷贝到 out。返回 0 成功；1 参数错；
/// 2 句柄无效/已释放（悬垂被拦截）；3 out_cap 不足（*out_len 填所需字节数）。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_blob_get(
    ds: *const PsDataset,
    handle: u64,
    out: *mut std::ffi::c_void,
    out_cap: u32,
    out_len: *mut u32,
    out_type_tag: *mut u32,
) -> i32 {
    if ds.is_null() || out_len.is_null() {
        return 1;
    }
    let Some((bytes, tag)) = (*ds).typed.objects().get(handle) else {
        return 2;
    };
    if !out_type_tag.is_null() {
        *out_type_tag = tag;
    }
    if bytes.len() > out_cap as usize {
        *out_len = bytes.len() as u32;
        return if out.is_null() { 1 } else { 3 };
    }
    if !bytes.is_empty() {
        if out.is_null() {
            return 1;
        }
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), out as *mut u8, bytes.len());
    }
    *out_len = bytes.len() as u32;
    0
}

/// 增加一个外部引用。返回 0 成功；2 句柄无效/已释放。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_blob_retain(ds: *mut PsDataset, handle: u64) -> i32 {
    if ds.is_null() {
        return 1;
    }
    if (*ds).typed.objects_mut().retain(handle) { 0 } else { 2 }
}

/// 释放一个引用，归零则失效。返回 0 成功；2 句柄无效/引用已尽。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_blob_release(ds: *mut PsDataset, handle: u64) -> i32 {
    if ds.is_null() {
        return 1;
    }
    if (*ds).typed.objects_mut().release(handle) { 0 } else { 2 }
}

/* ===== BJ1 时间索引对象：按 topic / 时间组织句柄，到达序 UID（借鉴 pj ObjectStore） =====
 * 与上面的 blob_* 正交：blob_* 管 payload 字节与句柄生命周期，这里只管
 * "(topic, time_ns) -> handle" 的有序映射与到达序。handle 必须先由 blob_put 取得。
 * 解决 pj 踩过的两个坑：乱序到达不丢行、乱序插入 O(log n) 而非 O(n²)。 */

/// 把已有对象句柄登记到某 topic 的时间点，返回到达序 UID（>0）；失败返回 0 并写 err。
/// `handle` 必须是由 `ps_dataset_blob_put` 取得的有效句柄（内核不在此校验其存活）。
/// `time_ns` 为时间戳（纳秒，u64 编码）；相等时间按到达序（UID）区分。
#[no_mangle]
pub unsafe extern "C" fn ps_object_insert(
    ds: *mut PsDataset,
    topic: *const c_char,
    time_ns: u64,
    handle: u64,
    err: *mut c_char,
    err_cap: u32,
) -> u64 {
    if ds.is_null() || topic.is_null() || handle == 0 {
        write_err(err, err_cap, "参数为空或句柄为 0");
        return 0;
    }
    let name = match std::ffi::CStr::from_ptr(topic).to_str() {
        Ok(s) => s,
        Err(_) => {
            write_err(err, err_cap, "topic 名非法 UTF-8");
            return 0;
        }
    };
    // 判断是否为时间序追加（>= 当前最大时间）；据此更新内容身份与 warm cache。
    // 空 topic 的首条也视为 append（Followable）。
    let is_append = (*ds)
        .object_reads
        .topic(name)
        .and_then(|t| t.max_time_ns())
        .map_or(true, |tmax| time_ns >= tmax);
    let uid = (*ds).object_reads.topic_mut(name).insert(time_ns, handle);
    (*ds).object_reads.on_insert(name, time_ns, is_append);
    uid
}

/// 时间点查找：`<= time_ns` 的最新一条，输出 `(handle, uid)`。
/// 返回 0 成功；1 参数错；2 topic 不存在或无匹配条目。
#[no_mangle]
pub unsafe extern "C" fn ps_object_latest_at(
    ds: *const PsDataset,
    topic: *const c_char,
    time_ns: u64,
    out_handle: *mut u64,
    out_uid: *mut u64,
) -> i32 {
    if ds.is_null() || topic.is_null() || out_handle.is_null() || out_uid.is_null() {
        return 1;
    }
    let Ok(name) = std::ffi::CStr::from_ptr(topic).to_str() else { return 1; };
    let Some(t) = (*ds).object_reads.topic(name) else { return 2; };
    let Some((h, uid)) = t.latest_at(time_ns) else { return 2; };
    *out_handle = h;
    *out_uid = uid;
    0
}

/// 到达序游标：取 `uid > since` 的条目，按到达序升序，写入 `out` 三元组 `(uid, time_ns, handle)`。
/// 每条占 3 个 u64；`out_cap` 为元素容量（不是 u64 个数）。
/// 返回 0 成功；1 参数错；2 缓冲不足（*out_len 填所需元素数）；3 topic 不存在。
#[no_mangle]
pub unsafe extern "C" fn ps_object_drain_new_since(
    ds: *const PsDataset,
    topic: *const c_char,
    since: u64,
    out: *mut u64,
    out_cap: u32,
    out_len: *mut u32,
) -> i32 {
    if ds.is_null() || topic.is_null() || out_len.is_null() {
        return 1;
    }
    let Ok(name) = std::ffi::CStr::from_ptr(topic).to_str() else { return 1; };
    let Some(t) = (*ds).object_reads.topic(name) else { return 3; };
    let rows = t.drain_new_since(since);
    if rows.len() > out_cap as usize {
        *out_len = rows.len() as u32;
        return if out.is_null() { 1 } else { 2 };
    }
    if !out.is_null() {
        for (r, (uid, t_ns, h)) in rows.iter().enumerate() {
            *out.add(r * 3) = *uid;
            *out.add(r * 3 + 1) = *t_ns;
            *out.add(r * 3 + 2) = *h;
        }
    }
    *out_len = rows.len() as u32;
    0
}

/// 时间窗遍历：闭区间 `[t0_ns, t1_ns]`，按时间升序，输出同上三元组。
/// 返回 0 成功；1 参数错；2 缓冲不足（*out_len 填所需元素数）；3 topic 不存在。
#[no_mangle]
pub unsafe extern "C" fn ps_object_range_by_time(
    ds: *const PsDataset,
    topic: *const c_char,
    t0_ns: u64,
    t1_ns: u64,
    out: *mut u64,
    out_cap: u32,
    out_len: *mut u32,
) -> i32 {
    if ds.is_null() || topic.is_null() || out_len.is_null() {
        return 1;
    }
    let Ok(name) = std::ffi::CStr::from_ptr(topic).to_str() else { return 1; };
    let Some(t) = (*ds).object_reads.topic(name) else { return 3; };
    let rows = t.range_by_time(t0_ns, t1_ns);
    if rows.len() > out_cap as usize {
        *out_len = rows.len() as u32;
        return if out.is_null() { 1 } else { 2 };
    }
    if !out.is_null() {
        for (r, (t_ns, uid, h)) in rows.iter().enumerate() {
            *out.add(r * 3) = *uid;
            *out.add(r * 3 + 1) = *t_ns;
            *out.add(r * 3 + 2) = *h;
        }
    }
    *out_len = rows.len() as u32;
    0
}

/// topic 数量 / 条目总数，供上层做容量预分配与观测。
#[no_mangle]
pub unsafe extern "C" fn ps_object_index_stats(
    ds: *const PsDataset,
    out_topic_count: *mut u32,
    out_entry_count: *mut u32,
) -> i32 {
    if ds.is_null() || out_topic_count.is_null() || out_entry_count.is_null() {
        return 1;
    }
    *out_topic_count = (*ds).object_reads.topic_count() as u32;
    *out_entry_count = (*ds).object_reads.total_entries() as u32;
    0
}

/* ===== BJ2 保留预算与跳跃驱逐（借鉴 pj RetentionBudget / RetentionJump） ===== */

/// 设置 topic 的保留预算。time_window_ns / max_entries 各自独立，0=关闭该维。
/// 设置后不立即驱逐，下次 `ps_object_apply_retention` 生效。返回 0 成功；1 参数错。
#[no_mangle]
pub unsafe extern "C" fn ps_object_set_retention(
    ds: *mut PsDataset,
    topic: *const c_char,
    time_window_ns: u64,
    max_entries: u32,
) -> i32 {
    if ds.is_null() || topic.is_null() {
        return 1;
    }
    let Ok(name) = std::ffi::CStr::from_ptr(topic).to_str() else { return 1; };
    let budget = ps_ndf::object_index::RetentionBudget::new(time_window_ns, max_entries as usize);
    (*ds).object_reads.topic_mut(name).set_budget(budget);
    0
}

/// 对 topic 应用保留预算，跳跃驱逐过期前缀，输出被逐 handle 数组（调用方须逐一 blob_release 释放引用）。
/// `out` 每元素一个 u64 handle；`out_cap` 为元素数。返回 0 成功；1 参数错；
/// 2 缓冲不足（*out_len 填所需）；3 topic 不存在。
#[no_mangle]
pub unsafe extern "C" fn ps_object_apply_retention(
    ds: *mut PsDataset,
    topic: *const c_char,
    out: *mut u64,
    out_cap: u32,
    out_len: *mut u32,
) -> i32 {
    if ds.is_null() || topic.is_null() || out_len.is_null() {
        return 1;
    }
    let Ok(name) = std::ffi::CStr::from_ptr(topic).to_str() else { return 1; };
    let Some(t) = (*ds).object_reads.topic(name) else { return 3; };
    // 先预览：缓冲不足则不驱逐，避免 handle 交不出去造成 blob 引用泄漏。
    let will_evict = t.retention_evictable_count();
    if will_evict > out_cap as usize {
        *out_len = will_evict as u32;
        return if out.is_null() { 1 } else { 2 };
    }
    let handles = (*ds).object_reads.topic_mut(name).apply_retention();
    if !handles.is_empty() {
        (*ds).object_reads.on_evict(name);
    }
    if !out.is_null() {
        for (r, h) in handles.iter().enumerate() {
            *out.add(r) = *h;
        }
    }
    *out_len = handles.len() as u32;
    0
}

/// topic 的驱逐水位（时间戳 < 此值已被回收），供读侧缓存失效判断。
/// 返回 0 成功；1 参数错；3 topic 不存在。
#[no_mangle]
pub unsafe extern "C" fn ps_object_evicted_before(
    ds: *const PsDataset,
    topic: *const c_char,
    out_ns: *mut u64,
) -> i32 {
    if ds.is_null() || topic.is_null() || out_ns.is_null() {
        return 1;
    }
    let Ok(name) = std::ffi::CStr::from_ptr(topic).to_str() else { return 1; };
    let Some(t) = (*ds).object_reads.topic(name) else { return 3; };
    *out_ns = t.evicted_before_ns();
    0
}

/* ===== BJ3 读语义：零拷贝视图 + warm cache + 高水位/迟到/内容身份 + 分池 =====
 * 边界：pj 的 pushLazy(fetch: LazyCallback) 惰性解压回调不跨 C ABI 传闭包；
 * 解压由上层（MCAP parser）在读取时驱动，本层提供零拷贝视图 + 缓存 + 语义原语。 */

/// 零拷贝 payload 视图：返回内核内字节指针与长度（不复制到 out）。
/// 指针生命周期绑定 dataset 且 handle 仍存活期间；blob_release/dataset_free 后失效，
/// 调用方不得跨这些操作持有。返回 0 成功；1 参数错；2 句柄无效/已释放。
#[no_mangle]
pub unsafe extern "C" fn ps_object_payload_view(
    ds: *const PsDataset,
    handle: u64,
    out_ptr: *mut *const u8,
    out_len: *mut u32,
    out_type_tag: *mut u32,
) -> i32 {
    if ds.is_null() || out_ptr.is_null() || out_len.is_null() {
        return 1;
    }
    let Some((bytes, tag)) = (*ds).typed.objects().get(handle) else {
        return 2;
    };
    *out_ptr = bytes.as_ptr();
    *out_len = bytes.len() as u32;
    if !out_type_tag.is_null() {
        *out_type_tag = tag;
    }
    0
}

/// warm latestAt：同 UID 重复请求命中缓存，不重复解析。输出 `(handle, uid, hit)`。
/// 返回 0 成功/命中或未命中；1 参数错；2 topic 不存在或无匹配。
#[no_mangle]
pub unsafe extern "C" fn ps_object_warm_latest_at(
    ds: *mut PsDataset,
    topic: *const c_char,
    time_ns: u64,
    out_handle: *mut u64,
    out_uid: *mut u64,
    out_hit: *mut u32,
) -> i32 {
    if ds.is_null() || topic.is_null() || out_handle.is_null() || out_uid.is_null() || out_hit.is_null() {
        return 1;
    }
    let Ok(name) = std::ffi::CStr::from_ptr(topic).to_str() else { return 1; };
    let Some((h, uid, hit)) = (*ds).object_reads.warm_latest_at(name, time_ns) else { return 2; };
    *out_handle = h;
    *out_uid = uid;
    *out_hit = hit as u32;
    0
}

/// 时间窗高水位：`time <= t` 的最大 UID（区别于 latestAt 的 UID，避免迟到重检）。
/// 返回 0 成功；1 参数错；2 topic 不存在或无匹配。
#[no_mangle]
pub unsafe extern "C" fn ps_object_max_uid_at_or_before(
    ds: *const PsDataset,
    topic: *const c_char,
    time_ns: u64,
    out_uid: *mut u64,
) -> i32 {
    if ds.is_null() || topic.is_null() || out_uid.is_null() {
        return 1;
    }
    let Ok(name) = std::ffi::CStr::from_ptr(topic).to_str() else { return 1; };
    let Some(t) = (*ds).object_reads.topic(name) else { return 2; };
    let Some(uid) = t.max_uid_at_or_before(time_ns) else { return 2; };
    *out_uid = uid;
    0
}

/// 迟到检测：`uid > after` 的最早时间戳；无则 *out_found=0。
/// 返回 0 成功；1 参数错；2 topic 不存在。
#[no_mangle]
pub unsafe extern "C" fn ps_object_oldest_arrival_after(
    ds: *const PsDataset,
    topic: *const c_char,
    after: u64,
    out_ts: *mut u64,
    out_found: *mut u32,
) -> i32 {
    if ds.is_null() || topic.is_null() || out_ts.is_null() || out_found.is_null() {
        return 1;
    }
    let Ok(name) = std::ffi::CStr::from_ptr(topic).to_str() else { return 1; };
    let Some(t) = (*ds).object_reads.topic(name) else { return 2; };
    match t.oldest_arrival_after(after) {
        Some(ts) => { *out_ts = ts; *out_found = 1; }
        None => { *out_ts = 0; *out_found = 0; }
    }
    0
}

/// 内容身份：输出 `(content, rewrite, removal)` 三计数器。
/// 返回 0 成功；1 参数错；2 topic 不存在。
#[no_mangle]
pub unsafe extern "C" fn ps_object_content_identity(
    ds: *const PsDataset,
    topic: *const c_char,
    out: *mut u64,
) -> i32 {
    if ds.is_null() || topic.is_null() || out.is_null() {
        return 1;
    }
    let Ok(name) = std::ffi::CStr::from_ptr(topic).to_str() else { return 1; };
    let Some(id) = (*ds).object_reads.identity(name) else { return 2; };
    *out = id.content;
    *out.add(1) = id.rewrite;
    *out.add(2) = id.removal;
    0
}

/// 配置 topic 的 ResidentPool（按 payload 大小分 small/large，0 容量=禁用）。
/// `small_reserve_bytes` 为 small 类保留份额，防大 payload 挤爆小对象。
/// 返回 0 成功；1 参数错。
#[no_mangle]
pub unsafe extern "C" fn ps_object_set_pool(
    ds: *mut PsDataset,
    topic: *const c_char,
    capacity_bytes: u64,
    small_reserve_bytes: u64,
) -> i32 {
    if ds.is_null() || topic.is_null() {
        return 1;
    }
    let Ok(name) = std::ffi::CStr::from_ptr(topic).to_str() else { return 1; };
    (*ds).object_reads.set_pool(name, capacity_bytes, small_reserve_bytes);
    0
}

/// 2.7.5 数据集切片：从源数据集按时间范围 `[t0_ns, t1_ns]`（含端点）与列子集
/// 抽出一张新表，装进全新 PsDataset（独立生命周期，调用方 ps_dataset_free 释放）。
///
/// `columns` 为列名数组（NULL/0 取全部列）；输出数据集只含 typed 表（旧式 f64 通道不复制）。
/// 返回 0 成功；1 参数错误；2 名字非法；3 表/列不存在；4 空列名；5 内部错误（见 err）。
///
/// # Safety
///
/// `ds` 必须为有效的 PsDataset 指针；`table` 及 `columns` 数组内各指针必须为
/// NUL 结尾 UTF-8 字符串；`columns` 含 `column_count` 个合法指针；
/// `out`/`err` 必须为可写指针，`err` 缓冲容量 `err_cap` 字节。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_slice(
    ds: *mut PsDataset,
    table: *const c_char,
    columns: *const *const c_char,
    column_count: u32,
    t0_ns: i64,
    t1_ns: i64,
    out: *mut *mut PsDataset,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if ds.is_null() || table.is_null() || out.is_null() {
        write_err(err, err_cap, "参数为空");
        return 1;
    }
    *out = std::ptr::null_mut();
    let Ok(tname) = CStr::from_ptr(table).to_str() else {
        write_err(err, err_cap, "表名不是合法 UTF-8");
        return 2;
    };
    let d = (*ds).read_table(tname);
    let Some(t) = d.typed.tables().iter().find(|t| t.name == tname) else {
        write_err(err, err_cap, &format!("表不存在：{tname}"));
        return 3;
    };
    // 列名 → 列索引（保留给定顺序）。
    let col_indices: Option<Vec<usize>> = if column_count == 0 {
        None
    } else {
        if columns.is_null() {
            write_err(err, err_cap, "列名数组为空");
            return 1;
        }
        let mut idx = Vec::with_capacity(column_count as usize);
        for i in 0..column_count as usize {
            let p = *columns.add(i);
            let Ok(name) = (if p.is_null() {
                Err("列名为空")
            } else {
                CStr::from_ptr(p).to_str().map_err(|_| "列名不是合法 UTF-8")
            }) else {
                write_err(err, err_cap, "列名非法");
                return 2;
            };
            match t.column_index(name) {
                Some(ci) => idx.push(ci),
                None => {
                    write_err(err, err_cap, &format!("列不存在：{tname}.{name}"));
                    return 3;
                }
            }
        }
        Some(idx)
    };
    // 时间范围 → 行区间（与 read_window 同口径：含端点）。
    let lo = t.lower_bound(t0_ns);
    let hi = t.upper_bound(t1_ns);
    let sliced = match t.slice(lo, hi, col_indices.as_deref()) {
        Ok(x) => x,
        Err(e) => { write_err(err, err_cap, &e); return 5; }
    };
    let mut nd = PsDataset::new();
    if let Err(e) = nd.typed.push_table(sliced) { write_err(err, err_cap, &e); return 5; }
    nd.rebuild_infos();
    *out = Box::into_raw(Box::new(nd));
    0
}

/// 2.7.6 多列对齐读取：同表多列一次 FFI 读完 `[t0_ns, t1_ns]` 的对齐行区间。
///
/// `out_time` 容量 cap；`out_values` 是长度 column_count 的指针数组，
/// 每个元素指向一块 cap 大小的 f64 缓冲。行数超 cap 时等步长抽稀（与
/// ps_dataset_read_window 同口径），各列严格同行对齐；null 输出 NaN。
/// 返回实际写入的行数（0 = 空窗口/参数错误）。
///
/// # Safety
///
/// `ds` 必须为有效的 PsDataset 指针；`table/columns` 必须为 NUL 结尾 UTF-8 字符串；
/// `out_values` 含 `column_count` 个指向 cap 大小 f64 缓冲的可写指针。
#[no_mangle]
pub unsafe extern "C" fn ps_dataset_read_window_multi(
    ds: *mut PsDataset,
    table: *const c_char,
    columns: *const *const c_char,
    column_count: u32,
    t0_ns: i64,
    t1_ns: i64,
    out_time: *mut f64,
    out_values: *const *mut f64,
    cap: u32,
) -> u32 {
    if ds.is_null() || table.is_null() || out_time.is_null() || out_values.is_null() || cap == 0 {
        return 0;
    }
    let Ok(tname) = CStr::from_ptr(table).to_str() else { return 0 };
    let d = (*ds).read_table(tname);
    let Some(t) = d.typed.tables().iter().find(|t| t.name == tname) else { return 0 };
    let n = column_count as usize;
    let mut cols: Vec<&ps_ndf::typed::Column> = Vec::with_capacity(n);
    for i in 0..n {
        let p = *columns.add(i);
        if p.is_null() {
            return 0;
        }
        let Ok(cname) = CStr::from_ptr(p).to_str() else { return 0 };
        match t.columns.iter().find(|c| c.name == cname) {
            Some(c) => cols.push(c),
            None => return 0,
        }
    }
    let lo = t.time_ns.partition_point(|&x| x < t0_ns);
    let hi = t.time_ns.partition_point(|&x| x <= t1_ns);
    if hi <= lo {
        return 0;
    }
    let rows = hi - lo;
    let step = (rows as u64).div_ceil(cap as u64).max(1) as usize;
    let mut written = 0u32;
    let mut i = lo;
    while i < hi && (written as usize) < cap as usize {
        *out_time.add(written as usize) = t.time_ns[i] as f64 * 1e-9;
        for (ci, c) in cols.iter().enumerate() {
            let buf = *out_values.add(ci);
            *buf.add(written as usize) = c.value_as_f64(i).unwrap_or(f64::NAN);
        }
        written += 1;
        i += step;
    }
    written
}

/// 2.7.9 分位数统计：`probs` 为分位点（如 0.5/0.95/0.99），线性插值口径。
/// NaN/inf 不参与统计；样本为空时输出 NaN。
/// 返回 0 成功；1 参数错误；2 缓冲不足（out 仍写前 cap 个？否——直接拒绝）。
///
/// # Safety
///
/// `v` 必须指向 `n` 个连续 f64；`probs` 必须指向 `prob_count` 个连续 f64；
/// `out` 必须可写 `prob_count` 个 f64。
#[no_mangle]
pub unsafe extern "C" fn ps_stats_quantiles(
    v: *const f64,
    n: u32,
    probs: *const f64,
    prob_count: u32,
    out: *mut f64,
    out_cap: u32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if out.is_null() || prob_count > out_cap {
        write_err(err, err_cap, "输出缓冲不足或为空");
        return 2;
    }
    let data: &[f64] = if n == 0 || v.is_null() {
        &[]
    } else {
        std::slice::from_raw_parts(v, n as usize)
    };
    let ps: &[f64] = if prob_count == 0 || probs.is_null() {
        &[]
    } else {
        std::slice::from_raw_parts(probs, prob_count as usize)
    };
    let got = ps_ops::stats::quantiles(data, ps);
    for (i, q) in got.iter().enumerate() {
        *out.add(i) = *q;
    }
    0
}

/* ===== S7 派生依赖图 IncrementalEngine（薄 C ABI）=====
 * opaque handle + 显式喂数，不跨边界传 Rust 结构体。
 * C 侧负责从 dataset 读通道再喂入；本层只做编排与增量调度。
 */

/// C 侧看到的不透明 DAG 句柄。
pub struct PsDag {
    engine: ps_dag::IncrementalEngine,
    channels: std::collections::HashMap<String, ps_dag::Series>,
}

#[no_mangle]
pub unsafe extern "C" fn ps_dag_create() -> *mut PsDag {
    Box::into_raw(Box::new(PsDag {
        engine: ps_dag::IncrementalEngine::new(ps_dag::Graph::new()),
        channels: std::collections::HashMap::new(),
    }))
}

#[no_mangle]
pub unsafe extern "C" fn ps_dag_free(dag: *mut PsDag) {
    if !dag.is_null() {
        drop(Box::from_raw(dag));
    }
}

/// 喂入一条输入通道（复制 t/v，调用方可立即释放自己的缓冲）。
/// 返回 0 成功，1 参数非法。
#[no_mangle]
pub unsafe extern "C" fn ps_dag_set_input(
    dag: *mut PsDag,
    name: *const c_char,
    t: *const f64,
    v: *const f64,
    n: u32,
) -> i32 {
    if dag.is_null() {
        return 1;
    }
    let name = match require_name(name, "通道名") {
        Ok(s) => s.to_string(),
        Err(_) => return 1,
    };
    if n > 0 && (t.is_null() || v.is_null()) {
        return 1;
    }
    let (ts, vs) = if n == 0 {
        (Vec::new(), Vec::new())
    } else {
        (
            std::slice::from_raw_parts(t, n as usize).to_vec(),
            std::slice::from_raw_parts(v, n as usize).to_vec(),
        )
    };
    (*dag).channels.insert(name, ps_dag::Series { t: ts, v: vs });
    0
}

/// 加节点。input_node < 0 表示输入来自名为 input_channel 的通道；否则来自该上游节点。
/// 返回节点 id（>=0）；失败返回 -1 并写 err。
#[no_mangle]
pub unsafe extern "C" fn ps_dag_add_node(
    dag: *mut PsDag,
    name: *const c_char,
    filter_id: *const c_char,
    params_json: *const c_char,
    input_channel: *const c_char,
    input_node: i32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if dag.is_null() {
        return -1;
    }
    let name = match require_name(name, "节点名") {
        Ok(s) => s.to_string(),
        Err(e) => { write_err(err, err_cap, &e); return -1; }
    };
    let filter = match require_name(filter_id, "算子") {
        Ok(s) => s.to_string(),
        Err(e) => { write_err(err, err_cap, &e); return -1; }
    };
    let params = match parse_params(params_json, err, err_cap) {
        Ok(v) => v,
        Err(_) => return -1,
    };
    let source = if input_node < 0 {
        let ch = match require_name(input_channel, "输入通道") {
            Ok(s) => s.to_string(),
            Err(e) => { write_err(err, err_cap, &e); return -1; }
        };
        ps_dag::Source::Channel(ch)
    } else {
        ps_dag::Source::Node(input_node as usize)
    };
    let node = ps_dag::Node::new(name, filter, params, source);
    // graph 与 states 同步扩展；约定建图完成后再 evaluate。
    ((*dag).engine.add_node(node)) as i32
}

/// 算子热替换：节点 id、拓扑与下游引用不变，只换算子与参数。
///
/// 新算子先暂存，下次 `ps_dag_evaluate` **整体成功**才正式生效；失败则该节点
/// 自动恢复旧算子，输出一并回滚。调参改错公式不会把图弄坏。
/// 返回 0 成功；1 参数非法；2 节点不存在。
#[no_mangle]
pub unsafe extern "C" fn ps_dag_replace_node_op(
    dag: *mut PsDag,
    id: i32,
    filter_id: *const c_char,
    params_json: *const c_char,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if dag.is_null() || id < 0 {
        return 1;
    }
    let filter = match require_name(filter_id, "算子") {
        Ok(s) => s.to_string(),
        Err(e) => { write_err(err, err_cap, &e); return 1; }
    };
    let params = match parse_params(params_json, err, err_cap) {
        Ok(v) => v,
        Err(_) => return 1,
    };
    match (*dag).engine.replace_node_op(id as usize, filter, params) {
        Ok(()) => 0,
        Err(ps_dag::GraphError::NodeNotFound(_)) => {
            write_err(err, err_cap, "节点不存在");
            2
        }
        Err(e) => {
            write_err(err, err_cap, &format!("{:?}", e));
            2
        }
    }
}

/// 加 MIMO 节点：`n` 路输入按时间戳精确 join（缺任一路不产出）。
///
/// 第 i 路：`node_ids[i] < 0` 表示来自 `channel_names[i]`，否则来自该上游节点。
/// 每路至多一次 memcpy 级解析开销，`n < 2` 直接拒绝。
/// 返回节点 id（>=0）；-1 失败并写 err。
#[no_mangle]
pub unsafe extern "C" fn ps_dag_add_node_multi(
    dag: *mut PsDag,
    name: *const c_char,
    filter_id: *const c_char,
    params_json: *const c_char,
    channel_names: *const *const c_char,
    node_ids: *const i32,
    n: u32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if dag.is_null() || n < 2 || channel_names.is_null() || node_ids.is_null() {
        write_err(err, err_cap, "MIMO 节点需要至少 2 路输入");
        return -1;
    }
    let name = match require_name(name, "节点名") {
        Ok(s) => s.to_string(),
        Err(e) => { write_err(err, err_cap, &e); return -1; }
    };
    let filter = match require_name(filter_id, "算子") {
        Ok(s) => s.to_string(),
        Err(e) => { write_err(err, err_cap, &e); return -1; }
    };
    let params = match parse_params(params_json, err, err_cap) {
        Ok(v) => v,
        Err(_) => return -1,
    };
    let chans = std::slice::from_raw_parts(channel_names, n as usize);
    let ids = std::slice::from_raw_parts(node_ids, n as usize);
    let mut inputs = Vec::with_capacity(n as usize);
    for i in 0..n as usize {
        if ids[i] < 0 {
            match require_name(chans[i], "输入通道") {
                Ok(s) => inputs.push(ps_dag::Source::Channel(s.to_string())),
                Err(e) => { write_err(err, err_cap, &e); return -1; }
            }
        } else {
            inputs.push(ps_dag::Source::Node(ids[i] as usize));
        }
    }
    ((*dag).engine.add_node(ps_dag::Node::new_multi(name, filter, params, inputs))) as i32
}

/// 加依赖边。0 成功，1 参数非法，2 循环或重复。
#[no_mangle]
pub unsafe extern "C" fn ps_dag_add_edge(dag: *mut PsDag, from: i32, to: i32, err: *mut c_char, err_cap: u32) -> i32 {
    if dag.is_null() || from < 0 || to < 0 {
        return 1;
    }
    match (*dag).engine.add_edge(from as usize, to as usize) {
        Ok(()) => 0,
        Err(ps_dag::GraphError::Cycle) => {
            write_err(err, err_cap, "循环依赖被拒绝");
            2
        }
        Err(ps_dag::GraphError::DuplicateEdge) => {
            write_err(err, err_cap, "依赖边重复");
            2
        }
        Err(e) => {
            write_err(err, err_cap, &format!("{:?}", e));
            2
        }
    }
}

/// 按 generation 增量求值。0 成功；2 图有环/缺通道；3 算子错误。
#[no_mangle]
pub unsafe extern "C" fn ps_dag_evaluate(dag: *mut PsDag, generation: u64, err: *mut c_char, err_cap: u32) -> i32 {
    if dag.is_null() {
        return 1;
    }
    match (*dag).engine.evaluate(generation, &(*dag).channels) {
        Ok(()) => 0,
        Err(ps_dag::GraphError::Cycle) => {
            write_err(err, err_cap, "图存在循环依赖");
            2
        }
        Err(ps_dag::GraphError::ChannelMissing(n)) => {
            write_err(err, err_cap, &format!("缺少输入通道：{n}"));
            2
        }
        Err(e) => {
            write_err(err, err_cap, &format!("{:?}", e));
            3
        }
    }
}

/// 分片回放：按 `budget_ms` 推进本 generation 的求值。
///
/// 逐节点提交：某节点失败只影响该节点（输出不动、未提交的热替换算子被恢复），
/// 已提交的上游不回滚。成功时 `*out_done = 1` 表示本 generation 已全部完成。
/// 返回 0 成功；1 参数非法；2 图/通道错误；3 算子错误。
#[no_mangle]
pub unsafe extern "C" fn ps_dag_evaluate_step(
    dag: *mut PsDag,
    generation: u64,
    budget_ms: u32,
    out_done: *mut i32,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if dag.is_null() || out_done.is_null() {
        return 1;
    }
    let budget = std::time::Duration::from_millis(budget_ms as u64);
    match (*dag).engine.evaluate_step(generation, &(*dag).channels, budget) {
        Ok(done) => {
            *out_done = if done { 1 } else { 0 };
            0
        }
        Err(ps_dag::GraphError::Cycle) => {
            write_err(err, err_cap, "图存在循环依赖");
            2
        }
        Err(ps_dag::GraphError::ChannelMissing(n)) => {
            write_err(err, err_cap, &format!("缺少输入通道：{n}"));
            2
        }
        Err(ps_dag::GraphError::OpFailed(e)) => {
            write_err(err, err_cap, &format!("算子失败：{e}"));
            3
        }
        Err(e) => {
            write_err(err, err_cap, &format!("{:?}", e));
            3
        }
    }
}

/// 快照读屏障：把 generation 的剩余回放推完再返回，保证调用方读到完整结果。
///
/// 单线程库没有别的线程在推进，所以这里的「阻塞」= 自己推完（不限预算）。
/// 返回 0 成功；1 空句柄；2 图/通道错误；3 算子错误。
#[no_mangle]
pub unsafe extern "C" fn ps_dag_read_barrier(
    dag: *mut PsDag,
    generation: u64,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if dag.is_null() {
        return 1;
    }
    match (*dag).engine.read_barrier(generation, &(*dag).channels) {
        Ok(()) => 0,
        Err(ps_dag::GraphError::Cycle) => {
            write_err(err, err_cap, "图存在循环依赖");
            2
        }
        Err(ps_dag::GraphError::ChannelMissing(n)) => {
            write_err(err, err_cap, &format!("缺少输入通道：{n}"));
            2
        }
        Err(ps_dag::GraphError::OpFailed(e)) => {
            write_err(err, err_cap, &format!("算子失败：{e}"));
            3
        }
        Err(e) => {
            write_err(err, err_cap, &format!("{:?}", e));
            3
        }
    }
}

/// 节点输出数量。
#[no_mangle]
pub unsafe extern "C" fn ps_dag_output_count(dag: *const PsDag) -> u32 {
    if dag.is_null() {
        return 0;
    }
    (*dag).engine.graph.nodes.len() as u32
}/// 读节点输出。0 成功；1 参数非法；2 未求值/越界；6 缓冲不足（out_n 仍给出实际长度）。
#[no_mangle]
pub unsafe extern "C" fn ps_dag_output(
    dag: *const PsDag,
    node: u32,
    out_t: *mut f64,
    out_v: *mut f64,
    cap: u32,
    out_n: *mut u32,
) -> i32 {
    if dag.is_null() || out_n.is_null() {
        return 1;
    }
    let Some(s) = (*dag).engine.output(node as usize) else {
        return 2;
    };
    let n = s.t.len().min(s.v.len()) as u32;
    *out_n = n;
    if out_t.is_null() || out_v.is_null() || n > cap {
        return 6;
    }
    std::ptr::copy_nonoverlapping(s.t.as_ptr(), out_t, n as usize);
    std::ptr::copy_nonoverlapping(s.v.as_ptr(), out_v, n as usize);
    0
}

/* ===== BJ6 输入 retention gap（段 id 水位，不看时间戳） ===== */

/// 上报一处输入缺口：`channel` 上段 id `[from_id, to_id]` 未到达即被丢弃。
///
/// 判定缺口存在与否只比较段 id 是否跳号，`t_lo/t_hi` 仅作定位提示——因此
/// sparse / 全 null（时间不推进）的输入不会被误判成缺口。
/// 已在水位之内的重复上报会被忽略（返回 0 但不再记录）。
/// 返回 0 成功；1 参数非法（空句柄、非法通道名、`to_id < from_id`）。
#[no_mangle]
pub unsafe extern "C" fn ps_dag_report_gap(
    dag: *mut PsDag,
    channel: *const c_char,
    from_id: u64,
    to_id: u64,
    t_lo: f64,
    t_hi: f64,
) -> i32 {
    if dag.is_null() || to_id < from_id {
        return 1;
    }
    let ch = match require_name(channel, "通道名") {
        Ok(s) => s,
        Err(_) => return 1,
    };
    (*dag).engine.report_gap(ch, from_id, to_id, t_lo, t_hi);
    0
}

/// 当前保留的缺口条数（≤ 容量上限）。
#[no_mangle]
pub unsafe extern "C" fn ps_dag_gap_count(dag: *const PsDag) -> u32 {
    if dag.is_null() {
        return 0;
    }
    (*dag).engine.gaps().len() as u32
}

/// 累计检测到的缺口数（含已被挤出保留窗口的）。
#[no_mangle]
pub unsafe extern "C" fn ps_dag_gap_total(dag: *const PsDag) -> u64 {
    if dag.is_null() {
        return 0;
    }
    (*dag).engine.gap_tracker().total()
}

/// 读第 `idx` 条缺口（0 为最旧）。`channel_out` 写 NUL 结尾通道名，
/// 超出 `channel_cap - 1` 截断。任一输出指针可为空（表示不需要该字段）。
/// 返回 0 成功；1 参数非法或 `idx` 越界。
#[no_mangle]
pub unsafe extern "C" fn ps_dag_gap_at(
    dag: *const PsDag,
    idx: u32,
    channel_out: *mut c_char,
    channel_cap: u32,
    from_id: *mut u64,
    to_id: *mut u64,
    t_lo: *mut f64,
    t_hi: *mut f64,
) -> i32 {
    if dag.is_null() {
        return 1;
    }
    let Some(g) = (*dag).engine.gaps().get(idx as usize) else {
        return 1;
    };
    if !channel_out.is_null() && channel_cap > 0 {
        let bytes = g.channel.as_bytes();
        let n = bytes.len().min(channel_cap as usize - 1);
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), channel_out as *mut u8, n);
        *channel_out.add(n) = 0;
    }
    if !from_id.is_null() {
        *from_id = g.from_id;
    }
    if !to_id.is_null() {
        *to_id = g.to_id;
    }
    if !t_lo.is_null() {
        *t_lo = g.t_lo;
    }
    if !t_hi.is_null() {
        *t_hi = g.t_hi;
    }
    0
}

/// 设置缺口保留上限；0 表示只累计总数、不保留明细。
#[no_mangle]
pub unsafe extern "C" fn ps_dag_set_gap_capacity(dag: *mut PsDag, cap: u32) {
    if dag.is_null() {
        return;
    }
    (*dag).engine.set_gap_capacity(cap as usize);
}
