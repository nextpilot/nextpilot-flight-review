//! CAN 日志的帧级模型，ASC 与 BLF 共用一个出口。
//!
//! 帧 → 曲线这一步不做任何"信号语义"猜测：没有 DBC 就不知道哪几个字节拼成
//! 一个物理量，硬猜出来的曲线是假的。所以默认只给两类可验证的量：
//! - `CAN<ch>.<ID>.bN`：报文第 N 字节的原始值（0..255）
//! - `CAN<ch>.<ID>.period`：该 ID 相邻两帧的间隔（ms），看周期抖动最直接
//! DBC 解码属于 M2 的协议编辑器（D13），届时时序/缩放/枚举都在那里定义。

/// 一帧 CAN / CAN FD 报文。
#[derive(Debug, Clone)]
pub struct Frame {
    pub t: f64,       // 秒（绝对或相对都行，出通道时统一平移到 0）
    pub bus: u8,      // 通道号（Vector 的 CAN1/CAN2…，从 1 开始）
    pub id: u32,      // 11 位或 29 位标识符（不含扩展位标记）
    pub ext: bool,    // 29 位扩展帧
    pub fd: bool,     // CAN FD
    pub rx: bool,     // 接收方向
    pub data: Vec<u8>,
}

impl Frame {
    pub fn id_text(&self) -> String {
        // 扩展帧补足 8 位，和 Vector 工具显示一致；标准帧 3 位
        if self.ext {
            format!("{:08X}", self.id)
        } else {
            format!("{:03X}", self.id)
        }
    }
}

pub struct CanLog {
    pub frames: Vec<Frame>,
    pub notes: Vec<String>,
}

/// 把帧序列摊成曲线。只保留**有变化**的字节：恒定字节画出来是一条直线，
/// 既没信息又把通道树撑爆（500 个 ID × 8 字节 = 4000 条）。
pub fn frames_to_series(frames: &[Frame]) -> Vec<(String, Vec<f64>, Vec<f64>)> {
    use std::collections::HashMap;

    if frames.is_empty() {
        return Vec::new();
    }
    let t0 = frames[0].t;

    // 先按 (总线, ID) 分组，再逐字节统计取值集合
    let mut groups: HashMap<(u8, String), Vec<usize>> = HashMap::new();
    for (i, f) in frames.iter().enumerate() {
        groups
            .entry((f.bus, f.id_text()))
            .or_default()
            .push(i);
    }

    let mut out = Vec::new();
    let mut keys: Vec<_> = groups.keys().cloned().collect();
    keys.sort_by(|a, b| (a.0, &a.1).cmp(&(b.0, &b.1)));

    for key in keys {
        let idx = &groups[&key];
        let bus = key.0;
        let id = &key.1;
        let max_len = idx.iter().map(|&i| frames[i].data.len()).max().unwrap_or(0);

        for b in 0..max_len {
            // 该字节在出现的帧里是否取过不同值
            let mut seen: Option<u8> = None;
            let mut varies = false;
            let mut has = false;
            for &i in idx {
                if let Some(v) = frames[i].data.get(b) {
                    has = true;
                    match seen {
                        None => seen = Some(*v),
                        Some(s) if s != *v => {
                            varies = true;
                            break;
                        }
                        _ => {}
                    }
                }
            }
            if !has || !varies {
                continue;
            }
            let t: Vec<f64> = idx
                .iter()
                .filter(|&&i| frames[i].data.len() > b)
                .map(|&i| frames[i].t - t0)
                .collect();
            let v: Vec<f64> = idx
                .iter()
                .filter(|&&i| frames[i].data.len() > b)
                .map(|&i| frames[i].data[b] as f64)
                .collect();
            out.push((format!("CAN{}.{}.b{}", bus, id, b), t, v));
        }

        // 周期（ms）：同 ID 相邻帧间隔，第一帧没有间隔所以跳过
        if idx.len() >= 3 {
            let t: Vec<f64> = idx[1..].iter().map(|&i| frames[i].t - t0).collect();
            let v: Vec<f64> = idx
                .windows(2)
                .map(|w| (frames[w[1]].t - frames[w[0]].t) * 1000.0)
                .collect();
            out.push((format!("CAN{}.{}.period", bus, id), t, v));
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn keeps_only_varying_bytes_and_period() {
        let frames = vec![
            Frame { t: 0.0, bus: 1, id: 0x1A0, ext: false, fd: false, rx: true, data: vec![0, 9, 0] },
            Frame { t: 0.01, bus: 1, id: 0x1A0, ext: false, fd: false, rx: true, data: vec![1, 9, 0] },
            Frame { t: 0.02, bus: 1, id: 0x1A0, ext: false, fd: false, rx: true, data: vec![2, 9, 0] },
        ];
        let s = frames_to_series(&frames);
        assert_eq!(s.len(), 2, "只有 b0 变化 + period");
        assert_eq!(s[0].0, "CAN1.1A0.b0");
        assert_eq!(s[0].2, vec![0.0, 1.0, 2.0]);
        assert_eq!(s[1].0, "CAN1.1A0.period");
        assert!((s[1].2[0] - 10.0).abs() < 1e-6);
    }
}
