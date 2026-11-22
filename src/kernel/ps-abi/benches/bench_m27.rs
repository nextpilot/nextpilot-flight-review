//! M2.7 验收基准（harness = false，`cargo bench -p ps-abi` 或
//! `cargo run --release -p ps-abi --bench bench_m27`）。
//!
//! 场景 1（2.7.1）：Python push 的取数路径——100 万行 F64 列读成 f64 数组。
//!   旧路径 = ps_dataset_value_at 逐元素 FFI；
//!   新路径 = ps_dataset_column_raw 借出内部缓冲 + memcpy（含 null patch）。
//!   两者都会把结果写 /dev/shm 文件（真实 push 还要写文件，一并计入）。
//! 场景 2（2.7.2）：全景 100 万行 lod_minmax 帧——
//!   重建（≈旧行为，每次全扫 O(n)）vs 金字塔命中（O(px·log₄n)）。

use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::time::Instant;

use ps_abi::{
    ps_dataset_column_raw, ps_dataset_free, ps_dataset_lod_minmax, ps_dataset_value_at,
    PsDataset, PsFrameColumn,
};

const ROWS: usize = 1_000_000;
const PX: usize = 1200;

fn now_us() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_micros() as i64
}

unsafe fn make_dataset() -> *mut PsDataset {
    let ds = ps_abi::ps_dataset_new();
    let table = CString::new("ATT").unwrap();
    let name = CString::new("roll").unwrap();
    let unit = CString::new("deg").unwrap();
    // 时间轴 100Hz * 1M 行；值 = sin 抖动 + 尖峰；每 97 行一个 null。
    let times: Vec<i64> = (0..ROWS).map(|i| now_us() + (i as i64) * 10_000).collect();
    let values: Vec<f64> = (0..ROWS)
        .map(|i| {
            if i % 97 == 0 {
                f64::NAN
            } else {
                (i as f64 * 0.001).sin() * 5.0 + (i % 1009) as f64 * 0.01
            }
        })
        .collect();
    let nulls: Vec<u8> = {
        let mut bits = vec![0u8; (ROWS + 7) / 8];
        for i in (0..ROWS).step_by(97) {
            bits[i / 8] |= 1 << (i % 8);
        }
        bits
    };
    let col = PsFrameColumn {
        name: name.as_ptr(),
        unit: unit.as_ptr(),
        value_type: 0,
        values: values.as_ptr() as *const std::ffi::c_void,
        nulls: nulls.as_ptr(),
    };
    let rc = ps_abi::ps_dataset_append_frame(
        ds,
        table.as_ptr(),
        times.as_ptr(),
        ROWS as u32,
        &col,
        1,
        std::ptr::null_mut(),
        0,
    );
    assert_eq!(rc, 0);
    ds
}

/// 旧路径：逐元素 FFI 读出 f64（null → NaN），可选写 shm 文件。
unsafe fn old_push(ds: *mut PsDataset, table: &CStr, column: &CStr, out_path: Option<&str>) -> std::time::Duration {
    let mut buf = vec![0f64; ROWS];
    let start = Instant::now();
    for i in 0..ROWS {
        let mut t = 0f64;
        let mut v = 0f64;
        let rc = ps_dataset_value_at(ds, table.as_ptr(), column.as_ptr(), i as u64, &mut t, &mut v);
        buf[i] = if rc == 2 { f64::NAN } else { v };
    }
    if let Some(p) = out_path {
        std::fs::write(p, std::slice::from_raw_parts(buf.as_ptr() as *const u8, buf.len() * 8)).unwrap();
    }
    start.elapsed()
}

/// 新路径：column_raw 借出内部缓冲 + memcpy（null patch），可选写 shm 文件。
unsafe fn new_push(ds: *mut PsDataset, table: &CStr, column: &CStr, out_path: Option<&str>) -> std::time::Duration {
    let start = Instant::now();
    unsafe {
        let mut ptr: *const std::ffi::c_void = std::ptr::null();
        let mut count: u64 = 0;
        let mut ty: u32 = 0;
        let mut nulls: *const u8 = std::ptr::null();
        let rc = ps_dataset_column_raw(
            ds,
            table.as_ptr(),
            column.as_ptr(),
            &mut ptr,
            &mut count,
            &mut ty,
            &mut nulls,
        );
        assert_eq!(rc, 0);
        assert_eq!(ty, 0); // F64
        if nulls.is_null() {
            if let Some(p) = out_path {
                std::fs::write(p, std::slice::from_raw_parts(ptr as *const u8, count as usize * 8)).unwrap();
            }
        } else {
            // 有 null：memcpy 后按位图补 NaN（仍然是一次整块拷贝）。
            let mut buf = vec![0f64; count as usize];
            std::ptr::copy_nonoverlapping(ptr as *const f64, buf.as_mut_ptr(), count as usize);
            let bits = std::slice::from_raw_parts(nulls, (count as usize + 7) / 8);
            for i in 0..count as usize {
                if bits[i / 8] & (1 << (i % 8)) != 0 {
                    buf[i] = f64::NAN;
                }
            }
            if let Some(p) = out_path {
                std::fs::write(p, std::slice::from_raw_parts(buf.as_ptr() as *const u8, buf.len() * 8)).unwrap();
            }
        }
    }
    start.elapsed()
}

fn main() {
    unsafe {
    println!("=== M2.7 验收基准（{} 行 F64）===", ROWS);
    let ds = make_dataset();
    let table = CStr::from_bytes_with_nul(b"ATT\0").unwrap();
    let column = CStr::from_bytes_with_nul(b"roll\0").unwrap();
    let tmp = std::env::temp_dir();

    // 预热一次
    let _ = old_push(ds, table, column, Some(tmp.join("ngpy-bench-old.bin").to_str().unwrap()));

    let old_mem = old_push(ds, table, column, None);
    let new_mem = new_push(ds, table, column, None);
    let old_io = old_push(ds, table, column, Some(tmp.join("ngpy-bench-old.bin").to_str().unwrap()));
    let new_io = new_push(ds, table, column, Some(tmp.join("ngpy-bench-new.bin").to_str().unwrap()));
    println!(
        "2.7.1 Python push 取数（1M 行 F64，含 null patch）:\n  旧 value_at 逐元素 FFI（纯取数）: {:>10.2?}\n  新 column_raw memcpy（纯取数）:    {:>10.2?}  ({:.1}x，硬线 ≤2ms)\n  旧（含写 8MB 文件）:              {:>10.2?}\n  新（含写 8MB 文件）:              {:>10.2?}  ({:.1}x)",
        old_mem,
        new_mem,
        old_mem.as_secs_f64() / new_mem.as_secs_f64().max(1e-9),
        old_io,
        new_io,
        old_io.as_secs_f64() / new_io.as_secs_f64().max(1e-9)
    );

    // 2.7.2 全景 LOD：首帧建塔（一次性 O(n)），之后每帧命中。
    let mut triples = vec![0f64; PX * 3];
    let t0 = now_us();
    let t1 = t0 + (ROWS as i64) * 10_000;
    let first = {
        let s = Instant::now();
        let n = ps_dataset_lod_minmax(ds, table.as_ptr(), column.as_ptr(), t0, t1, PX as u32, triples.as_mut_ptr(), PX as u32);
        assert!(n > 0);
        s.elapsed()
    };
    // 金字塔命中：重复调用取中位。
    let mut hits = Vec::new();
    for _ in 0..20 {
        let s = Instant::now();
        ps_dataset_lod_minmax(ds, table.as_ptr(), column.as_ptr(), t0, t1, PX as u32, triples.as_mut_ptr(), PX as u32);
        hits.push(s.elapsed());
    }
    hits.sort();
    let hit = hits[hits.len() / 2];
    // 旧行为模拟：置塔失效 → 每帧重建 O(n)。
    let mut rebuilds = Vec::new();
    for _ in 0..5 {
        invalidate_pyramid(ds, table, column);
        let s = Instant::now();
        ps_dataset_lod_minmax(ds, table.as_ptr(), column.as_ptr(), t0, t1, PX as u32, triples.as_mut_ptr(), PX as u32);
        rebuilds.push(s.elapsed());
    }
    rebuilds.sort();
    let rebuild = rebuilds[rebuilds.len() / 2];
    println!(
        "2.7.2 全景 LOD 帧（1M 行 → {} px）:\n  旧（每帧全量重扫，≈重建）: {:>10.2?}\n  新首帧（建塔一次性 O(n)）: {:>10.2?}\n  新命中帧（金字塔 O(px·log₄n)）: {:>10.2?}  ({:.1}x vs 旧)",
        PX, rebuild, first, hit, rebuild.as_secs_f64() / hit.as_secs_f64().max(1e-9)
    );

    ps_dataset_free(ds);
    let _ = std::fs::remove_file(tmp.join("ngpy-bench-old.bin"));
    let _ = std::fs::remove_file(tmp.join("ngpy-bench-new.bin"));
}

/// 测试钩子：把指定列的金字塔清掉（模拟旧行为的每帧全扫）。
unsafe fn invalidate_pyramid(ds: *mut PsDataset, table: &CStr, column: &CStr) {
    // ps-abi 未暴露列句柄；这里通过一次 append 1 行 + ring 修剪触发 drop_prefix？
    // 更直接：借 Rlib 访问（bench 与 ps-abi 同 workspace，可走 pub API）。
    // ps_abi 没有公开内部句柄，改为在 ps-abi 里提供 #[doc(hidden)] 辅助。
    ps_abi::__m27_invalidate_lod_for_bench(ds, table.as_ptr() as *const c_char, column.as_ptr() as *const c_char);
    }
}
