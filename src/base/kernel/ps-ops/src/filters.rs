//! 12 个 PJ4 内置过滤器，批处理版本。
//! 语义对齐 `pj4/resources/filters/builtin_filters.luau`；`suppressed` 行不产出。

use serde_json::Value;

fn num(p: &Value, key: &str, d: f64) -> f64 {
    p.get(key).and_then(|v| v.as_f64()).unwrap_or(d)
}

fn boolean(p: &Value, key: &str, d: bool) -> bool {
    p.get(key).and_then(|v| v.as_bool()).unwrap_or(d)
}

fn int(p: &Value, key: &str, d: usize) -> usize {
    p.get(key).and_then(|v| v.as_u64()).map(|v| v as usize).unwrap_or(d).max(1)
}

fn bits(p: &Value, key: &str, d: i64) -> i64 {
    p.get(key).and_then(|v| v.as_i64()).or_else(|| p.get(key).and_then(|v| v.as_str()).and_then(|s| s.parse().ok())).unwrap_or(d)
}

/// 返回 (t_out, v_out)。首样本被抑制的算子会让输出比输入短 1。
pub fn apply(id: &str, t: &[f64], v: &[f64], p: &Value) -> Result<(Vec<f64>, Vec<f64>), String> {
    let n = t.len().min(v.len());
    let mut ot = Vec::with_capacity(n);
    let mut ov = Vec::with_capacity(n);
    match id {
        "none" => {
            ot.extend_from_slice(&t[..n]);
            ov.extend_from_slice(&v[..n]);
        }
        "absolute" => {
            ot.extend_from_slice(&t[..n]);
            ov.extend(v[..n].iter().map(|x| x.abs()));
        }
        "scale" => {
            let k = num(p, "value_scale", 1.0);
            let b = num(p, "value_offset", 0.0);
            let dt = num(p, "time_offset_sec", 0.0);
            ot.extend(t[..n].iter().map(|x| x + dt));
            ov.extend(v[..n].iter().map(|x| k * x + b));
        }
        "derivative" => {
            let use_fixed = boolean(p, "use_custom_dt", false);
            let fixed = num(p, "custom_dt", 1.0);
            for i in 1..n {
                let dt = if use_fixed { fixed } else { t[i] - t[i - 1] };
                ot.push(t[i]);
                ov.push(if dt != 0.0 { (v[i] - v[i - 1]) / dt } else { 0.0 });
            }
        }
        "integral" => {
            let use_fixed = boolean(p, "use_custom_dt", false);
            let fixed = num(p, "custom_dt", 1.0);
            let mut acc = 0.0f64;
            for i in 1..n {
                let dt = if use_fixed { fixed } else { t[i] - t[i - 1] };
                acc += (v[i] + v[i - 1]) * dt / 2.0;
                ot.push(t[i]);
                ov.push(acc);
            }
        }
        "moving_average" => {
            let w = int(p, "window", 100);
            let comp = boolean(p, "compensate_time_offset", false);
            let mut bt = Vec::with_capacity(w);
            let mut bv = Vec::with_capacity(w);
            for i in 0..n {
                bt.push(t[i]);
                bv.push(v[i]);
                if bt.len() > w {
                    bt.remove(0);
                    bv.remove(0);
                }
                let total = v[i] * (w - bv.len()) as f64 + bv.iter().sum::<f64>();
                let out_t = if comp && bt.len() > 1 {
                    bt[0] + (bt[bt.len() - 1] - bt[0]) / 2.0
                } else {
                    t[i]
                };
                ot.push(out_t);
                ov.push(total / w as f64);
            }
        }
        "moving_rms" => {
            let w = int(p, "window", 100);
            let mut buf = Vec::with_capacity(w);
            for i in 0..n {
                buf.push(v[i]);
                if buf.len() > w {
                    buf.remove(0);
                }
                let total = v[i] * v[i] * (w - buf.len()) as f64 + buf.iter().map(|x| x * x).sum::<f64>();
                ot.push(t[i]);
                ov.push((total / w as f64).sqrt());
            }
        }
        "moving_variance" => {
            let w = int(p, "window", 100);
            let std_dev = boolean(p, "std_dev", false);
            let mut buf = Vec::with_capacity(w);
            for i in 0..n {
                buf.push(v[i]);
                if buf.len() > w {
                    buf.remove(0);
                }
                let pad = (w - buf.len()) as f64;
                let total = v[i] * pad + buf.iter().sum::<f64>();
                let avg = total / w as f64;
                let sqr = (v[i] - avg) * (v[i] - avg) * pad
                    + buf.iter().map(|x| (x - avg) * (x - avg)).sum::<f64>();
                let var = sqr / w as f64;
                ot.push(t[i]);
                ov.push(if std_dev { var.sqrt() } else { var });
            }
        }
        "outlier_removal" => {
            let factor = num(p, "outlier_factor", 100.0);
            let mut rt = Vec::with_capacity(4);
            let mut rv = Vec::with_capacity(4);
            for i in 0..n {
                rt.push(t[i]);
                rv.push(v[i]);
                if rt.len() > 4 {
                    rt.remove(0);
                    rv.remove(0);
                }
                if rt.len() < 4 {
                    ot.push(t[i]);
                    ov.push(v[i]);
                    continue;
                }
                let (y0, y1, y2, y3) = (rv[0], rv[1], rv[2], rv[3]);
                let (d1, d2) = (y1 - y2, y2 - y3);
                let outlier = if d1 * d2 < 0.0 {
                    let d0 = y0 - y1;
                    let jump = d1.abs().max(d2.abs());
                    let ratio = if d0 == 0.0 { f64::INFINITY } else { jump / d0.abs() };
                    ratio > factor
                } else {
                    false
                };
                if !outlier {
                    ot.push(rt[2]);
                    ov.push(rv[2]);
                }
            }
        }
        "samples_counter" => {
            let ms = num(p, "samples_ms", 1000.0).max(0.0);
            let win = ms * 1e-3;
            let mut q = std::collections::VecDeque::new();
            for i in 0..n {
                q.push_back(t[i]);
                let min_t = t[i] - win;
                while let Some(front) = q.front() {
                    if *front < min_t {
                        q.pop_front();
                    } else {
                        break;
                    }
                }
                ot.push(t[i]);
                ov.push(q.len() as f64);
            }
        }
        "binary_filter" => {
            let op = bits(p, "binary_op", 3);
            let a = num(p, "binary_a", 0.0);
            let b = num(p, "binary_b", 0.0);
            let tol = num(p, "binary_tol", 1e-9);
            for i in 0..n {
                let x = v[i];
                let pass = match op {
                    0 => (x - a).abs() <= tol,
                    1 => x < a,
                    2 => x <= a,
                    3 => x > a,
                    4 => x >= a,
                    5 => a <= x && x <= b,
                    _ => false,
                };
                ot.push(t[i]);
                ov.push(if pass { 1.0 } else { 0.0 });
            }
        }
        "time_since_previous" => {
            for i in 1..n {
                ot.push(t[i]);
                ov.push(t[i] - t[i - 1]);
            }
        }
        other => return Err(format!("未知过滤器：{other}")),
    }
    Ok((ot, ov))
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn derivative_and_integral_suppress_first() {
        let t = [0.0, 1.0, 2.0];
        let v = [0.0, 2.0, 4.0];
        let (_, d) = apply("derivative", &t, &v, &json!({})).unwrap();
        assert_eq!(d, vec![2.0, 2.0]);
        let (_, i) = apply("integral", &t, &v, &json!({})).unwrap();
        assert_eq!(i, vec![1.0, 4.0]);
    }

    #[test]
    fn moving_average_pads_with_current() {
        let t = [0.0, 1.0, 2.0, 3.0];
        let v = [0.0, 0.0, 3.0, 3.0];
        let (_, y) = apply("moving_average", &t, &v, &json!({"window": 2})).unwrap();
        assert_eq!(y, vec![0.0, 0.0, 1.5, 3.0]);
    }

    #[test]
    fn binary_filter_threshold() {
        let t = [0.0, 1.0];
        let v = [1.0, 3.0];
        let (_, y) = apply("binary_filter", &t, &v, &json!({"binary_op": 3, "binary_a": 2})).unwrap();
        assert_eq!(y, vec![0.0, 1.0]);
    }


    #[test]
    fn all_basic_ops_golden() {
        let t = [0.0, 1.0, 2.0, 3.0];
        let v = [-1.0, 0.0, 3.0, 4.0];
        let (_, none) = apply("none", &t, &v, &json!({})).unwrap();
        assert_eq!(none, vec![-1.0, 0.0, 3.0, 4.0]);
        let (_, abs) = apply("absolute", &t, &v, &json!({})).unwrap();
        assert_eq!(abs, vec![1.0, 0.0, 3.0, 4.0]);
        let (ot, sc) = apply("scale", &t, &v,
            &json!({"value_scale":2.0,"value_offset":10.0,"time_offset_sec":1.0})).unwrap();
        assert_eq!(ot, vec![1.0, 2.0, 3.0, 4.0]);
        assert_eq!(sc, vec![8.0, 10.0, 16.0, 18.0]);
        let (_, ma) = apply("moving_average", &t, &v, &json!({"window":2})).unwrap();
        assert_eq!(ma, vec![-1.0, -0.5, 1.5, 3.5]);
        let (_, rms) = apply("moving_rms", &t, &v, &json!({"window":2})).unwrap();
        assert!((rms[0] - 1.0).abs() < 1e-9 && (rms[1] - 0.7071067811865476).abs() < 1e-9);
        assert!((rms[2] - 2.1213203435596424).abs() < 1e-9 && (rms[3] - 3.5355339059327378).abs() < 1e-9);
        let (_, var) = apply("moving_variance", &t, &v, &json!({"window":2})).unwrap();
        assert_eq!(var, vec![0.0, 0.25, 2.25, 0.25]);
        let (_, std) = apply("moving_variance", &t, &v,
            &json!({"window":2,"std_dev":true})).unwrap();
        assert_eq!(std, vec![0.0, 0.5, 1.5, 0.5]);
        let (_, cnt) = apply("samples_counter", &t, &v, &json!({"samples_ms":2000})).unwrap();
        assert_eq!(cnt, vec![1.0, 2.0, 3.0, 3.0]);
        let (_, bin) = apply("binary_filter", &t, &v,
            &json!({"binary_op":5,"binary_a":0.0,"binary_b":3.0})).unwrap();
        assert_eq!(bin, vec![0.0, 1.0, 1.0, 0.0]);
        let (_, dts) = apply("time_since_previous", &t, &v, &json!({})).unwrap();
        assert_eq!(dts, vec![1.0, 1.0, 1.0]);
    }

    #[test]
    fn unknown_filter_errors() {
        assert!(apply("nope", &[0.0], &[1.0], &json!({})).is_err());
    }
}
