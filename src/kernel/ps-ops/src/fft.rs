//! 实数 FFT：radix-2 迭代版，长度非 2 的幂时补零。
//! 只出单边幅度谱，幅值按窗归一化（正弦输入的峰值等于其幅值）。

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Window {
    Rect,
    Hann,
    Hamming,
    Blackman,
}

impl Window {
    pub fn from_code(code: i32) -> Self {
        match code {
            1 => Window::Hann,
            2 => Window::Hamming,
            3 => Window::Blackman,
            _ => Window::Rect,
        }
    }

    pub fn coef(self, i: usize, n: usize) -> f64 {
        if n <= 1 {
            return 1.0;
        }
        let x = i as f64 / (n - 1) as f64;
        match self {
            Window::Rect => 1.0,
            Window::Hann => 0.5 - 0.5 * (2.0 * std::f64::consts::PI * x).cos(),
            Window::Hamming => 0.54 - 0.46 * (2.0 * std::f64::consts::PI * x).cos(),
            Window::Blackman => 0.42 - 0.5 * (2.0 * std::f64::consts::PI * x).cos()
                + 0.08 * (4.0 * std::f64::consts::PI * x).cos(),
        }
    }
}

pub fn next_pow2(n: usize) -> usize {
    let mut m = 1usize;
    while m < n {
        m <<= 1;
    }
    m
}

/// 原位复数 FFT（Cooley-Tukey，基 2）。长度必须是 2 的幂。
pub fn fft_inplace(re: &mut [f64], im: &mut [f64]) {
    let n = re.len();
    if n <= 1 || n != im.len() {
        return;
    }
    debug_assert!(n.is_power_of_two());
    // 位反转置换
    let mut j = 0usize;
    for i in 1..n {
        let mut bit = n >> 1;
        while j & bit != 0 {
            j ^= bit;
            bit >>= 1;
        }
        j |= bit;
        if i < j {
            re.swap(i, j);
            im.swap(i, j);
        }
    }
    // 蝶形
    let mut len = 2usize;
    while len <= n {
        let ang = -2.0 * std::f64::consts::PI / len as f64;
        let (wr, wi) = (ang.cos(), ang.sin());
        let half = len / 2;
        let mut i = 0usize;
        while i < n {
            let (mut cr, mut ci) = (1.0f64, 0.0f64);
            for k in 0..half {
                let p = i + k;
                let q = p + half;
                let tr = re[q] * cr - im[q] * ci;
                let ti = re[q] * ci + im[q] * cr;
                re[q] = re[p] - tr;
                im[q] = im[p] - ti;
                re[p] += tr;
                im[p] += ti;
                let ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
            i += len;
        }
        len <<= 1;
    }
}

/// 单边幅度谱。返回 (freq, mag)，长度为 nfft/2+1。
pub fn amplitude(v: &[f64], fs: f64, window: Window) -> (Vec<f64>, Vec<f64>) {
    let n_in = v.len();
    if n_in == 0 || fs <= 0.0 {
        return (Vec::new(), Vec::new());
    }
    let n = next_pow2(n_in);
    let mut re = vec![0.0f64; n];
    let mut im = vec![0.0f64; n];
    let mut wsum = 0.0f64;
    for i in 0..n_in {
        let w = window.coef(i, n_in);
        wsum += w;
        re[i] = v[i] * w;
    }
    if wsum <= 0.0 {
        return (Vec::new(), Vec::new());
    }
    fft_inplace(&mut re, &mut im);

    let m = n / 2 + 1;
    let mut freq = Vec::with_capacity(m);
    let mut mag = Vec::with_capacity(m);
    for k in 0..m {
        let a = (re[k] * re[k] + im[k] * im[k]).sqrt();
        let scale = if k == 0 || k == n / 2 { 1.0 } else { 2.0 };
        freq.push(k as f64 * fs / n as f64);
        mag.push(scale * a / wsum);
    }
    (freq, mag)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::f64::consts::PI;

    #[test]
    fn sine_peak_at_expected_bin() {
        // 整数周期（64 个）才能让无窗峰值幅度精确等于 1，非整数周期必然泄漏
        let fs = 1024.0;
        let n = 1024;
        let f = 64.0;
        let v: Vec<f64> = (0..n).map(|i| (2.0 * PI * f * i as f64 / fs).sin()).collect();
        let (freq, mag) = amplitude(&v, fs, Window::Rect);
        let mut best = 0usize;
        for k in 1..mag.len() {
            if mag[k] > mag[best] {
                best = k;
            }
        }
        assert!((freq[best] - f).abs() < 1.0);
        // 整数周期、无窗时幅值应接近 1
        assert!((mag[best] - 1.0).abs() < 0.02);
    }

    #[test]
    fn dc_is_not_doubled() {
        let (_, mag) = amplitude(&[2.0; 64], 64.0, Window::Rect);
        assert!((mag[0] - 2.0).abs() < 1e-9);
    }

    #[test]
    fn non_power_of_two_is_padded() {
        let fs = 128.0;
        let v: Vec<f64> = (0..100).map(|i| (2.0 * PI * 16.0 * i as f64 / fs).sin()).collect();
        let (freq, mag) = amplitude(&v, fs, Window::Hann);
        let mut best = 0usize;
        for k in 1..mag.len() {
            if mag[k] > mag[best] {
                best = k;
            }
        }
        assert!((freq[best] - 16.0).abs() < 1.0);
    }

    #[test]
    fn parseval_energy() {
        // 无窗时 sum(|X|^2) 与 n*sum(x^2) 一致
        let n = 256;
        let v: Vec<f64> = (0..n).map(|i| (i as f64 * 0.37).sin()).collect();
        let mut re = v.clone();
        let mut im = vec![0.0; n];
        fft_inplace(&mut re, &mut im);
        let ex: f64 = re.iter().zip(im.iter()).map(|(a, b)| a * a + b * b).sum();
        let time: f64 = v.iter().map(|x| x * x).sum::<f64>() * n as f64;
        assert!((ex - time).abs() / time < 1e-9);
    }
}
