//! 补传路径随机化对拍（differential fuzz）。
//!
//! 现有 BJ4 测试都是固定用例（3 行 + 2 行补传），覆盖的是"能不能对"而不是
//! "在任意操作交错下都对"。本测试用固定种子的伪随机序列，把 append / backfill /
//! materialize / 加列 交错执行，每步与"全量按 (时间, 到达序) 稳定排序"的模型对拍。
//!
//! 关键点：模型自己维护到达序计数器，不读内核 `seq`，所以断言不依赖内部实现细节；
//! 同时因为是"到达序"，重复时间戳的先后也能被验证（这是归并最容易错的地方）。

use ps_ndf::typed::{Column, ColumnData, Dataset, NullBits};

/// xorshift64*：确定性、无依赖，便于失败复现。
struct Rng(u64);

impl Rng {
    fn new(seed: u64) -> Self {
        Rng(seed.wrapping_mul(0x9E37_79B9_7F4A_7C15) | 1)
    }
    fn next(&mut self) -> u64 {
        let mut x = self.0;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        self.0 = x;
        x.wrapping_mul(0x2545_F491_4F6C_DD1D)
    }
    fn below(&mut self, n: usize) -> usize {
        if n == 0 {
            0
        } else {
            (self.next() % n as u64) as usize
        }
    }
    fn range(&mut self, lo: i64, hi: i64) -> i64 {
        if hi <= lo {
            lo
        } else {
            lo + (self.next() % ((hi - lo) as u64 + 1)) as i64
        }
    }
}

/// 一行在模型里的记录：时间、到达序、各列值（f64；null 用 None）。
#[derive(Clone, Debug)]
struct ModelRow {
    t: i64,
    arrival: u64,
    vals: Vec<Option<f64>>,
}

/// 把一列 Option<f64> 转成 (ColumnData, NullBits)。
fn to_column(vals: &[Option<f64>]) -> (ColumnData, NullBits) {
    let data: Vec<f64> = vals.iter().map(|v| v.unwrap_or(f64::NAN)).collect();
    let mut nulls = NullBits::new(vals.len());
    for (i, v) in vals.iter().enumerate() {
        if v.is_none() {
            nulls.set_null(i, true);
        }
    }
    (ColumnData::F64(data), nulls)
}

fn check(ds: &mut Dataset, model: &[ModelRow], tag: &str) {
    let view = ds.read();
    let Some(t) = view.find_table("t") else {
        assert!(model.is_empty(), "{tag}: 表不存在但模型有 {} 行", model.len());
        return;
    };
    // 期望：按 (时间, 到达序) 稳定排序
    let mut expected = model.to_vec();
    expected.sort_by(|a, b| a.t.cmp(&b.t).then(a.arrival.cmp(&b.arrival)));

    assert_eq!(t.row_count(), expected.len(), "{tag}: 行数不一致");
    let got_t: Vec<i64> = t.time_ns.as_slice().to_vec();
    let exp_t: Vec<i64> = expected.iter().map(|r| r.t).collect();
    assert_eq!(got_t, exp_t, "{tag}: 时间轴不一致");

    // 时间轴必须单调非降（lower_bound/upper_bound 的前提）
    assert!(
        got_t.windows(2).all(|w| w[0] <= w[1]),
        "{tag}: 时间轴不单调"
    );

    for (ci, name) in ["a", "b"].iter().enumerate() {
        let Some(col) = t.column(name) else { continue };
        for (ri, exp) in expected.iter().enumerate() {
            let Some(exp_v) = exp.vals.get(ci).copied().flatten().filter(|v| !v.is_nan()) else {
                assert!(col.is_null(ri), "{tag}: {name}[{ri}] 期望 null 但非 null");
                continue;
            };
            assert!(!col.is_null(ri), "{tag}: {name}[{ri}] 期望非 null 但为 null");
            let got = col.value_as_f64(ri).expect("非 null 应有值");
            assert!(
                (got - exp_v).abs() < 1e-9,
                "{tag}: {name}[{ri}] 值不一致 got={got} exp={exp_v}"
            );
        }
    }
}

fn run_seed(seed: u64) {
    let mut rng = Rng::new(seed);
    let mut ds = Dataset::new();
    let mut model: Vec<ModelRow> = Vec::new();
    let mut arrival: u64 = 0;
    let mut next_t: i64 = 0; // 主表尾之后的下一个可用时间（保证 append 单调）
    let mut col_count = 1usize;

    for step in 0..40 {
        let tag = format!("seed={seed} step={step}");
        match rng.below(10) {
            // 0..=3 顺序追加
            0..=3 => {
                let rows = 1 + rng.below(6);
                let mut times = Vec::with_capacity(rows);
                let mut vals: Vec<Vec<Option<f64>>> = vec![Vec::with_capacity(rows)];
                for _ in 0..rows {
                    next_t += 1 + rng.below(5) as i64;
                    times.push(next_t);
                    vals[0].push(if rng.below(5) == 0 {
                        None
                    } else {
                        Some(rng.below(1000) as f64 * 0.5)
                    });
                }
                let cols: Vec<(String, String, ColumnData, NullBits)> = if col_count == 1 {
                    let (d, n) = to_column(&vals[0]);
                    vec![("a".into(), "".into(), d, n)]
                } else {
                    let (d, n) = to_column(&vals[0]);
                    let (d2, n2) = to_column(&vals[0].iter().map(|v| v.map(|x| x * 2.0)).collect::<Vec<_>>());
                    vec![("a".into(), "".into(), d, n), ("b".into(), "".into(), d2, n2)]
                };
                ds.append_frame("t", times.clone(), cols).expect(&tag);
                for (i, t) in times.iter().enumerate() {
                    let mut v = vec![vals[0][i]];
                    if col_count > 1 {
                        v.push(vals[0][i].map(|x| x * 2.0));
                    }
                    model.push(ModelRow { t: *t, arrival, vals: v });
                    arrival += 1;
                }
            }
            // 4..=6 补传（时间落在 [0, next_t) 内，内部有序）
            4..=6 => {
                if next_t <= 1 {
                    continue;
                }
                let rows = 1 + rng.below(6);
                let mut times: Vec<i64> = (0..rows)
                    .map(|_| rng.range(0, next_t - 1))
                    .collect();
                times.sort_unstable();
                let mut vals: Vec<Option<f64>> = Vec::with_capacity(rows);
                for _ in 0..rows {
                    vals.push(if rng.below(5) == 0 {
                        None
                    } else {
                        Some(rng.below(1000) as f64 * 0.5)
                    });
                }
                let cols: Vec<(String, String, ColumnData, NullBits)> = if col_count == 1 {
                    let (d, n) = to_column(&vals);
                    vec![("a".into(), "".into(), d, n)]
                } else {
                    let (d, n) = to_column(&vals);
                    let (d2, n2) = to_column(&vals.iter().map(|v| v.map(|x| x * 2.0)).collect::<Vec<_>>());
                    vec![("a".into(), "".into(), d, n), ("b".into(), "".into(), d2, n2)]
                };
                ds.append_frame("t", times.clone(), cols).expect(&tag);
                for (i, t) in times.iter().enumerate() {
                    let mut v = vec![vals[i]];
                    if col_count > 1 {
                        v.push(vals[i].map(|x| x * 2.0));
                    }
                    model.push(ModelRow { t: *t, arrival, vals: v });
                    arrival += 1;
                }
            }
            // 7..=8 模拟读取：触发物化
            7..=8 => {
                ds.materialize_table("t");
                check(&mut ds, &model, &tag);
            }
            // 9 加列（只在还没加过时）
            _ => {
                if col_count >= 2 || model.is_empty() {
                    continue;
                }
                // 加列要求列长 == 真实行数，先物化再取
                ds.materialize_table("t");
                let rows = ds.read().find_table("t").unwrap().row_count();
                let extra: Vec<Option<f64>> = (0..rows).map(|i| Some(i as f64 + 0.25)).collect();
                let (d, n) = to_column(&extra);
                let col = Column::with_nulls("b", "", d, n).expect("构造列失败");
                let ti = ds.table_index("t").unwrap();
                ds.table_mut(ti).unwrap().add_column(col).expect(&tag);
                // 新列的值按"物化后的物理行号"给出：先把模型排成物理序，再按位置赋。
                model.sort_by(|x, y| x.t.cmp(&y.t).then(x.arrival.cmp(&y.arrival)));
                for (i, r) in model.iter_mut().enumerate() {
                    r.vals.push(Some(i as f64 + 0.25));
                }
                col_count = 2;
                ds.materialize_table("t");
                check(&mut ds, &model, &tag);
                continue;
            }
        }
        if rng.below(3) == 0 {
            ds.materialize_table("t");
            check(&mut ds, &model, &tag);
        }
    }

    // 收尾：完整对拍
    ds.materialize_table("t");
    check(&mut ds, &model, &format!("seed={seed} final"));
    assert_eq!(
        ds.pending_stats("t").unwrap(),
        (0, 0),
        "seed={seed}: 物化后不应残留 pending 段"
    );
}

#[test]
fn backfill_fuzz_matches_full_sort_across_operation_interleavings() {
    for seed in 0..40u64 {
        run_seed(seed);
    }
}

#[test]
fn backfill_fuzz_table_shape_invariants() {
    // 附带不变量：时间轴单调、seq 唯一、列长一致。
    let mut ds = Dataset::new();
    let mut rng = Rng::new(7);
    let mut next_t = 0i64;
    let mut times_all = Vec::new();
    for _ in 0..15 {
        let rows = 1 + rng.below(5);
        let mut times = Vec::with_capacity(rows);
        let vals: Vec<Option<f64>> = (0..rows).map(|_| Some(rng.below(100) as f64)).collect();
        for _ in 0..rows {
            next_t += 1;
            times.push(next_t);
        }
        let (d, n) = to_column(&vals);
        let first = times_all.is_empty();
        if first {
            ds.append_frame("t", times.clone(), vec![("a".into(), "".into(), d, n)]).unwrap();
        } else {
            ds.append_frame("t", times.clone(), vec![("a".into(), "".into(), d, n)]).unwrap();
        }
        times_all.extend(times);
        // 随机补传
        if next_t > 3 {
            let bt = vec![rng.range(0, next_t - 1), rng.range(0, next_t - 1)];
            let mut bt = bt;
            bt.sort_unstable();
            let (d, n) = to_column(&[Some(1.0), Some(2.0)]);
            ds.append_frame("t", bt, vec![("a".into(), "".into(), d, n)]).unwrap();
        }
    }
    ds.materialize_table("t");
    let view = ds.read();
    let t = view.find_table("t").unwrap();
    assert!(t.time_ns.windows(2).all(|w| w[0] <= w[1]));
    let mut s = t.seq.clone();
    s.sort_unstable();
    let n = s.len();
    s.dedup();
    assert_eq!(s.len(), n, "seq 必须唯一");
    for c in &t.columns {
        assert_eq!(c.len(), t.row_count(), "列 {} 长度不一致", c.name);
    }
}
