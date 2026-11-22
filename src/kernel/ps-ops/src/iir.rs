//! 巴特沃斯 IIR：按二阶节（SOS）级联，二阶节的 Q 取巴特沃斯极点分布。
//! 只做正向滤波，不补偿相位（要零相位请在调用侧前后各滤一次）。

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    LowPass,
    HighPass,
    BandPass,
}

impl Kind {
    pub fn from_code(code: i32) -> Self {
        match code {
            1 => Kind::HighPass,
            2 => Kind::BandPass,
            _ => Kind::LowPass,
        }
    }
}

/// 一节二阶 IIR 的系数（已归一化 a0=1）。
#[derive(Clone, Copy)]
struct Sos {
    b0: f64,
    b1: f64,
    b2: f64,
    a1: f64,
    a2: f64,
}

/// 巴特沃斯第 k 个二阶节的 Q；order 为总阶数（取偶数）。
fn butter_q(k: usize, order: usize, band: bool) -> f64 {
    if band {
        // 带通只需一节、Q 由带宽决定，调用侧另算
        return 1.0;
    }
    let n = (order / 2) as f64;
    let theta = std::f64::consts::PI * (2.0 * k as f64 + 1.0) / (2.0 * n);
    1.0 / (2.0 * theta.sin())
}

fn lowpass_sos(fc: f64, fs: f64, q: f64) -> Sos {
    let w0 = 2.0 * std::f64::consts::PI * fc / fs;
    let (sin, cos) = w0.sin_cos();
    let alpha = sin / (2.0 * q);
    let a0 = 1.0 + alpha;
    Sos {
        b0: (1.0 - cos) / 2.0 / a0,
        b1: (1.0 - cos) / a0,
        b2: (1.0 - cos) / 2.0 / a0,
        a1: -2.0 * cos / a0,
        a2: (1.0 - alpha) / a0,
    }
}

fn highpass_sos(fc: f64, fs: f64, q: f64) -> Sos {
    let w0 = 2.0 * std::f64::consts::PI * fc / fs;
    let (sin, cos) = w0.sin_cos();
    let alpha = sin / (2.0 * q);
    let a0 = 1.0 + alpha;
    Sos {
        b0: (1.0 + cos) / 2.0 / a0,
        b1: -(1.0 + cos) / a0,
        b2: (1.0 + cos) / 2.0 / a0,
        a1: -2.0 * cos / a0,
        a2: (1.0 - alpha) / a0,
    }
}

fn bandpass_sos(f_low: f64, f_high: f64, fs: f64, q: f64) -> Sos {
    let w0 = 2.0 * std::f64::consts::PI * ((f_low + f_high) / 2.0) / fs;
    let (sin, cos) = w0.sin_cos();
    let alpha = sin / (2.0 * q);
    let a0 = 1.0 + alpha;
    Sos {
        b0: alpha / a0,
        b1: 0.0,
        b2: -alpha / a0,
        a1: -2.0 * cos / a0,
        a2: (1.0 - alpha) / a0,
    }
}

fn run_sos(x: &[f64], s: Sos) -> Vec<f64> {
    let mut y = Vec::with_capacity(x.len());
    let (mut x1, mut x2) = (0.0f64, 0.0f64);
    let (mut y1, mut y2) = (0.0f64, 0.0f64);
    for &xn in x {
        if !xn.is_finite() {
            // 缺失点保持上一拍输出，状态不推进，避免 NaN 进反馈
            y.push(y1);
            continue;
        }
        let yn = s.b0 * xn + s.b1 * x1 + s.b2 * x2 - s.a1 * y1 - s.a2 * y2;
        x2 = x1;
        x1 = xn;
        y2 = y1;
        y1 = yn;
        y.push(yn);
    }
    y
}

/// 低通/高通：`order` 取偶数，奇数自动进一位；`fc` 需 < fs/2。
/// 带通：`fc` 是中心频率，`order` 解释为带宽比例（0.05..1），只用一节。
pub fn butterworth(v: &[f64], fs: f64, fc: f64, order: usize, kind: Kind, bandwidth: f64) -> Vec<f64> {
    if v.is_empty() || fs <= 0.0 || fc <= 0.0 {
        return Vec::new();
    }
    let nyq = fs / 2.0;
    let fc = fc.min(nyq * 0.999);
    match kind {
        Kind::BandPass => {
            let bw = bandwidth.clamp(0.01, 1.0);
            let half = fc * bw / 2.0;
            let lo = (fc - half).max(1e-9);
            let hi = (fc + half).min(nyq * 0.999);
            let q = fc / (hi - lo).max(1e-9);
            run_sos(v, bandpass_sos(lo, hi, fs, q))
        }
        _ => {
            let ord = if order % 2 == 1 { order + 1 } else { order }.max(2);
            let mut y: Vec<f64> = v.to_vec();
            for k in 0..ord / 2 {
                let q = butter_q(k, ord, false);
                let s = match kind {
                    Kind::LowPass => lowpass_sos(fc, fs, q),
                    _ => highpass_sos(fc, fs, q),
                };
                y = run_sos(&y, s);
            }
            y
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::f64::consts::PI;

    fn sine(n: usize, f: f64, fs: f64) -> Vec<f64> {
        (0..n).map(|i| (2.0 * PI * f * i as f64 / fs).sin()).collect()
    }

    fn rms(x: &[f64]) -> f64 {
        let from = x.len() / 2; // 丢掉启动瞬态
        let s: f64 = x[from..].iter().map(|v| v * v).sum();
        (s / (x.len() - from) as f64).sqrt()
    }

    #[test]
    fn lowpass_keeps_passband_attenuates_stopband() {
        let fs = 1000.0;
        let pass = sine(4000, 10.0, fs);
        let stop = sine(4000, 300.0, fs);
        let yp = butterworth(&pass, fs, 50.0, 4, Kind::LowPass, 0.1);
        let ys = butterworth(&stop, fs, 50.0, 4, Kind::LowPass, 0.1);
        let rp = rms(&yp);
        let rs = rms(&ys);
        // 10 Hz 在通带，幅值应基本不变（0.707 ≈ 1/√2）
        assert!((rp - 0.707).abs() < 0.05, "通带 rms={rp}");
        // 300 Hz 在阻带，4 阶至少衰减 40 dB
        assert!(rs / rp < 0.01, "阻带比={}", rs / rp);
    }

    #[test]
    fn highpass_mirror() {
        let fs = 1000.0;
        let pass = sine(4000, 300.0, fs);
        let stop = sine(4000, 10.0, fs);
        let yp = butterworth(&pass, fs, 50.0, 4, Kind::HighPass, 0.1);
        let ys = butterworth(&stop, fs, 50.0, 4, Kind::HighPass, 0.1);
        assert!(rms(&yp) > 0.5);
        assert!(rms(&ys) / rms(&yp) < 0.01);
    }

    #[test]
    fn stable_on_dc_input() {
        let y = butterworth(&[1.0; 1000], 1000.0, 100.0, 4, Kind::LowPass, 0.1);
        assert!(y.iter().all(|x| x.is_finite()));
        assert!((y[999] - 1.0).abs() < 0.01);
    }

    #[test]
    fn empty_and_invalid() {
        assert!(butterworth(&[], 1000.0, 50.0, 2, Kind::LowPass, 0.1).is_empty());
        assert!(butterworth(&[0.0; 10], 0.0, 50.0, 2, Kind::LowPass, 0.1).is_empty());
    }
}
