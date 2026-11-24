//! BJ4 补传路径基准：断线重连场景下的写入与读取代价。
//!
//! 现有 `bench_dataset` 只覆盖"建表 → 读"的稳态，完全没有补传（pending 段）路径。
//! 本基准补三个真实场景：
//!
//!   backfill_push        单批补传写入（10k 行落到 pending 段）——应为 O(批)
//!   backfill_first_read  补传后第一次读：归并 O(N) + LOD 整塔重建
//!   backfill_alt_round   补传→读 交替一轮（真实断线重连 + 每帧重绘）
//!   backfill_batch10_read 补传 10 批后读一次（BJ4 声称的收益场景）
//!   steady_lod_after     归并后稳态 LOD（对照：证明开销全在归并 + 重建）
//!
//! 运行：`cargo bench -p ps-abi --bench bench_backfill`

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_void};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use ps_abi::{
    ps_dataset_append_frame, ps_dataset_column_raw, ps_dataset_free, ps_dataset_lod_minmax,
    ps_dataset_new, ps_dataset_read_window, PsDataset, PsFrameColumn,
};

const ROWS: usize = 1_000_000;
const PERIOD_NS: i64 = 10_000_000; // 100 Hz
const PX: usize = 1200;
const CAP: u32 = 4096;
const ROUNDS: usize = 9; // 归并很重，轮数少一些
const BATCH: usize = 10_000; // 每批补传行数
const BATCHES: usize = 10; // "多批补传后单次读"的批数

fn now_ns() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_nanos() as i64)
        .unwrap_or(0)
}

fn us(d: Duration) -> f64 {
    d.as_secs_f64() * 1e6
}

fn percentile(sorted: &[f64], q: f64) -> f64 {
    if sorted.is_empty() {
        return 0.0;
    }
    let idx = ((sorted.len() as f64 - 1.0) * q).round() as usize;
    sorted[idx.min(sorted.len() - 1)]
}

struct Stat {
    p50: f64,
    p99: f64,
}

fn summarize(mut samples: Vec<f64>) -> Stat {
    samples.sort_by(|a, b| a.partial_cmp(b).unwrap());
    Stat { p50: percentile(&samples, 0.50), p99: percentile(&samples, 0.99) }
}

unsafe fn build_dataset() -> (*mut PsDataset, CString, CString, CString, i64) {
    let ds = ps_dataset_new();
    let table = CString::new("ATT").unwrap();
    let name = CString::new("roll").unwrap();
    let unit = CString::new("deg").unwrap();
    let base = now_ns();
    let times: Vec<i64> = (0..ROWS).map(|i| base + (i as i64) * PERIOD_NS).collect();
    let values: Vec<f64> = (0..ROWS)
        .map(|i| (i as f64 * 0.001).sin() * 5.0 + (i % 1009) as f64 * 0.01)
        .collect();
    let col = PsFrameColumn {
        name: name.as_ptr(),
        unit: unit.as_ptr(),
        value_type: 0,
        values: values.as_ptr() as *const c_void,
        nulls: std::ptr::null(),
    };
    let rc = ps_dataset_append_frame(
        ds,
        table.as_ptr(),
        times.as_ptr(),
        ROWS as u32,
        &col,
        1,
        std::ptr::null_mut(),
        0,
    );
    assert_eq!(rc, 0, "构造基准数据集失败");
    (ds, table, name, unit, base)
}

/// 构造一批"落在主表中部"的补传帧：时间戳从 `start_row` 开始，间隔 3 个采样点，
/// 保证落在主表时间范围内（即走 merge_sorted_rows 的 backfill 分支）。
unsafe fn push_backfill(
    ds: *mut PsDataset,
    table: &CString,
    name: &CString,
    unit: &CString,
    base: i64,
    start_row: usize,
) {
    let times: Vec<i64> = (0..BATCH)
        .map(|i| base + ((start_row + i * 3) as i64) * PERIOD_NS)
        .collect();
    let values: Vec<f64> = (0..BATCH).map(|i| ((i * 7) % 1000) as f64 * 0.01).collect();
    let col = PsFrameColumn {
        name: name.as_ptr(),
        unit: unit.as_ptr(),
        value_type: 0,
        values: values.as_ptr() as *const c_void,
        nulls: std::ptr::null(),
    };
    let rc = ps_dataset_append_frame(
        ds,
        table.as_ptr(),
        times.as_ptr(),
        BATCH as u32,
        &col,
        1,
        std::ptr::null_mut(),
        0,
    );
    assert_eq!(rc, 0, "补传失败");
}

unsafe fn lod_read(ds: *mut PsDataset, table: &CStr, name: &CStr, base: i64, triples: &mut [f64]) -> u32 {
    let t0 = base;
    let t1 = base + (ROWS as i64 + 4 * BATCH as i64) * PERIOD_NS;
    ps_dataset_lod_minmax(
        ds,
        table.as_ptr(),
        name.as_ptr(),
        t0,
        t1,
        PX as u32,
        triples.as_mut_ptr(),
        PX as u32,
    )
}

unsafe fn run_all() -> Vec<(&'static str, Stat)> {
    let mut out: Vec<(&'static str, Stat)> = Vec::new();
    let mut triples = vec![0f64; PX * 3];
    let table_c = CStr::from_bytes_with_nul(b"ATT\0").unwrap();
    let col_c = CStr::from_bytes_with_nul(b"roll\0").unwrap();

    // ---- 1. 单批补传写入成本 ----
    {
        let (ds, table, name, unit, base) = build_dataset();
        let mut samples = Vec::new();
        let cursor = 1000usize;
        for r in 0..ROUNDS {
            let s = Instant::now();
            push_backfill(ds, &table, &name, &unit, base, cursor + r * 7);
            samples.push(us(s.elapsed()));
        }
        out.push(("backfill_push", summarize(samples)));
        ps_dataset_free(ds);
    }

    // ---- 2. 补传后首次读：归并 + LOD 重建 ----
    {
        let mut samples = Vec::new();
        for r in 0..ROUNDS {
            let (ds, table, name, unit, base) = build_dataset();
            push_backfill(ds, &table, &name, &unit, base, 1000 + r * 7);
            let s = Instant::now();
            let n = lod_read(ds, &table_c, &col_c, base, &mut triples);
            samples.push(us(s.elapsed()));
            assert!(n > 0);
            ps_dataset_free(ds);
        }
        out.push(("backfill_first_read", summarize(samples)));
    }

    // ---- 3. 补传→读 交替一轮（断线重连 + 每帧刷新）----
    {
        let mut samples = Vec::new();
        let (ds, table, name, unit, base) = build_dataset();
        for r in 0..ROUNDS {
            // 注意：交替场景下主表已被上一轮归并撑大，这里用递增的起点避免撞已有行。
            let s = Instant::now();
            push_backfill(ds, &table, &name, &unit, base, 2000 + r * 11);
            let n = lod_read(ds, &table_c, &col_c, base, &mut triples);
            samples.push(us(s.elapsed()));
            assert!(n > 0);
        }
        out.push(("backfill_alt_round", summarize(samples)));
        ps_dataset_free(ds);
    }

    // ---- 4. 补传 10 批后读一次（BJ4 声称的收益场景）----
    {
        let mut samples = Vec::new();
        for r in 0..3 {
            let (ds, table, name, unit, base) = build_dataset();
            let s = Instant::now();
            for b in 0..BATCHES {
                push_backfill(ds, &table, &name, &unit, base, 1000 + b * 17 + r * 3);
            }
            let n = lod_read(ds, &table_c, &col_c, base, &mut triples);
            samples.push(us(s.elapsed()));
            assert!(n > 0);
            ps_dataset_free(ds);
        }
        out.push(("backfill_batch10_read", summarize(samples)));
    }

    // ---- 5. 归并后稳态 LOD（对照）----
    {
        let (ds, table, name, unit, base) = build_dataset();
        push_backfill(ds, &table, &name, &unit, base, 1000);
        // 首次读把 pending 归并掉
        lod_read(ds, &table_c, &col_c, base, &mut triples);
        let mut samples = Vec::new();
        for _ in 0..ROUNDS {
            let s = Instant::now();
            lod_read(ds, &table_c, &col_c, base, &mut triples);
            samples.push(us(s.elapsed()));
        }
        out.push(("steady_lod_after", summarize(samples)));
        ps_dataset_free(ds);
    }

    // ---- 6. 归并后稳态 readWindow（对照）----
    {
        let (ds, table, name, unit, base) = build_dataset();
        push_backfill(ds, &table, &name, &unit, base, 1000);
        lod_read(ds, &table_c, &col_c, base, &mut triples);
        let mut ot = vec![0f64; CAP as usize];
        let mut ov = vec![0f64; CAP as usize];
        let t0 = base + 500_000 * PERIOD_NS;
        let t1 = t0 + 1000 * PERIOD_NS;
        let mut samples = Vec::new();
        for _ in 0..ROUNDS {
            let s = Instant::now();
            let n = ps_dataset_read_window(
                ds,
                table.as_ptr(),
                name.as_ptr(),
                t0,
                t1,
                ot.as_mut_ptr(),
                ov.as_mut_ptr(),
                CAP,
            );
            samples.push(us(s.elapsed()));
            assert!(n > 0);
        }
        out.push(("steady_window_after", summarize(samples)));
        ps_dataset_free(ds);
    }

    // ---- 7. 拆解：只归并（column_raw 触发 materialize，不建 LOD 塔）----
    {
        let (ds, table, name, unit, base) = build_dataset();
        push_backfill(ds, &table, &name, &unit, base, 1000);
        let mut p: *const c_void = std::ptr::null();
        let mut cnt: u64 = 0;
        let mut ty: u32 = 0;
        let mut samples = Vec::new();
        for r in 0..ROUNDS {
            if r > 0 {
                push_backfill(ds, &table, &name, &unit, base, 3000 + r * 13);
            }
            let s = Instant::now();
            let rc = ps_dataset_column_raw(
                ds,
                table.as_ptr(),
                name.as_ptr(),
                &mut p,
                &mut cnt,
                &mut ty,
                std::ptr::null_mut(),
            );
            samples.push(us(s.elapsed()));
            assert_eq!(rc, 0);
        }
        println!("[debug] materialize_only 后 count={cnt}");
        out.push(("materialize_only", summarize(samples)));
        ps_dataset_free(ds);
    }

    // ---- 8. 拆解：归并后只建 LOD 塔 ----
    {
        let (ds, table, name, unit, base) = build_dataset();
        push_backfill(ds, &table, &name, &unit, base, 1000);
        // 先归并（column_raw 不建塔）
        let mut p: *const c_void = std::ptr::null();
        let mut cnt: u64 = 0;
        let mut ty: u32 = 0;
        ps_dataset_column_raw(ds, table.as_ptr(), name.as_ptr(), &mut p, &mut cnt, &mut ty, std::ptr::null_mut());
        let mut samples = Vec::new();
        for r in 0..ROUNDS {
            if r > 0 {
                push_backfill(ds, &table, &name, &unit, base, 5000 + r * 19);
                ps_dataset_column_raw(ds, table.as_ptr(), name.as_ptr(), &mut p, &mut cnt, &mut ty, std::ptr::null_mut());
            }
            // 归并已经把 lod 清空，这里测的就是纯建塔
            let s = Instant::now();
            let n = lod_read(ds, &table_c, &col_c, base, &mut triples);
            samples.push(us(s.elapsed()));
            assert!(n > 0);
        }
        out.push(("lod_rebuild_only", summarize(samples)));
        ps_dataset_free(ds);
    }

    out
}

fn print_table(rows: &[(&'static str, Stat)]) {
    println!("=== BJ4 补传路径基准（{} 万行主表 / 每批 {} 行）===", ROWS / 10_000, BATCH);
    println!("{:<22}{:>12}{:>12}", "metric", "p50(us)", "p99(us)");
    println!("{}", "=".repeat(48));
    for (name, s) in rows {
        println!("{:<22}{:>12.2}{:>12.2}", name, s.p50, s.p99);
    }
}

fn main() {
    unsafe {
        let rows = run_all();
        print_table(&rows);
    }
}
