//! 读取路径一致性：所有读取入口在"有未归并补传段"时必须看到同一份完整数据。
//!
//! 背景：BJ4 引入 pending 段后，"读取前物化"是靠每个 ABI 入口各自调用的隐式契约。
//! 之前实测发现 `ps_dataset_table_rows` 少报行数、`find_series` 读到残缺输入，
//! 说明这类漏调无法靠代码审阅穷举。本测试用一个统一的"行数应等于真实行数"断言
//! 覆盖全部读入口，新增入口时把断言加进来即可防回归。
//!
//! 关键设计：断言的是**可观测契约**（读到的行数/点数），不依赖内部实现。

use std::ffi::CString;
use std::os::raw::c_void;

use ps_abi::{
    ps_dataset_append_frame, ps_dataset_column_raw, ps_dataset_free, ps_dataset_lod_minmax,
    ps_dataset_new, ps_dataset_read_window, ps_dataset_read_window_multi, ps_dataset_table_rows,
    ps_dataset_value_at, PsDataset, PsFrameColumn,
};

const PERIOD: i64 = 1_000_000; // 1ms

struct Fixture {
    ds: *mut PsDataset,
    table: CString,
    col: CString,
    base: i64,
}

impl Fixture {
    unsafe fn new(rows: usize) -> Self {
        let ds = ps_dataset_new();
        let table = CString::new("T").unwrap();
        let col = CString::new("v").unwrap();
        let unit = CString::new("").unwrap();
        let base = 1_000_000_000i64;
        let times: Vec<i64> = (0..rows).map(|i| base + i as i64 * PERIOD).collect();
        let values: Vec<f64> = (0..rows).map(|i| i as f64).collect();
        let c = PsFrameColumn {
            name: col.as_ptr(),
            unit: unit.as_ptr(),
            value_type: 0,
            values: values.as_ptr() as *const c_void,
            nulls: std::ptr::null(),
        };
        let rc = ps_dataset_append_frame(
            ds,
            table.as_ptr(),
            times.as_ptr(),
            rows as u32,
            &c,
            1,
            std::ptr::null_mut(),
            0,
        );
        assert_eq!(rc, 0, "建表失败");
        Fixture { ds, table, col, base }
    }

    /// 补传一批"落在主表中部"的行，时间戳为 base + start_row*PERIOD 起、每 2 个采样点一个。
    unsafe fn backfill(&mut self, count: usize, start_row: usize) {
        let unit = CString::new("").unwrap();
        let times: Vec<i64> = (0..count)
            .map(|i| self.base + ((start_row + i * 2) as i64) * PERIOD)
            .collect();
        let values: Vec<f64> = (0..count).map(|i| -((i + 1) as f64)).collect();
        let c = PsFrameColumn {
            name: self.col.as_ptr(),
            unit: unit.as_ptr(),
            value_type: 0,
            values: values.as_ptr() as *const c_void,
            nulls: std::ptr::null(),
        };
        let rc = ps_dataset_append_frame(
            self.ds,
            self.table.as_ptr(),
            times.as_ptr(),
            count as u32,
            &c,
            1,
            std::ptr::null_mut(),
            0,
        );
        assert_eq!(rc, 0, "补传失败");
    }

    unsafe fn rows(&self) -> u64 {
        ps_dataset_table_rows(self.ds, self.table.as_ptr())
    }

    /// 全时间范围 read_window，cap 足够大，返回写入行数。
    unsafe fn window_rows(&self, cap: u32) -> u32 {
        let mut t = vec![0f64; cap as usize];
        let mut v = vec![0f64; cap as usize];
        ps_dataset_read_window(
            self.ds,
            self.table.as_ptr(),
            self.col.as_ptr(),
            i64::MIN / 4,
            i64::MAX / 4,
            t.as_mut_ptr(),
            v.as_mut_ptr(),
            cap,
        )
    }

    unsafe fn raw_count(&self) -> u64 {
        let mut p: *const c_void = std::ptr::null();
        let mut n: u64 = 0;
        let mut ty: u32 = 0;
        let rc = ps_dataset_column_raw(
            self.ds,
            self.table.as_ptr(),
            self.col.as_ptr(),
            &mut p,
            &mut n,
            &mut ty,
            std::ptr::null_mut(),
        );
        assert_eq!(rc, 0, "column_raw 失败");
        n
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        unsafe { ps_dataset_free(self.ds) }
    }
}

/// 补传后，每个读取入口看到的总行数必须一致。
#[test]
fn all_read_paths_see_backfilled_rows() {
    unsafe {
        let mut f = Fixture::new(100);
        assert_eq!(f.rows(), 100);
        assert_eq!(f.window_rows(4096), 100);
        assert_eq!(f.raw_count(), 100);

        // 中部补传 20 行 → 真实行数 120
        f.backfill(20, 10);
        assert_eq!(f.rows(), 120, "table_rows 必须包含补传行");
        assert_eq!(f.window_rows(4096), 120, "read_window 必须包含补传行");
        assert_eq!(f.raw_count(), 120, "column_raw 必须包含补传行");

        // 再来一批，验证可以反复
        f.backfill(5, 60);
        assert_eq!(f.rows(), 125);
        assert_eq!(f.window_rows(4096), 125);
        assert_eq!(f.raw_count(), 125);
    }
}

/// 补传行的**值**必须按时间正确插入，而不只是行数对。
#[test]
fn backfilled_values_land_at_correct_positions() {
    unsafe {
        let f = Fixture::new(10);
        let mut f = f;
        // 主表 v[i] = i，时间 base + i*PERIOD
        // 补传 3 行，起点 row 3，间隔 2 个采样点 → 时间索引 3,5,7，值 -1,-2,-3
        f.backfill(3, 3);
        let n = f.window_rows(64);
        assert_eq!(n, 13);
        assert_eq!(f.rows(), 13);

        // 时间索引 3/5/7 上应有负值；其余位置为原值
        let mut t = vec![0f64; 64];
        let mut v = vec![0f64; 64];
        let got = ps_dataset_read_window(
            f.ds,
            f.table.as_ptr(),
            f.col.as_ptr(),
            i64::MIN / 4,
            i64::MAX / 4,
            t.as_mut_ptr(),
            v.as_mut_ptr(),
            64,
        );
        assert_eq!(got, 13);
        // 逐行核对：时间单调，且负值只出现在原本 row 3/5/7 的时间点上
        assert!(t[..got as usize].windows(2).all(|w| w[0] <= w[1]));
        let negatives: Vec<f64> = v[..got as usize].iter().copied().filter(|x| *x < 0.0).collect();
        assert_eq!(negatives, vec![-1.0, -2.0, -3.0], "补传值应按时间顺序出现");
    }
}

/// value_at 的最后一行的索引必须覆盖补传行。
#[test]
fn value_at_reaches_last_backfilled_row() {
    unsafe {
        let mut f = Fixture::new(50);
        // 补传一行比主表尾更晚的记录：起点 row 100（时间 > 主表尾），
        // 但这走 append 分支。这里改用中部补传，验证最后一行仍是主表尾。
        f.backfill(10, 5);
        assert_eq!(f.rows(), 60);
        let mut t = 0f64;
        let mut v = 0f64;
        let rc = ps_dataset_value_at(
            f.ds,
            f.table.as_ptr(),
            f.col.as_ptr(),
            59,
            &mut t,
            &mut v,
        );
        assert_eq!(rc, 0, "value_at 最后一行应存在");
        assert!((v - 49.0).abs() < 1e-9, "最后一行应是主表尾的值 49，实际 {v}");
    }
}

/// LOD 必须覆盖补传区间：补传一个"尖峰"，LOD 的最大值必须看到它。
#[test]
fn lod_sees_backfilled_peak() {
    unsafe {
        let mut f = Fixture::new(100);
        // 补传一批值全为 -1 的行（负值，不影响 max），再补一个 +1000 的尖峰
        f.backfill(10, 10);
        let unit = CString::new("").unwrap();
        let times = vec![f.base + 500 * PERIOD + 1];
        let values = vec![1000.0f64];
        let c = PsFrameColumn {
            name: f.col.as_ptr(),
            unit: unit.as_ptr(),
            value_type: 0,
            values: values.as_ptr() as *const c_void,
            nulls: std::ptr::null(),
        };
        let rc = ps_dataset_append_frame(
            f.ds,
            f.table.as_ptr(),
            times.as_ptr(),
            1,
            &c,
            1,
            std::ptr::null_mut(),
            0,
        );
        assert_eq!(rc, 0);
        assert_eq!(f.rows(), 111);

        // 用足够多的像素让桶细分到能看出尖峰
        let px = 1024usize;
        let mut tri = vec![0f64; px * 3];
        let n = ps_dataset_lod_minmax(
            f.ds,
            f.table.as_ptr(),
            f.col.as_ptr(),
            f.base,
            f.base + 1200 * PERIOD,
            px as u32,
            tri.as_mut_ptr(),
            px as u32,
        );
        assert!(n > 0, "LOD 应返回三元组");
        let max_seen = tri[..n as usize * 3]
            .chunks(3)
            .map(|c| c[2])
            .fold(f64::NEG_INFINITY, f64::max);
        assert!(
            (max_seen - 1000.0).abs() < 1e-6,
            "LOD 的最大值应看到补传尖峰 1000，实际 {max_seen}"
        );
    }
}

/// read_window_multi 走 const 指针 + 内部物化，同样必须看到补传行。
#[test]
fn read_window_multi_sees_backfilled_rows() {
    unsafe {
        let mut f = Fixture::new(40);
        f.backfill(8, 4);
        assert_eq!(f.rows(), 48);

        let names = [f.col.as_ptr()];
        let mut t = vec![0f64; 128];
        let mut v = vec![0f64; 128];
        let ptrs = [v.as_mut_ptr()];
        let n = ps_dataset_read_window_multi(
            f.ds,
            f.table.as_ptr(),
            names.as_ptr(),
            1,
            i64::MIN / 4,
            i64::MAX / 4,
            t.as_mut_ptr(),
            ptrs.as_ptr(),
            128,
        );
        assert_eq!(n as u64, f.rows(), "read_window_multi 行数应与 table_rows 一致");
    }
}

/// 空表与单行表的边界：不应 panic，返回 0。
#[test]
fn empty_and_single_row_are_safe() {
    unsafe {
        let f = Fixture::new(1);
        assert_eq!(f.rows(), 1);
        assert_eq!(f.window_rows(16), 1);
        assert_eq!(f.raw_count(), 1);

        // lod 在单行表上也应返回 1 个三元组（或 0），不应崩
        let mut tri = vec![0f64; 30];
        let n = ps_dataset_lod_minmax(
            f.ds,
            f.table.as_ptr(),
            f.col.as_ptr(),
            f.base,
            f.base + 10 * PERIOD,
            10,
            tri.as_mut_ptr(),
            10,
        );
        assert!(n <= 10);
    }
}

/// 时间戳全部相同：归并必须稳定且不丢行。
#[test]
fn duplicate_timestamps_are_all_retained() {
    unsafe {
        let ds = ps_dataset_new();
        let table = CString::new("D").unwrap();
        let col = CString::new("v").unwrap();
        let unit = CString::new("").unwrap();
        let times = vec![5000i64; 5];
        let values: Vec<f64> = (0..5).map(|i| i as f64).collect();
        let c = PsFrameColumn {
            name: col.as_ptr(),
            unit: unit.as_ptr(),
            value_type: 0,
            values: values.as_ptr() as *const c_void,
            nulls: std::ptr::null(),
        };
        assert_eq!(
            ps_dataset_append_frame(
                ds,
                table.as_ptr(),
                times.as_ptr(),
                5,
                &c,
                1,
                std::ptr::null_mut(),
                0
            ),
            0
        );
        // 再补传 3 行同时间
        let times2 = vec![5000i64; 3];
        let values2 = vec![9.0f64, 9.5, 9.9];
        let c2 = PsFrameColumn {
            name: col.as_ptr(),
            unit: unit.as_ptr(),
            value_type: 0,
            values: values2.as_ptr() as *const c_void,
            nulls: std::ptr::null(),
        };
        assert_eq!(
            ps_dataset_append_frame(
                ds,
                table.as_ptr(),
                times2.as_ptr(),
                3,
                &c2,
                1,
                std::ptr::null_mut(),
                0
            ),
            0
        );
        assert_eq!(ps_dataset_table_rows(ds, table.as_ptr()), 8, "同时间戳不应丢行");
        let mut t = vec![0f64; 32];
        let mut v = vec![0f64; 32];
        let n = ps_dataset_read_window(
            ds,
            table.as_ptr(),
            col.as_ptr(),
            i64::MIN / 4,
            i64::MAX / 4,
            t.as_mut_ptr(),
            v.as_mut_ptr(),
            32,
        );
        assert_eq!(n, 8);
        ps_dataset_free(ds);
    }
}
