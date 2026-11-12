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
