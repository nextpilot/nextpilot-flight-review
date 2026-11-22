//! 单通道统计：一次遍历出 count/mean/std/min/max/rms，中位数单独走排序。
//! NaN 与 inf 视为缺失点，不参与任何累加，也不计入 count。

pub struct Stats {
    pub count: u32,
    pub mean: f64,
    pub std_dev: f64,
    pub min: f64,
    pub max: f64,
    pub rms: f64,
    pub median: f64,
}

impl Stats {
    pub fn empty() -> Self {
        Self { count: 0, mean: f64::NAN, std_dev: f64::NAN, min: f64::NAN, max: f64::NAN, rms: f64::NAN, median: f64::NAN }
    }
}

/// `with_median` 为真时多一次 O(n log n) 排序，千万点量级约百毫秒。
pub fn series(v: &[f64], with_median: bool) -> Stats {
    let mut st = Stats::empty();
    let mut sum = 0.0f64;
    let mut sum_sq = 0.0f64;
    let mut n = 0u32;
    let mut lo = f64::INFINITY;
    let mut hi = f64::NEG_INFINITY;
    for &x in v {
        if !x.is_finite() {
            continue;
        }
        n += 1;
        sum += x;
        sum_sq += x * x;
        lo = lo.min(x);
        hi = hi.max(x);
    }
    if n == 0 {
        return st;
    }
    st.count = n;
    st.mean = sum / n as f64;
    // 总体方差；样本方差在 ABI 侧不做区分，控制台按总体报
    let var = (sum_sq / n as f64) - st.mean * st.mean;
    st.std_dev = var.max(0.0).sqrt();
    st.min = lo;
    st.max = hi;
    st.rms = (sum_sq / n as f64).sqrt();
    if with_median {
        let mut buf: Vec<f64> = v.iter().copied().filter(|x| x.is_finite()).collect();
        buf.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
        let m = buf.len();
        st.median = if m % 2 == 1 { buf[m / 2] } else { (buf[m / 2 - 1] + buf[m / 2]) / 2.0 };
    }
    st
}

/// 分位数：`probs` 里的每个分位点（0..1）输出一个值，线性插值口径
/// （q = p*(n-1)，相邻两值按小数部分插值）。NaN/inf 不参与；样本空 → NaN。
/// 排序一次 O(n log n)，之后每个分位点 O(1)。
pub fn quantiles(v: &[f64], probs: &[f64]) -> Vec<f64> {
    let mut buf: Vec<f64> = v.iter().copied().filter(|x| x.is_finite()).collect();
    buf.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
    let n = buf.len();
    probs
        .iter()
        .map(|&p| {
            if n == 0 || !p.is_finite() {
                return f64::NAN;
            }
            let p = p.clamp(0.0, 1.0);
            let pos = p * (n - 1) as f64;
            let lo = pos.floor() as usize;
            let hi = (lo + 1).min(n - 1);
            let frac = pos - lo as f64;
            buf[lo] + (buf[hi] - buf[lo]) * frac
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn basic_moments() {
        let st = series(&[1.0, 2.0, 3.0, 4.0], true);
        assert_eq!(st.count, 4);
        assert!((st.mean - 2.5).abs() < 1e-12);
        assert!((st.min - 1.0).abs() < 1e-12);
        assert!((st.max - 4.0).abs() < 1e-12);
        assert!((st.median - 2.5).abs() < 1e-12);
        // 总体方差 = 1.25
        assert!((st.std_dev - 1.25f64.sqrt()).abs() < 1e-12);
        assert!((st.rms - (30.0f64 / 4.0).sqrt()).abs() < 1e-12);
    }

    #[test]
    fn skips_non_finite() {
        let st = series(&[1.0, f64::NAN, 3.0, f64::INFINITY], true);
        assert_eq!(st.count, 2);
        assert!((st.mean - 2.0).abs() < 1e-12);
    }

    #[test]
    fn empty_input() {
        let st = series(&[], false);
        assert_eq!(st.count, 0);
        assert!(st.mean.is_nan());
    }

    #[test]
    fn odd_median() {
        let st = series(&[3.0, 1.0, 2.0], true);
        assert!((st.median - 2.0).abs() < 1e-12);
    }

    // ===== M2.7.9 分位数 =====

    #[test]
    fn quantiles_linear_interpolation() {
        let v = [1.0f64, 2.0, 3.0, 4.0];
        let q = quantiles(&v, &[0.0, 0.5, 1.0]);
        assert!((q[0] - 1.0).abs() < 1e-12);
        assert!((q[1] - 2.5).abs() < 1e-12);
        assert!((q[2] - 4.0).abs() < 1e-12);
        // P95：pos = 0.95*3 = 2.85 → 3 + 0.85*(4-3) = 3.85
        let q = quantiles(&v, &[0.95]);
        assert!((q[0] - 3.85).abs() < 1e-12);
    }

    #[test]
    fn quantiles_skip_non_finite_and_clamp() {
        let q = quantiles(&[1.0, f64::NAN, 3.0, f64::INFINITY, -1.0e308 * 10.0], &[0.5, 2.0, -1.0]);
        assert!((q[0] - 2.0).abs() < 1e-12); // 有效样本只剩 [1,3]
        assert!((q[1] - 3.0).abs() < 1e-12); // clamp 到 1.0
        assert!((q[2] - 1.0).abs() < 1e-12); // clamp 到 0.0
    }

    #[test]
    fn quantiles_empty_input() {
        let q = quantiles(&[], &[0.5, 0.99]);
        assert!(q[0].is_nan() && q[1].is_nan());
    }
}
