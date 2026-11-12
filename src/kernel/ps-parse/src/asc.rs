//! Vector ASC（文本）CAN 日志解析。
//!
//! 只认与画图有关的部分：时间戳 + 通道 + ID + 方向 + DLC + 数据字节。
//! 其余行（`date` / `base hex` / `internal events` / `Begin Triggerblock`）
//! 只是头部元信息，遇到就跳过；不认识的行直接忽略，不因一行怪数据整份报废。
//!
//! 语法（与 Vector CANalyzer/CANoe 导出一致）：
//! ```text
//!   0.002000 1  1A0             Rx   d 8 11 22 33 44 55 66 77 88
//!   0.002000 1  18FEF100x       Rx   d 8 00 00 00 00 00 00 00 00
//!   0.010000 CANFD   1 Rx  1A0            1 0 9 12 11 22 ...
//! ```

use super::can::{CanLog, Frame};
use std::path::Path;

pub fn parse_file(path: &Path) -> Result<CanLog, String> {
    let buf = std::fs::read(path).map_err(|e| format!("读不了 {}: {}", path.display(), e))?;
    parse_str(&String::from_utf8_lossy(&buf))
}

pub fn parse_str(text: &str) -> Result<CanLog, String> {
    let mut frames: Vec<Frame> = Vec::new();
    let mut notes: Vec<String> = Vec::new();
    // `base hex` / `base dec`：Vector 允许十进制导出，10 进制时 ID 与数据都按 10 解析
    let mut base_hex = true;
    // FD 行里 dlc 与真实数据长度可能不一致，这里只用真实长度
    let mut bad_lines = 0usize;

    for line in text.lines() {
        let s = line.trim();
        if s.is_empty() || s.starts_with("//") {
            continue;
        }
        let low = s.to_ascii_lowercase();
        if low.starts_with("base ") {
            base_hex = low.contains("hex");
            continue;
        }
        if low.starts_with("date ")
            || low.starts_with("begin triggerblock")
            || low.starts_with("end triggerblock")
            || low.contains("internal events")
            || low.starts_with("no internal events")
            || low.starts_with("start of measurement")
            || low.starts_with("errorframe")
        {
            continue;
        }

        let mut it = s.split_whitespace();
        let ts: f64 = match it.next().and_then(|w| w.parse().ok()) {
            Some(v) => v,
            None => continue, // 统计行 / 注释行
        };

        let second = it.next().unwrap_or("");
        if second.eq_ignore_ascii_case("CANFD") {
            // <ts> CANFD <ch> <dir> <id> <brs> <esi> <dlc> <len> <bytes...>
            let ch = it.next().and_then(|w| w.parse::<u8>().ok()).unwrap_or(1);
            let dir = it.next().unwrap_or("Rx");
            let id_tok = it.next().unwrap_or("");
            let _brs = it.next();
            let _esi = it.next();
            let _dlc = it.next();
            let len: usize = it.next().and_then(|w| w.parse().ok()).unwrap_or(0);
            let mut data = Vec::new();
            for tok in it {
                if data.len() >= len {
                    break;
                }
                match parse_num(tok, base_hex) {
                    Some(b) => data.push(b),
                    None => break,
                }
            }
            match parse_id(id_tok, base_hex) {
                Some((id, ext)) => frames.push(Frame {
                    t: ts,
                    bus: ch,
                    id,
                    ext,
                    fd: true,
                    rx: dir.eq_ignore_ascii_case("Rx"),
                    data,
                }),
                None => bad_lines += 1,
            }
            continue;
        }

        // 经典 CAN：<ts> <ch> <id> <Rx|Tx> d <dlc> <bytes...>
        let ch = match second.parse::<u8>() {
            Ok(v) => v,
            Err(_) => continue, // 统计行等
        };
        let id_tok = match it.next() {
            Some(w) if !w.eq_ignore_ascii_case("ErrorFrame") => w,
            _ => continue,
        };
        let dir = it.next().unwrap_or("Rx");
        let kind = it.next().unwrap_or("");
        if kind.eq_ignore_ascii_case("r") {
            continue; // 远程帧没有数据，画不了
        }
        let dlc: usize = it.next().and_then(|w| w.parse().ok()).unwrap_or(0);
        let mut data = Vec::new();
        for tok in it {
            if data.len() >= dlc.min(8) {
                break;
            }
            match parse_num(tok, base_hex) {
                Some(b) => data.push(b),
                None => break,
            }
        }
        match parse_id(id_tok, base_hex) {
            Some((id, ext)) => frames.push(Frame {
                t: ts,
                bus: ch,
                id,
                ext,
                fd: false,
                rx: dir.eq_ignore_ascii_case("Rx"),
                data,
            }),
            None => bad_lines += 1,
        }
    }

    if frames.is_empty() {
        return Err("ASC 里没有解析出任何 CAN 帧（检查是不是符号化导出或纯错误帧）".into());
    }
    notes.push(format!(
        "ASC：{} 帧，{} 个 ID；base {}",
        frames.len(),
        {
            let mut ids = std::collections::HashSet::new();
            for f in &frames {
                ids.insert((f.bus, f.id_text()));
            }
            ids.len()
        },
        if base_hex { "hex" } else { "dec" }
    ));
    if bad_lines > 0 {
        notes.push(format!("{} 行 ID 解析失败，已跳过", bad_lines));
    }
    Ok(CanLog { frames, notes })
}

/// ID 串：`1A0` / `18FEF100x`（末尾 x = 29 位扩展帧）
fn parse_id(tok: &str, base_hex: bool) -> Option<(u32, bool)> {
    let (body, ext) = if let Some(stripped) = tok.strip_suffix(['x', 'X']) {
        (stripped, true)
    } else {
        (tok, false)
    };
    let radix = if base_hex { 16 } else { 10 };
    let id = if base_hex {
        u32::from_str_radix(body, radix).ok()?
    } else {
        body.parse::<u32>().ok()?
    };
    Some((id & 0x1FFF_FFFF, ext))
}

fn parse_num(tok: &str, base_hex: bool) -> Option<u8> {
    if base_hex {
        u8::from_str_radix(tok, 16).ok()
    } else {
        tok.parse::<u8>().ok()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const SAMPLE: &str = r#"date Thu Nov 12 10:00:00.000 2026
base hex  timestamps absolute
internal events logged
// version 13.0.0
Begin Triggerblock Thu Nov 12 10:00:00.000 2026
   0.000000 Start of measurement
   0.002000 1  1A0             Rx   d 2 11 22
   0.004000 1  18FEF100x       Rx   d 1 33
   0.006000 2  1A0             Tx   d 2 11 44
   0.008000 CANFD   1 Rx  2B0            1 0 9 12 11 22 33 44 55 66 77 88 99 AA BB CC
   0.010000 1  1A0             Rx   d 2 11 22
End Triggerblock
"#;

    #[test]
    fn parses_classic_ext_and_fd() {
        let log = parse_str(SAMPLE).unwrap();
        assert_eq!(log.frames.len(), 5);
        assert!(log.frames[1].ext && log.frames[1].id == 0x18FEF100);
        assert_eq!(log.frames[2].bus, 2);
        assert!(!log.frames[2].rx);
        assert!(log.frames[3].fd);
        assert_eq!(log.frames[3].data.len(), 12);
    }

    #[test]
    fn tolerates_decimal_base_and_junk_lines() {
        let txt = "base dec\n0.500000 1 416 Rx d 1 7\nsome statistics line\n";
        let log = parse_str(txt).unwrap();
        assert_eq!(log.frames.len(), 1);
        assert_eq!(log.frames[0].id, 416);
        assert_eq!(log.frames[0].data, vec![7]);
    }
}
