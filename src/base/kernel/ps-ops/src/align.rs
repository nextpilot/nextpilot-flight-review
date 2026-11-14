//! 时间对齐：把异步采样的通道对齐到统一时间栅格（M2 才接主线，先落最小实现）。

/// 最近邻对齐：对每个目标时刻取源序列中时间最近的值。
pub fn nearest(src_t: &[f64], src_v: &[f64], dst_t: &[f64]) -> Vec<f64> {
    let mut out = Vec::with_capacity(dst_t.len());
    let mut j = 0usize;
    for &td in dst_t {
        while j + 1 < src_t.len() && (src_t[j + 1] - td).abs() <= (td - src_t[j]).abs() {
            j += 1;
        }
        out.push(if src_t.is_empty() { f64::NAN } else { src_v[j] });
    }
    out
}

/// 线性插值；超出源区间取边界值，源为空返回 NaN。
pub fn linear(src_t: &[f64], src_v: &[f64], dst_t: &[f64]) -> Vec<f64> {
    if src_t.is_empty() {
        return vec![f64::NAN; dst_t.len()];
    }
    let mut out = Vec::with_capacity(dst_t.len());
    let mut j = 0usize;
    for &td in dst_t {
        while j + 1 < src_t.len() && src_t[j + 1] < td {
            j += 1;
        }
        if td <= src_t[0] {
            out.push(src_v[0]);
        } else if j + 1 >= src_t.len() {
            out.push(*src_v.last().unwrap());
        } else {
            let (t0, t1) = (src_t[j], src_t[j + 1]);
            let (v0, v1) = (src_v[j], src_v[j + 1]);
            let k = if t1 > t0 { (td - t0) / (t1 - t0) } else { 0.0 };
            out.push(v0 + (v1 - v0) * k);
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn nearest_picks_closest() {
        let t = [0.0, 1.0, 2.0];
        let v = [10.0, 20.0, 30.0];
        assert_eq!(nearest(&t, &v, &[0.4, 1.6]), vec![10.0, 30.0]);
    }

    #[test]
    fn linear_interpolates_midpoint() {
        let t = [0.0, 2.0];
        let v = [0.0, 100.0];
        let out = linear(&t, &v, &[1.0]);
        assert!((out[0] - 50.0).abs() < 1e-9);
    }
}
