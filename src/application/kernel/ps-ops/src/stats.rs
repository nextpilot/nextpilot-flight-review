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
}
