//! 向量算子：全部成批、整块处理，禁止逐点进 Lua（见 D5 / §5.1）。
//! 这里只放 M0 需要的三个：scale / add / moving_avg；M1 过滤器在 filters。

pub mod filters;
pub mod quat;

pub fn scale(x: &[f32], k: f32) -> Vec<f32> {
    x.iter().map(|v| *v * k).collect()
}

pub fn add(a: &[f32], b: &[f32]) -> Vec<f32> {
    a.iter().zip(b.iter()).map(|(x, y)| *x + *y).collect()
}

/// 滑动平均；窗口 > 长度时退化为全长均值。NaN 透传，不参与累加。
pub fn moving_avg(x: &[f32], window: usize) -> Vec<f32> {
    let w = window.max(1).min(x.len().max(1));
    let mut out = Vec::with_capacity(x.len());
    let mut sum = 0.0f64;
    let mut cnt = 0usize;
    for i in 0..x.len() {
        if x[i].is_finite() {
            sum += x[i] as f64;
            cnt += 1;
        }
        if i >= w {
            let old = x[i - w];
            if old.is_finite() {
                sum -= old as f64;
                cnt -= 1;
            }
        }
        let n = cnt.min(w);
        out.push(if n == 0 { f32::NAN } else { (sum / n as f64) as f32 });
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn scale_and_add() {
        assert_eq!(scale(&[1.0, -2.0], 2.0), vec![2.0, -4.0]);
        assert_eq!(add(&[1.0, 2.0], &[3.0, 4.0]), vec![4.0, 6.0]);
    }

    #[test]
    fn moving_avg_smooths_step() {
        let x = [0.0, 0.0, 3.0, 3.0];
        let y = moving_avg(&x, 2);
        assert!((y[2] - 1.5).abs() < 1e-6);
        assert!((y[3] - 3.0).abs() < 1e-6);
    }
}
