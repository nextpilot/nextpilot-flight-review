//! 抗混叠抽取：先窗函数法设计线性相位低通 FIR，再按 factor 取点。
//! 输出时间轴同步抽取，长度约为 n/factor（去掉滤波器延迟后对齐）。

/// `factor` 越大，FIR 越长以维持同样的归一化过渡带；这里取 8·factor+1 抽头。
pub fn decimate(t: &[f64], v: &[f64], factor: usize) -> (Vec<f64>, Vec<f64>) {
    let factor = factor.max(1);
    let n = t.len().min(v.len());
    if n == 0 {
        return (Vec::new(), Vec::new());
    }
    if factor == 1 {
        return (t[..n].to_vec(), v[..n].to_vec());
    }

    // 截止取新奈奎斯特的 0.8，Hamming 窗
    let taps = (8 * factor + 1).max(3);
    let half = taps / 2;
    let fc = 0.4 / factor as f64; // 归一化到 Nyquist=0.5 → 0.8 × (0.5/factor)
    let mut h = vec![0.0f64; taps];
    let mut hsum = 0.0f64;
    for i in 0..taps {
        let m = i as f64 - half as f64;
        let sinc = if m == 0.0 {
            2.0 * fc
        } else {
            (2.0 * std::f64::consts::PI * fc * m).sin() / (std::f64::consts::PI * m)
        };
        let w = 0.54 - 0.46 * (2.0 * std::f64::consts::PI * i as f64 / (taps - 1) as f64).cos();
        h[i] = sinc * w;
        hsum += h[i];
    }
    // 归一化到直流增益 1
    if hsum.abs() > 0.0 {
        for c in h.iter_mut() {
            *c /= hsum;
        }
    }

    let mut ot = Vec::with_capacity(n / factor + 1);
    let mut ov = Vec::with_capacity(n / factor + 1);
    let mut i = 0usize;
    while i < n {
        let mut acc = 0.0f64;
        for k in 0..taps {
            let idx = i as isize + k as isize - half as isize;
            if idx >= 0 && (idx as usize) < n {
                let x = v[idx as usize];
                if x.is_finite() {
                    acc += x * h[k];
                }
            }
        }
        ot.push(t[i]);
        ov.push(acc);
        i += factor;
    }
    (ot, ov)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::f64::consts::PI;

    fn sine(n: usize, f: f64, fs: f64) -> Vec<f64> {
        (0..n).map(|i| (2.0 * PI * f * i as f64 / fs).sin()).collect()
    }

    #[test]
    fn factor_one_is_identity() {
        let t = [0.0, 1.0, 2.0];
        let v = [1.0, 2.0, 3.0];
        let (ot, ov) = decimate(&t, &v, 1);
        assert_eq!(ot, t.to_vec());
        assert_eq!(ov, v.to_vec());
    }

    #[test]
    fn output_length_and_time_axis() {
        let n = 1000;
        let fs = 1000.0;
        let t: Vec<f64> = (0..n).map(|i| i as f64 / fs).collect();
        let v = sine(n, 10.0, fs);
        let (ot, ov) = decimate(&t, &v, 4);
        assert_eq!(ot.len(), ov.len());
        assert!(ot.len() >= n / 4);
        assert!((ot[1] - ot[0] - 4.0 / fs).abs() < 1e-9);
    }

    #[test]
    fn keeps_low_frequency_amplitude() {
        let n = 4000;
        let fs = 1000.0;
        let t: Vec<f64> = (0..n).map(|i| i as f64 / fs).collect();
        let v = sine(n, 5.0, fs);
        let (_, ov) = decimate(&t, &v, 8);
        let from = ov.len() / 2;
        let s: f64 = ov[from..].iter().map(|x| x * x).sum::<f64>() / (ov.len() - from) as f64;
        assert!((s.sqrt() - 0.707).abs() < 0.1, "rms={}", s.sqrt());
    }

    #[test]
    fn attenuates_alias_band() {
        // 400 Hz 在 500 Hz 采样下会混叠到 100 Hz，必须被低通压掉
        let n = 4000;
        let fs = 1000.0;
        let t: Vec<f64> = (0..n).map(|i| i as f64 / fs).collect();
        let v = sine(n, 400.0, fs);
        let (_, ov) = decimate(&t, &v, 4);
        let from = ov.len() / 2;
        let rms = (ov[from..].iter().map(|x| x * x).sum::<f64>() / (ov.len() - from) as f64).sqrt();
        assert!(rms < 0.05, "混叠残留 rms={rms}");
    }

    #[test]
    fn empty_input() {
        assert!(decimate(&[], &[], 4).0.is_empty());
    }
}
