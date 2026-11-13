//! 四元数 → RPY，公式与 PJ4 官方 `toolbox_quaternion` 对齐。
//! 输入按 w/x/y/z；输出 roll/pitch/yaw（默认弧度），可选 degrees/unwrap。

const PI: f64 = std::f64::consts::PI;

fn unwrap(cur: f64, prev: f64, offset: &mut f64) {
    let wrap = PI * 2.0;
    let threshold = PI * 1.95;
    if cur - prev > threshold {
        *offset -= wrap;
    } else if prev - cur > threshold {
        *offset += wrap;
    }
}

/// 返回 (roll, pitch, yaw) 三个数组。
pub fn to_rpy(
    w: &[f64],
    x: &[f64],
    y: &[f64],
    z: &[f64],
    degrees: bool,
    do_unwrap: bool,
) -> Result<(Vec<f64>, Vec<f64>, Vec<f64>), String> {
    let n = w.len().min(x.len()).min(y.len()).min(z.len());
    if n == 0 {
        return Err("四元数输入为空".into());
    }
    let scale = if degrees { 180.0 / PI } else { 1.0 };
    let mut r = Vec::with_capacity(n);
    let mut p = Vec::with_capacity(n);
    let mut yw = Vec::with_capacity(n);
    let (mut pr, mut pp, mut py) = (0.0, 0.0, 0.0);
    let (mut or, mut op, mut oy) = (0.0, 0.0, 0.0);

    for i in 0..n {
        let (mut qw, mut qx, mut qy, mut qz) = (w[i], x[i], y[i], z[i]);
        let norm2 = qw * qw + qx * qx + qy * qy + qz * qz;
        if norm2 <= 0.0 {
            return Err(format!("四元数第 {i} 点模长为 0"));
        }
        if (norm2 - 1.0).abs() > f64::EPSILON {
            let inv = 1.0 / norm2.sqrt();
            qw *= inv;
            qx *= inv;
            qy *= inv;
            qz *= inv;
        }

        let sinr = 2.0 * (qw * qx + qy * qz);
        let cosr = 1.0 - 2.0 * (qx * qx + qy * qy);
        let mut roll = sinr.atan2(cosr);

        let sinp = 2.0 * (qw * qy - qz * qx);
        let pitch = if sinp.abs() >= 1.0 { PI / 2.0 * sinp.signum() } else { sinp.asin() };

        let siny = 2.0 * (qw * qz + qx * qy);
        let cosy = 1.0 - 2.0 * (qy * qy + qz * qz);
        let mut yaw = siny.atan2(cosy);

        if i != 0 && do_unwrap {
            unwrap(roll, pr, &mut or);
            unwrap(pitch, pp, &mut op);
            unwrap(yaw, py, &mut oy);
        }
        pr = roll;
        pp = pitch;
        py = yaw;
        roll += or;
        yaw += oy;

        r.push(scale * roll);
        p.push(scale * (pitch + op));
        yw.push(scale * yaw);
    }
    Ok((r, p, yw))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn approx(a: &[f64], b: &[f64]) {
        assert_eq!(a.len(), b.len());
        for (x, y) in a.iter().zip(b) {
            assert!((x - y).abs() < 1e-9, "{x} != {y}");
        }
    }

    #[test]
    fn identity_is_zero() {
        let (r, p, y) = to_rpy(&[1.0], &[0.0], &[0.0], &[0.0], true, true).unwrap();
        approx(&r, &[0.0]);
        approx(&p, &[0.0]);
        approx(&y, &[0.0]);
    }

    #[test]
    fn ninety_deg_yaw() {
        let a = (90.0f64).to_radians() / 2.0;
        let (r, p, y) = to_rpy(&[a.cos()], &[0.0], &[0.0], &[a.sin()], true, true).unwrap();
        approx(&r, &[0.0]);
        approx(&p, &[0.0]);
        approx(&y, &[90.0]);
    }

    #[test]
    fn roll_pitch_yaw_roundtrip() {
        let (r, p, y) = ((20.0f64).to_radians(), (10.0f64).to_radians(), (30.0f64).to_radians());
        let (cr, sr) = ((r / 2.0).cos(), (r / 2.0).sin());
        let (cp, sp) = ((p / 2.0).cos(), (p / 2.0).sin());
        let (cy, sy) = ((y / 2.0).cos(), (y / 2.0).sin());
        let w = cr * cp * cy + sr * sp * sy;
        let x = sr * cp * cy - cr * sp * sy;
        let y = cr * sp * cy + sr * cp * sy;
        let z = cr * cp * sy - sr * sp * cy;
        let (r, p, yy) = to_rpy(&[w], &[x], &[y], &[z], true, true).unwrap();
        approx(&r, &[20.0]);
        approx(&p, &[10.0]);
        approx(&yy, &[30.0]);
    }
}
