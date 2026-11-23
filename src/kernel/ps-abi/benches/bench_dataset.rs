//! SCENE-02 / S8 数据层基准：纳秒口径性能基线 + p99 回归守护。
//!
//! 这是 S1（时间轴 us→ns）落地后建立的"纳秒版"基线，作为 S3(live 追加) /
//! S5(列压缩) 的回归对照。与历史 `bench_m27.rs`（M2.7 验收口径）并存，互不干扰。
//!
//! 运行方式（`cargo bench -p ps-abi --bench bench_dataset -- <args>`）：
//!   （无参数）                          跑基准并打印 p50/p99
//!   --save-baseline <path>             跑基准并把结果存为基线 JSON
//!   --baseline <path> [--threshold T] [--metric p50|p99] [--min-abs US]
//!                                      跑基准并与基线对比，选定指标劣化 > T 判失败(退出码 1)
//!
//! 判据指标默认 p50：p50 在共享/虚拟环境稳定（实测波动 <1%）；p99 尾部延迟对
//! CPU 调度噪声极敏感（实测同代码连跑波动可达 ±25%），只在专用、稳定 CI 机器上
//! 用 `--metric p99` 才有意义。表格同时打印两个指标的变化，但只用选定指标判定。
//! 为避免 window_read/value_at 这类微秒级小指标被噪声误报，判定还需绝对劣化
//! 超过 `--min-abs`（默认 5us）：相对超阈值且绝对超容差才算回归。
//!
//! 使用约束：基线须在**专用、负载稳定**的机器采集，对比也在同环境跑。共享/虚拟
//! 环境（如 CI 沙箱）的 CPU 调度抖动会让时延波动 10%+，导致误报；这类环境应放宽
//! `--threshold`/`--min-abs` 或仅用本基准观察趋势，不作硬门禁。基线文件机器相关，
//! 不入库，由使用方在其 CI 环境自行采集维护。
//!
//! 指标（均在 100 万行 F64 列、纳秒时间轴上）：
//!   lod_build    LOD 金字塔首帧建塔（O(n)，一次性）
//!   lod_hit      LOD 金字塔命中查询（O(px·log₄n)，稳态）
//!   window_read  readWindow 有界读（cap=4096）
//!   column_raw   columnRaw 整列零拷贝借出 + memcpy
//!   value_at     随机行号访问（逐元素 FFI）
//!   append_batch 批追加吞吐（每批 5 万行，含增量建塔）
//!
//! 单位统一为微秒(us)。统计取多轮样本的 p50/p99，避免单次噪声。

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_void};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use ps_abi::{
    ps_dataset_append_frame, ps_dataset_column_raw, ps_dataset_free, ps_dataset_lod_minmax,
    ps_dataset_new, ps_dataset_read_window, ps_dataset_value_at, PsDataset, PsFrameColumn,
};
use serde_json::{json, Map, Value};

const ROWS: usize = 1_000_000;
const PERIOD_NS: i64 = 10_000_000; // 100 Hz 采样 → 10 ms = 1e7 ns
const PX: usize = 1200;
const CAP: u32 = 4096;
const ROUNDS: usize = 21; // 奇数，中位数好取
const BUILD_ROUNDS: usize = 5; // 建塔要 invalidate，次数少些
const APPEND_BATCHES: usize = 20;
const APPEND_BATCH_ROWS: usize = 50_000;
const VALUE_AT_PER_ROUND: usize = 1000;
const DEFAULT_THRESHOLD: f64 = 0.10; // 选定指标劣化 >10% 判失败
const DEFAULT_MIN_ABS_US: f64 = 5.0; // 且绝对劣化 >5us，避免极小指标的微秒噪声被百分比放大

fn now_ns() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_nanos() as i64
}

fn us(d: Duration) -> f64 {
    d.as_secs_f64() * 1e6
}

fn percentile(sorted: &[f64], q: f64) -> f64 {
    if sorted.is_empty() {
        return 0.0;
    }
    let pos = (sorted.len() - 1) as f64 * q;
    let lo = pos.floor() as usize;
    let hi = pos.ceil() as usize;
    if lo == hi {
        sorted[lo]
    } else {
        sorted[lo] + (sorted[hi] - sorted[lo]) * (pos - lo as f64)
    }
}

#[derive(Clone, Copy)]
struct Stat {
    p50: f64,
    p99: f64,
}

fn summarize(mut samples: Vec<f64>) -> Stat {
    samples.sort_by(|a, b| a.partial_cmp(b).unwrap());
    Stat {
        p50: percentile(&samples, 0.50),
        p99: percentile(&samples, 0.99),
    }
}

/// 构造 100 万行 F64 表，时间轴纳秒、100Hz 递增（满足内核单调要求）。
unsafe fn build_dataset() -> (*mut PsDataset, CString, CString, i64) {
    let ds = ps_dataset_new();
    let table = CString::new("ATT").unwrap();
    let name = CString::new("roll").unwrap();
    let unit = CString::new("deg").unwrap();
    let base = now_ns();
    let times: Vec<i64> = (0..ROWS)
        .map(|i| base + (i as i64) * PERIOD_NS)
        .collect();
    let values: Vec<f64> = (0..ROWS)
        .map(|i| (i as f64 * 0.001).sin() * 5.0 + (i % 1009) as f64 * 0.01)
        .collect();
    let col = PsFrameColumn {
        name: name.as_ptr(),
        unit: unit.as_ptr(),
        value_type: 0, // F64
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
    (ds, table, name, base)
}

/// 简单 LCG，给 value_at 随机访问用（避免顺序访问被缓存过度优化）。
struct Lcg(u64);
impl Lcg {
    fn next(&mut self, n: usize) -> usize {
        self.0 = self
            .0
            .wrapping_mul(6364136223846793005)
            .wrapping_add(1442695040888963407);
        ((self.0 >> 33) as usize) % n
    }
}

unsafe fn run_all() -> Vec<(&'static str, Stat)> {
    let mut out: Vec<(&'static str, Stat)> = Vec::new();
    let (ds, table, name, base) = build_dataset();
    let table_c = CStr::from_bytes_with_nul(b"ATT\0").unwrap();
    let col_c = CStr::from_bytes_with_nul(b"roll\0").unwrap();
    let t0 = base;
    let t1 = base + (ROWS as i64) * PERIOD_NS;
    let mut triples = vec![0f64; PX * 3];

    // ---- 1. LOD 首帧建塔 ----
    let mut build_samples = Vec::new();
    for _ in 0..BUILD_ROUNDS {
        ps_abi::__m27_invalidate_lod_for_bench(
            ds,
            table.as_ptr() as *const c_char,
            name.as_ptr() as *const c_char,
        );
        let s = Instant::now();
        let n = ps_dataset_lod_minmax(
            ds,
            table_c.as_ptr(),
            col_c.as_ptr(),
            t0,
            t1,
            PX as u32,
            triples.as_mut_ptr(),
            PX as u32,
        );
        assert!(n > 0);
        build_samples.push(us(s.elapsed()));
    }
    out.push(("lod_build", summarize(build_samples)));

    // ---- 2. LOD 命中查询 ----
    let mut hit_samples = Vec::new();
    for _ in 0..ROUNDS {
        let s = Instant::now();
        ps_dataset_lod_minmax(
            ds,
            table_c.as_ptr(),
            col_c.as_ptr(),
            t0,
            t1,
            PX as u32,
            triples.as_mut_ptr(),
            PX as u32,
        );
        hit_samples.push(us(s.elapsed()));
    }
    out.push(("lod_hit", summarize(hit_samples)));

    // ---- 3. readWindow 有界读 ----
    let mut ot = vec![0f64; CAP as usize];
    let mut ov = vec![0f64; CAP as usize];
    let mut win_samples = Vec::new();
    for _ in 0..ROUNDS {
        let s = Instant::now();
        let n = ps_dataset_read_window(
            ds,
            table_c.as_ptr(),
            col_c.as_ptr(),
            t0,
            t1,
            ot.as_mut_ptr(),
            ov.as_mut_ptr(),
            CAP,
        );
        // cap 是有界读取的上限，等步长抽稀后实际点数可能略少于 cap
        assert!(n > 0 && n <= CAP);
        win_samples.push(us(s.elapsed()));
    }
    out.push(("window_read", summarize(win_samples)));

    // ---- 4. columnRaw 整列借出 + memcpy ----
    let mut raw_samples = Vec::new();
    for _ in 0..ROUNDS {
        let s = Instant::now();
        let mut ptr: *const c_void = std::ptr::null();
        let mut count: u64 = 0;
        let mut ty: u32 = 0;
        let mut nulls: *const u8 = std::ptr::null();
        let rc = ps_dataset_column_raw(
            ds,
            table_c.as_ptr(),
            col_c.as_ptr(),
            &mut ptr,
            &mut count,
            &mut ty,
            &mut nulls,
        );
        assert_eq!(rc, 0);
        assert_eq!(ty, 0);
        assert!(!nulls.is_null() || count == ROWS as u64);
        let mut buf = vec![0f64; count as usize];
        std::ptr::copy_nonoverlapping(ptr as *const f64, buf.as_mut_ptr(), count as usize);
        std::hint::black_box(&buf);
        raw_samples.push(us(s.elapsed()));
    }
    out.push(("column_raw", summarize(raw_samples)));

    // ---- 5. value_at 随机访问 ----
    let mut rng = Lcg(0x1234_5678_9ABC_DEF0);
    let mut va_samples = Vec::new();
    for _ in 0..ROUNDS {
        let s = Instant::now();
        for _ in 0..VALUE_AT_PER_ROUND {
            let row = rng.next(ROWS) as u64;
            let mut t = 0f64;
            let mut v = 0f64;
            let rc = ps_dataset_value_at(
                ds,
                table_c.as_ptr(),
                col_c.as_ptr(),
                row,
                &mut t,
                &mut v,
            );
            assert_eq!(rc, 0);
            std::hint::black_box(v);
        }
        va_samples.push(us(s.elapsed()) / VALUE_AT_PER_ROUND as f64);
    }
    out.push(("value_at", summarize(va_samples)));
    ps_dataset_free(ds);

    // ---- 6. append 批追加吞吐（独立数据集，含增量建塔）----
    let ads = ps_dataset_new();
    let atable = CString::new("LOG").unwrap();
    let aname = CString::new("x").unwrap();
    let aunit = CString::new("").unwrap();
    let abase = now_ns();
    let mut append_samples = Vec::new();
    for b in 0..APPEND_BATCHES {
        let start = (b * APPEND_BATCH_ROWS) as i64;
        let times: Vec<i64> = (0..APPEND_BATCH_ROWS)
            .map(|i| abase + (start + i as i64) * PERIOD_NS)
            .collect();
        let values: Vec<f64> = (0..APPEND_BATCH_ROWS)
            .map(|i| (i as f64).sin())
            .collect();
        let col = PsFrameColumn {
            name: aname.as_ptr(),
            unit: aunit.as_ptr(),
            value_type: 0,
            values: values.as_ptr() as *const c_void,
            nulls: std::ptr::null(),
        };
        let s = Instant::now();
        let rc = ps_dataset_append_frame(
            ads,
            atable.as_ptr(),
            times.as_ptr(),
            APPEND_BATCH_ROWS as u32,
            &col,
            1,
            std::ptr::null_mut(),
            0,
        );
        assert_eq!(rc, 0, "批追加失败");
        append_samples.push(us(s.elapsed()));
    }
    out.push(("append_batch", summarize(append_samples)));
    ps_dataset_free(ads);

    // ---- 7. 乱序批追加（S2 reorder 入口）----
    // 每批内时间反转（最坏情况的乱序），但整批窗口仍递增，不触发 backfill 合并。
    // 有序追加路径走 order=None 零开销直通；本基准度量乱序时的 O(k log k) 排序成本。
    let uds = ps_dataset_new();
    let utable = CString::new("LOGU").unwrap();
    let uname = CString::new("x").unwrap();
    let uunit = CString::new("").unwrap();
    let ubase = now_ns();
    let mut unordered_samples = Vec::new();
    for b in 0..APPEND_BATCHES {
        let start = (b * APPEND_BATCH_ROWS) as i64;
        let times: Vec<i64> = (0..APPEND_BATCH_ROWS)
            .map(|i| ubase + (start + (APPEND_BATCH_ROWS - 1 - i) as i64) * PERIOD_NS)
            .collect(); // 批内时间逆序
        let values: Vec<f64> = (0..APPEND_BATCH_ROWS)
            .map(|i| (i as f64).sin())
            .collect();
        let col = PsFrameColumn {
            name: uname.as_ptr(),
            unit: uunit.as_ptr(),
            value_type: 0,
            values: values.as_ptr() as *const c_void,
            nulls: std::ptr::null(),
        };
        let s = Instant::now();
        let rc = ps_dataset_append_frame(
            uds,
            utable.as_ptr(),
            times.as_ptr(),
            APPEND_BATCH_ROWS as u32,
            &col,
            1,
            std::ptr::null_mut(),
            0,
        );
        assert_eq!(rc, 0, "乱序批追加失败");
        unordered_samples.push(us(s.elapsed()));
    }
    out.push(("append_batch_unordered", summarize(unordered_samples)));
    ps_dataset_free(uds);

    out
}

fn print_table(
    results: &[(&str, Stat)],
    base_metrics: Option<&Map<String, Value>>,
    threshold: f64,
    min_abs_us: f64,
    metric: &str,
) -> bool {
    println!(
        "{:<16} {:>11} {:>11} {:>11} {:>11} {:>6}",
        "metric", "p50(us)", "p99(us)", "p50Δ", "p99Δ", "判定"
    );
    println!("{}", "=".repeat(74));
    let mut all_ok = true;
    for (name, s) in results {
        // (相对变化, 绝对变化 us)
        let (d50, d99) = if let Some(bm) = base_metrics {
            let b50 = bm.get(*name).and_then(|v| v.get("p50_us")).and_then(|v| v.as_f64());
            let b99 = bm.get(*name).and_then(|v| v.get("p99_us")).and_then(|v| v.as_f64());
            (
                b50.map(|b| ((s.p50 - b) / b.max(1e-9), s.p50 - b)),
                b99.map(|b| ((s.p99 - b) / b.max(1e-9), s.p99 - b)),
            )
        } else {
            (None, None)
        };
        let fmt_delta = |d: Option<(f64, f64)>| -> String {
            d.map(|(r, _)| format!("{:+.1}%", r * 100.0)).unwrap_or_else(|| "-".into())
        };
        // 判定只看选定指标：p50（默认，稳定）或 p99（专用环境）
        // 需同时满足：相对劣化 > threshold 且 绝对劣化 > min_abs_us
        let judged = if metric == "p99" { d99 } else { d50 };
        let verdict = match judged {
            Some((rel, abs)) if rel > threshold && abs > min_abs_us => {
                all_ok = false;
                "FAIL"
            }
            Some(_) => "PASS",
            None => "-",
        };
        println!(
            "{:<16} {:>11.2} {:>11.2} {:>11} {:>11} {:>6}",
            name,
            s.p50,
            s.p99,
            fmt_delta(d50),
            fmt_delta(d99),
            verdict
        );
    }
    all_ok
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut save_path: Option<String> = None;
    let mut baseline_path: Option<String> = None;
    let mut threshold = DEFAULT_THRESHOLD;
    let mut min_abs_us = DEFAULT_MIN_ABS_US;
    let mut metric = String::from("p50");
    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "--save-baseline" => {
                i += 1;
                save_path = args.get(i).cloned();
            }
            "--baseline" => {
                i += 1;
                baseline_path = args.get(i).cloned();
            }
            "--threshold" => {
                i += 1;
                threshold = args
                    .get(i)
                    .and_then(|s| s.parse().ok())
                    .unwrap_or(DEFAULT_THRESHOLD);
            }
            "--metric" => {
                i += 1;
                if let Some(m) = args.get(i) {
                    if m == "p50" || m == "p99" {
                        metric = m.clone();
                    } else {
                        eprintln!("--metric 只接受 p50 或 p99，忽略: {}", m);
                    }
                }
            }
            "--min-abs" => {
                i += 1;
                min_abs_us = args
                    .get(i)
                    .and_then(|s| s.parse().ok())
                    .unwrap_or(DEFAULT_MIN_ABS_US);
            }
            _ => {}
        }
        i += 1;
    }

    println!(
        "=== SCENE-02 数据层基准（{} 行 F64，纳秒时间轴，{} 轮）===",
        ROWS, ROUNDS
    );
    let results = unsafe { run_all() };

    // 读取基线（若指定）
    let baseline_doc = baseline_path.as_ref().and_then(|p| match std::fs::read_to_string(p) {
        Ok(s) => match serde_json::from_str::<Value>(&s) {
            Ok(v) => Some(v),
            Err(e) => {
                eprintln!("基线 JSON 解析失败: {}", e);
                None
            }
        },
        Err(e) => {
            eprintln!("读取基线失败: {}", e);
            None
        }
    });
    let base_metrics = baseline_doc.as_ref().and_then(|v| v.get("metrics").and_then(|m| m.as_object()));

    let all_ok = print_table(&results, base_metrics, threshold, min_abs_us, &metric);

    // 保存基线（若指定）
    if let Some(p) = save_path {
        let mut metrics = Map::new();
        for (name, s) in &results {
            metrics.insert(
                (*name).to_string(),
                json!({ "p50_us": s.p50, "p99_us": s.p99 }),
            );
        }
        let doc = json!({
            "schema": 1,
            "note": "SCENE-02 数据层基线（纳秒口径，1M 行 F64，100Hz）",
            "rows": ROWS,
            "period_ns": PERIOD_NS,
            "rounds": ROUNDS,
            "metrics": metrics,
        });
        std::fs::write(&p, serde_json::to_string_pretty(&doc).unwrap()).unwrap();
        println!("基线已保存: {}", p);
    }

    if baseline_path.is_some() && !all_ok {
        eprintln!(
            "回归判定失败：至少一个指标 {} 劣化超过 {:.0}%",
            metric,
            threshold * 100.0
        );
        std::process::exit(1);
    }
}
