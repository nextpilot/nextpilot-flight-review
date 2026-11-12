//! NDF（NextPilot Data Format）：单通道时间序列的内存表示。
//! 设计约束：t 与 v 分列存储（列式），f32/f64 双精度由 `Precision` 区分。

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Precision {
    F32,
    F64,
}

/// 一条通道的原始样本。`t` 用 f64 单调递增（秒），`v` 存 f64 便于统一算子。
#[derive(Debug, Clone, Default)]
pub struct Series {
    pub name: String,
    pub unit: String,
    pub t: Vec<f64>,
    pub v: Vec<f64>,
}

impl Series {
    pub fn new(name: impl Into<String>) -> Self {
        Self { name: name.into(), ..Default::default() }
    }

    pub fn push(&mut self, t: f64, v: f64) {
        self.t.push(t);
        self.v.push(v);
    }

    pub fn len(&self) -> usize {
        self.t.len()
    }

    pub fn is_empty(&self) -> bool {
        self.t.is_empty()
    }

    /// 返回 (min, max)；空序列返回 None。
    pub fn min_max(&self) -> Option<(f64, f64)> {
        let mut iter = self.v.iter().copied().filter(|x| x.is_finite());
        let first = iter.next()?;
        let mut lo = first;
        let mut hi = first;
        for x in iter {
            if x < lo {
                lo = x;
            }
            if x > hi {
                hi = x;
            }
        }
        Some((lo, hi))
    }

    /// 抽稀到每 `bucket` 个样本一个点（min/max 保形），M0 先做等距抽稀。
    pub fn downsample(&self, bucket: usize) -> Vec<(f64, f64)> {
        let bucket = bucket.max(1);
        if bucket == 1 {
            return self.t.iter().copied().zip(self.v.iter().copied()).collect();
        }
        self.t
            .chunks(bucket)
            .zip(self.v.chunks(bucket))
            .map(|(ts, vs)| {
                let t = ts[0];
                let mut lo = f64::INFINITY;
                let mut hi = f64::NEG_INFINITY;
                for v in vs.iter().copied().filter(|x| x.is_finite()) {
                    lo = lo.min(v);
                    hi = hi.max(v);
                }
                (t, if lo.is_finite() { (lo + hi) / 2.0 } else { f64::NAN })
            })
            .collect()
    }
}

/// 视口 LOD：把 [t0,t1] 内样本按像素分桶，每桶输出 (中点时间, min, max)。
/// 降采样必须在 Rust 侧完成（§11 铁律 3），UI 只拿 ≤px 个三元组去画。
pub fn lod_minmax(t: &[f64], v: &[f64], t0: f64, t1: f64, px: usize) -> Vec<(f64, f64, f64)> {
    let px = px.max(1);
    if t.is_empty() || v.is_empty() || !(t1 > t0) {
        return Vec::new();
    }
    let n = t.len().min(v.len());
    let start = t.partition_point(|x| *x < t0);
    let span = t1 - t0;

    let mut out: Vec<(f64, f64, f64)> = Vec::with_capacity(px);
    let mut cur_bucket = usize::MAX;
    for i in start..n {
        let ti = t[i];
        if ti > t1 {
            break;
        }
        let b = (((ti - t0) / span) * px as f64).floor();
        let b = if b < 0.0 { 0 } else { (b as usize).min(px - 1) };
        let val = v[i];
        if b != cur_bucket {
            cur_bucket = b;
            out.push((t0 + span * (b as f64 + 0.5) / px as f64, val, val));
        } else if let Some(last) = out.last_mut() {
            if val.is_finite() {
                last.1 = last.1.min(val);
                last.2 = last.2.max(val);
            }
        }
    }
    out
}

/// 通道精度推断：用于 D11（f32/f64/kEnum/kString）的最小实现。
pub fn precision_of(v: &[f64]) -> Precision {
    if v.iter().all(|x| (*x as f32).is_finite() && (*x as f32) as f64 == *x) {
        Precision::F32
    } else {
        Precision::F64
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn push_and_range() {
        let mut s = Series::new("att.roll");
        s.push(0.0, 1.5);
        s.push(0.1, -2.0);
        assert_eq!(s.len(), 2);
        assert_eq!(s.min_max(), Some((-2.0, 1.5)));
    }

    #[test]
    fn empty_range_is_none() {
        assert!(Series::new("x").min_max().is_none());
    }

    #[test]
    fn lod_buckets_keep_extremes() {
        let mut s = Series::new("x");
        for i in 0..100 {
            let t = i as f64 * 0.1;
            s.push(t, if i % 2 == 0 { -1.0 } else { 1.0 });
        }
        let lod = lod_minmax(&s.t, &s.v, 0.0, 10.0, 10);
        assert_eq!(lod.len(), 10);
        for (_, lo, hi) in &lod {
            assert_eq!(*lo, -1.0);
            assert_eq!(*hi, 1.0);
        }
    }

    #[test]
    fn lod_empty_when_range_invalid() {
        assert!(lod_minmax(&[0.0], &[1.0], 1.0, 0.0, 10).is_empty());
    }

    #[test]
    fn downsample_keeps_bucket_midpoint() {
        let mut s = Series::new("x");
        for i in 0..8 {
            s.push(i as f64, if i % 2 == 0 { 0.0 } else { 10.0 });
        }
        let p = s.downsample(4);
        assert_eq!(p.len(), 2);
        assert!((p[0].1 - 5.0).abs() < 1e-9);
    }
}
