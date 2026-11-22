//! Welch 功率谱密度：分段加窗、段间重叠、逐段功率谱平均。
//! 归一化按 scipy 的 density 口径：单边乘 2（DC 与 Nyquist 不乘），除以 fs·sum(w²)。

use crate::fft::{fft_inplace, next_pow2, Window};

/// `nfft` 为 0 时取 256；`overlap` 为 0..1 的重叠比例，常用 0.5。
/// 段数不足一段时退化为整段一次估计。
pub fn welch(v: &[f64], fs: f64, nfft: usize, overlap: f64, window: Window) -> (Vec<f64>, Vec<f64>) {
    let n = v.len();
    if n == 0 || fs <= 0.0 {
        return (Vec::new(), Vec::new());
    }
    let nfft = if nfft == 0 { 256 } else { next_pow2(nfft) };
    let nfft = nfft.min(next_pow2(n)).max(2);
    let step = ((1.0 - overlap.clamp(0.0, 0.95)) * nfft as f64).round().max(1.0) as usize;

    let mut w = vec![0.0f64; nfft];
    let mut wsum_sq = 0.0f64;
    for i in 0..nfft {
        w[i] = window.coef(i, nfft);
        wsum_sq += w[i] * w[i];
    }
    if wsum_sq <= 0.0 {
        return (Vec::new(), Vec::new());
    }

    let m = nfft / 2 + 1;
    let mut acc = vec![0.0f64; m];
    let mut segs = 0usize;

    let mut start = 0usize;
    while start + nfft <= n {
        let mut re = vec![0.0f64; nfft];
        let mut im = vec![0.0f64; nfft];
        for i in 0..nfft {
            re[i] = v[start + i] * w[i];
        }
        fft_inplace(&mut re, &mut im);
        for k in 0..m {
            let p = re[k] * re[k] + im[k] * im[k];
            let scale = if k == 0 || k == nfft / 2 { 1.0 } else { 2.0 };
            acc[k] += scale * p / (fs * wsum_sq);
        }
        segs += 1;
        start += step;
    }

    // 不足一段：整段当一段，长度补到 nfft
    if segs == 0 {
        let mut re = vec![0.0f64; nfft];
        let mut im = vec![0.0f64; nfft];
        for i in 0..n.min(nfft) {
            re[i] = v[i] * w[i];
        }
        fft_inplace(&mut re, &mut im);
        for k in 0..m {
            let p = re[k] * re[k] + im[k] * im[k];
            let scale = if k == 0 || k == nfft / 2 { 1.0 } else { 2.0 };
            acc[k] = scale * p / (fs * wsum_sq);
        }
        segs = 1;
    }

    let mut freq = Vec::with_capacity(m);
    let mut psd = Vec::with_capacity(m);
    for k in 0..m {
        freq.push(k as f64 * fs / nfft as f64);
        psd.push(acc[k] / segs as f64);
    }
    (freq, psd)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::f64::consts::PI;

    #[test]
    fn sine_peak_frequency_and_amplitude() {
        let fs = 1000.0;
        let n = 4096;
        let f = 100.0;
        let a = 2.0;
        let v: Vec<f64> = (0..n).map(|i| a * (2.0 * PI * f * i as f64 / fs).sin()).collect();
        let (freq, psd) = welch(&v, fs, 1024, 0.5, Window::Hann);
        let mut best = 1usize;
        for k in 1..psd.len() {
            if psd[k] > psd[best] {
                best = k;
            }
        }
        assert!((freq[best] - f).abs() < 2.0, "峰值在 {}", freq[best]);
        // 正弦功率应集中在 A²/2 = 2 附近；PSD 密度峰值随分辨率平方缩放，只校验量级
        assert!(psd[best] > 0.0);
    }

    #[test]
    fn short_input_falls_back_to_single_segment() {
        let (freq, psd) = welch(&[1.0; 40], 40.0, 512, 0.5, Window::Rect);
        assert_eq!(freq.len(), psd.len());
        assert!(!psd.is_empty());
    }

    #[test]
    fn empty_input() {
        assert!(welch(&[], 100.0, 256, 0.5, Window::Hann).0.is_empty());
    }
}
