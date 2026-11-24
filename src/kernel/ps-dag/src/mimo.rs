//! MIMO 精确时间戳 join（BJ7）。
//!
//! pj IncrementalEngine 的 MIMO 节点只有在 N 路输入于**同一时间戳**都到齐才触发
//! （ARCHITECTURE:184-185）。我们照此实现：按时间戳对齐取交集，缺任一路就不产出。
//!
//! 判定口径说明：DAG 的 `Series.t` 是 f64 秒（不是 NDF 的 i64 纳秒），跨来源的
//! 「精确相等」很脆弱，所以默认按**位模式相等**比较（同一时间戳同一表示即相等），
//! 需要容忍浮点误差时可给 `params.eps`。跨来源数据请保证时间戳表示一致或显式给 eps。
//!
//! 多输入运算放在本模块而不是 ps-ops：ps-ops 的算子签名是单路 `(t, v)`，把二元
//! 算子塞进去会破坏其「一元算子库」定位。

use crate::Series;
use serde_json::Value;

/// 对齐口径：eps <= 0 时按位相等，否则按绝对差。
fn same_time(a: f64, b: f64, eps: f64) -> bool {
    if eps > 0.0 {
        (a - b).abs() <= eps
    } else {
        a.to_bits() == b.to_bits()
    }
}

fn eps_of(params: &Value) -> f64 {
    params.get("eps").and_then(|v| v.as_f64()).unwrap_or(0.0)
}

/// N 路对齐：返回公共时间戳（取首路的时间）与各路命中的行号。
///
/// 各路被视为按时间非降（NDF 主表单调，DAG 输出继承输入顺序）。游标法逐轮推进
/// 最落后的那几路，任一路耗尽即结束——缺任一路的时间点不会产出，符合精确 join。
fn align(inputs: &[&Series], eps: f64) -> (Vec<f64>, Vec<Vec<usize>>) {
    let n = inputs.len();
    let mut idx = vec![0usize; n];
    let mut out_t = Vec::new();
    let mut out_idx: Vec<Vec<usize>> = vec![Vec::new(); n];
    if n == 0 {
        return (out_t, out_idx);
    }

    loop {
        // 任一路耗尽：剩余路再也不可能凑齐。
        if (0..n).any(|i| idx[i] >= inputs[i].t.len()) {
            break;
        }
        let tmax = (0..n).fold(f64::NEG_INFINITY, |m, i| m.max(inputs[i].t[idx[i]]));
        if (0..n).all(|i| same_time(inputs[i].t[idx[i]], tmax, eps)) {
            // 全部对齐：产出一行，各路同时前进。
            out_t.push(inputs[0].t[idx[0]]);
            for i in 0..n {
                out_idx[i].push(idx[i]);
                idx[i] += 1;
            }
        } else {
            // 有路落后于 tmax：推进所有「明显落后」的路，至少一路保证终止。
            let mut moved = false;
            for i in 0..n {
                let t = inputs[i].t[idx[i]];
                if !same_time(t, tmax, eps) && t < tmax {
                    idx[i] += 1;
                    moved = true;
                }
            }
            if !moved {
                // 理论上不可达（前面已排除全等）；防御性跳出，避免死循环。
                break;
            }
        }
    }
    (out_t, out_idx)
}

/// 执行一次 MIMO 节点。
///
/// 支持的 `filter_id`：
/// - `join_equal` / `join`：对齐后取首路的值
/// - `join_combine`：对齐后按 `params.op`（`add`/`sub`/`mul`/`div`，默认 `add`）
///   对两路做逐点运算
/// - `join_count`：对齐后输出 1.0（用于「N 路同时到达」的事件计数）
pub fn apply_multi(filter_id: &str, inputs: &[&Series], params: &Value) -> Result<Series, String> {
    if inputs.len() < 2 {
        return Err(format!("MIMO 节点需要至少 2 路输入，实际 {}", inputs.len()));
    }
    let eps = eps_of(params);
    let (t, idx) = align(inputs, eps);
    match filter_id {
        "join_equal" | "join" => {
            let v = idx[0].iter().map(|&i| inputs[0].v[i]).collect();
            Ok(Series { t, v })
        }
        "join_combine" => {
            let op = params.get("op").and_then(|v| v.as_str()).unwrap_or("add");
            if !matches!(op, "add" | "sub" | "mul" | "div") {
                return Err(format!("join_combine 不支持 op={op}"));
            }
            let (a, b) = (&idx[0], &idx[1]);
            let v = a
                .iter()
                .zip(b.iter())
                .map(|(&ia, &ib)| {
                    let (x, y) = (inputs[0].v[ia], inputs[1].v[ib]);
                    match op {
                        "add" => x + y,
                        "sub" => x - y,
                        "mul" => x * y,
                        "div" => {
                            if y == 0.0 {
                                f64::NAN
                            } else {
                                x / y
                            }
                        }
                        _ => unreachable!("op 已预校验"),
                    }
                })
                .collect();
            Ok(Series { t, v })
        }
        "join_count" => Ok(Series { t, v: vec![1.0; idx[0].len()] }),
        other => Err(format!("MIMO 节点不支持算子 {other}")),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn s(t: &[f64], v: &[f64]) -> Series {
        Series { t: t.to_vec(), v: v.to_vec() }
    }

    #[test]
    fn only_matching_timestamps_produce_rows() {
        // a: 0,1,2,3   b: 1,2,4 —— 交集只有 1,2。
        let a = s(&[0.0, 1.0, 2.0, 3.0], &[10.0, 11.0, 12.0, 13.0]);
        let b = s(&[1.0, 2.0, 4.0], &[21.0, 22.0, 24.0]);
        let out = apply_multi("join_equal", &[&a, &b], &json!({})).unwrap();
        assert_eq!(out.t, vec![1.0, 2.0]);
        assert_eq!(out.v, vec![11.0, 12.0]);
    }

    #[test]
    fn no_common_timestamp_yields_empty() {
        let a = s(&[0.0, 1.0], &[1.0, 2.0]);
        let b = s(&[5.0, 6.0], &[3.0, 4.0]);
        let out = apply_multi("join_equal", &[&a, &b], &json!({})).unwrap();
        assert!(out.t.is_empty() && out.v.is_empty());
    }

    #[test]
    fn duplicate_timestamps_are_skipped_not_repeated() {
        // b 的 1.0 出现两次：只取第一次，避免笛卡尔积爆炸。
        let a = s(&[0.0, 1.0, 2.0], &[0.0, 1.0, 2.0]);
        let b = s(&[1.0, 1.0, 2.0], &[9.0, 9.0, 8.0]);
        let out = apply_multi("join_equal", &[&a, &b], &json!({})).unwrap();
        assert_eq!(out.t, vec![1.0, 2.0]);
        assert_eq!(out.v, vec![1.0, 2.0]);
    }

    #[test]
    fn three_way_join_requires_all_inputs() {
        let a = s(&[0.0, 1.0, 2.0], &[1.0, 1.0, 1.0]);
        let b = s(&[1.0, 2.0, 3.0], &[2.0, 2.0, 2.0]);
        let c = s(&[2.0, 3.0], &[3.0, 3.0]);
        let out = apply_multi("join_equal", &[&a, &b, &c], &json!({})).unwrap();
        assert_eq!(out.t, vec![2.0], "三路只有 2.0 同时到达");
    }

    #[test]
    fn combine_arithmetic_is_pointwise_on_aligned_rows() {
        let a = s(&[0.0, 1.0, 2.0], &[1.0, 2.0, 3.0]);
        let b = s(&[1.0, 2.0, 3.0], &[10.0, 20.0, 30.0]);
        let add = apply_multi("join_combine", &[&a, &b], &json!({"op": "add"})).unwrap();
        assert_eq!(add.t, vec![1.0, 2.0]);
        assert_eq!(add.v, vec![12.0, 23.0]);

        let sub = apply_multi("join_combine", &[&a, &b], &json!({"op": "sub"})).unwrap();
        assert_eq!(sub.v, vec![-8.0, -17.0]);

        let mul = apply_multi("join_combine", &[&a, &b], &json!({"op": "mul"})).unwrap();
        assert_eq!(mul.v, vec![20.0, 60.0]);

        let div = apply_multi("join_combine", &[&a, &b], &json!({"op": "div"})).unwrap();
        assert_eq!(div.v, vec![0.2, 0.15]);
    }

    #[test]
    fn division_by_zero_yields_nan_not_panic() {
        let a = s(&[1.0], &[5.0]);
        let b = s(&[1.0], &[0.0]);
        let out = apply_multi("join_combine", &[&a, &b], &json!({"op": "div"})).unwrap();
        assert!(out.v[0].is_nan());
    }

    #[test]
    fn eps_allows_near_timestamps_to_join() {
        // 严格逐位：0.1+0.2 与 0.3 位模式不同，不 join。
        let a = s(&[0.1 + 0.2], &[1.0]);
        let b = s(&[0.3], &[2.0]);
        assert!(apply_multi("join_equal", &[&a, &b], &json!({})).unwrap().t.is_empty());
        // 给 eps 后可以对齐。
        let out = apply_multi("join_equal", &[&a, &b], &json!({"eps": 1e-9})).unwrap();
        assert_eq!(out.t.len(), 1);
    }

    #[test]
    fn mismatched_lengths_terminate_at_shorter_input() {
        let a = s(&[0.0, 1.0, 2.0, 3.0], &[0.0; 4]);
        let b = s(&[0.0, 1.0], &[0.0; 2]);
        let out = apply_multi("join_equal", &[&a, &b], &json!({})).unwrap();
        assert_eq!(out.t, vec![0.0, 1.0]);
    }

    #[test]
    fn empty_input_yields_empty_without_panic() {
        let a = s(&[], &[]);
        let b = s(&[1.0], &[1.0]);
        let out = apply_multi("join_equal", &[&a, &b], &json!({})).unwrap();
        assert!(out.t.is_empty());
    }

    #[test]
    fn single_input_is_rejected() {
        let a = s(&[0.0], &[1.0]);
        assert!(apply_multi("join_equal", &[&a], &json!({})).is_err());
    }

    #[test]
    fn unknown_filter_is_rejected() {
        let a = s(&[0.0], &[1.0]);
        let b = s(&[0.0], &[2.0]);
        assert!(apply_multi("absolute", &[&a, &b], &json!({})).is_err());
    }

    #[test]
    fn join_count_counts_aligned_rows() {
        let a = s(&[0.0, 1.0, 2.0], &[0.0; 3]);
        let b = s(&[1.0, 2.0], &[0.0; 2]);
        let out = apply_multi("join_count", &[&a, &b], &json!({})).unwrap();
        assert_eq!(out.t, vec![1.0, 2.0]);
        assert_eq!(out.v, vec![1.0, 1.0]);
    }
}
