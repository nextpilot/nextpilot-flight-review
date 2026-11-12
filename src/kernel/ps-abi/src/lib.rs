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

/// 读 CSV → 数据集。成功返回 0 并写 `out`；失败返回非 0，`err` 里给原因。
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

    let mut ds = Dataset { channels: Vec::new(), infos: Vec::new() };
    for (i, header) in table.headers.iter().enumerate().skip(1) {
        let t = table.columns.first().cloned().unwrap_or_default();
        let v = table.columns.get(i).cloned().unwrap_or_default();
        ds.channels.push(Channel {
            name: CString::new(header.as_str()).unwrap_or_else(|_| CString::new("channel").unwrap()),
            unit: unit_of(header),
            t,
            v,
        });
    }
    ds.rebuild_infos();
    *out = Box::into_raw(Box::new(ds));
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
