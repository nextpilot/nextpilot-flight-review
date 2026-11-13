//! ArduPilot DataFlash `.bin` 解析。
//!
//! 规范依据：ArduPilot `libraries/AP_Logger/LogStructure.h` 与官方
//! `ArduPilot/plotjuggler-apbin-plugins` 的 `DataLoadAPBin`。
//! 文件由 `0xA3 0x95 <msgid>` 包头 + 定长消息组成；`FMT`(128) 自描述后续消息。
//! 这里只产出数值曲线，`PARM`/`MSG` 单独收集，字符串字段不进曲线。

use std::collections::HashMap;
use std::fs;
use std::path::Path;

const HEAD1: u8 = 0xA3;
const HEAD2: u8 = 0x95;
const FMT_ID: u8 = 128;
const FMT_PACKET_LEN: usize = 89;

#[derive(Clone, Debug)]
struct Field {
    name: String,
    code: u8,
    off: usize,
    size: usize,
}

#[derive(Clone, Debug)]
struct Format {
    id: u8,
    name: String,
    length: usize,
    fields: Vec<Field>,
    instance_idx: Option<usize>,
}

#[derive(Clone, Copy, Default)]
struct FmtUnits {
    units: [u8; 16],
    mults: [u8; 16],
}

/// 一条曲线。`name` 形如 `ATT.Roll`，多实例 `IMU.#0.AccX`。
pub struct ChannelData {
    pub name: String,
    pub unit: String,
    pub t: Vec<f64>,
    pub v: Vec<f64>,
}

/// 参数与日志文本：参数进参数页，MSG 进消息页。
pub struct ApLog {
    pub channels: Vec<ChannelData>,
    pub params: Vec<(String, String)>,
    pub messages: Vec<(f64, String)>,
    pub point_rows: usize,
}

fn code_size(c: u8) -> Option<usize> {
    Some(match c {
        b'a' => 64,
        b'b' | b'B' => 1,
        b'h' | b'H' | b'c' | b'C' => 2,
        b'i' | b'I' | b'f' | b'e' | b'E' | b'L' | b'M' => 4,
        b'd' | b'q' | b'Q' => 8,
        b'n' => 4,
        b'N' => 16,
        b'Z' => 64,
        _ => return None,
    })
}

fn is_string(c: u8) -> bool {
    matches!(c, b'n' | b'N' | b'Z')
}

fn parse_format(pkt: &[u8]) -> Option<Format> {
    if pkt.len() < FMT_PACKET_LEN {
        return None;
    }
    let id = pkt[3];
    let length = pkt[4] as usize;
    let name = trim_str(&pkt[5..9]);
    if name.is_empty() || !name.bytes().all(|b| b.is_ascii_graphic()) {
        return None;
    }
    let fmt = trim_str(&pkt[9..25]);
    let labels = trim_str(&pkt[25..89]);
    if fmt.is_empty() || length < 3 {
        return None;
    }
    let labels: Vec<&str> = labels.split(',').collect();
    let mut fields = Vec::new();
    let mut off = 3;
    for (i, code) in fmt.bytes().enumerate() {
        let size = code_size(code)?;
        let label = labels.get(i).copied().unwrap_or("").trim();
        fields.push(Field {
            name: if label.is_empty() { String::new() } else { label.to_string() },
            code,
            off,
            size,
        });
        off += size;
    }
    Some(Format { id, name, length, fields, instance_idx: None })
}

fn trim_str(b: &[u8]) -> String {
    let end = b.iter().position(|c| *c == 0).unwrap_or(b.len());
    String::from_utf8_lossy(&b[..end]).trim().to_string()
}

fn read_num(pkt: &[u8], f: &Field) -> Option<f64> {
    let end = f.off + f.size;
    if end > pkt.len() {
        return None;
    }
    let s = &pkt[f.off..end];
    Some(match f.code {
        b'b' => s[0] as i8 as f64,
        b'B' | b'M' => s[0] as f64,
        b'h' | b'c' => i16::from_le_bytes([s[0], s[1]]) as f64,
        b'H' | b'C' => u16::from_le_bytes([s[0], s[1]]) as f64,
        b'i' | b'e' | b'L' => i32::from_le_bytes([s[0], s[1], s[2], s[3]]) as f64,
        b'I' | b'E' => u32::from_le_bytes([s[0], s[1], s[2], s[3]]) as f64,
        b'f' => f32::from_le_bytes([s[0], s[1], s[2], s[3]]) as f64,
        b'd' => f64::from_le_bytes(s[..8].try_into().ok()?),
        b'q' => i64::from_le_bytes(s[..8].try_into().ok()?) as f64,
        b'Q' => u64::from_le_bytes(s[..8].try_into().ok()?) as f64,
        _ => return None,
    })
}

fn read_string(pkt: &[u8], f: &Field) -> String {
    let end = (f.off + f.size).min(pkt.len());
    if f.off >= end {
        return String::new();
    }
    trim_str(&pkt[f.off..end])
}

fn find_field(fmt: &Format, name: &str) -> Option<usize> {
    fmt.fields.iter().position(|f| f.name == name)
}

/// 扫描整个文件，找到所有 `FMT` 定义。未知类型的消息跳过 1 字节重新同步。
fn collect_formats(buf: &[u8]) -> HashMap<u8, Format> {
    let mut formats = HashMap::new();
    let mut pos = 0usize;
    while pos + 3 <= buf.len() {
        if buf[pos] != HEAD1 || buf[pos + 1] != HEAD2 {
            pos += 1;
            continue;
        }
        let id = buf[pos + 2];
        if id == FMT_ID {
            if pos + FMT_PACKET_LEN > buf.len() {
                break;
            }
            if let Some(f) = parse_format(&buf[pos..pos + FMT_PACKET_LEN]) {
                formats.insert(f.id, f);
            }
            pos += FMT_PACKET_LEN;
            continue;
        }
        match formats.get(&id) {
            Some(f) if f.length >= 3 => pos += f.length,
            _ => pos += 1,
        }
    }
    formats
}

/// 第二遍：FMTU / UNIT / MULT，补单位、倍率和 `#` 实例字段。
fn collect_units(buf: &[u8], formats: &mut HashMap<u8, Format>) -> (HashMap<u8, FmtUnits>, HashMap<u8, String>, HashMap<u8, f64>) {
    let mut fmtu: HashMap<u8, FmtUnits> = HashMap::new();
    let mut unit_names: HashMap<u8, String> = HashMap::new();
    let mut mults: HashMap<u8, f64> = HashMap::new();
    let mut pos = 0usize;
    while pos + 3 <= buf.len() {
        if buf[pos] != HEAD1 || buf[pos + 1] != HEAD2 {
            pos += 1;
            continue;
        }
        let id = buf[pos + 2];
        if id == FMT_ID {
            pos += FMT_PACKET_LEN;
            continue;
        }
        let Some(fmt) = formats.get(&id).cloned() else {
            pos += 1;
            continue;
        };
        if pos + fmt.length > buf.len() {
            break;
        }
        let pkt = &buf[pos..pos + fmt.length];
        match fmt.name.as_str() {
            "FMTU" if pkt.len() >= 44 => {
                let target = pkt[11];
                let mut u = [0u8; 16];
                let mut m = [0u8; 16];
                u.copy_from_slice(&pkt[12..28]);
                m.copy_from_slice(&pkt[28..44]);
                fmtu.insert(target, FmtUnits { units: u, mults: m });
                if let Some(idx) = u.iter().position(|c| *c == b'#') {
                    if let Some(f) = formats.get_mut(&target) {
                        f.instance_idx = Some(idx);
                    }
                }
            }
            "UNIT" => {
                if let (Some(i_id), Some(i_label)) = (find_field(&fmt, "Id"), find_field(&fmt, "Label")) {
                    if let Some(v) = read_num(pkt, &fmt.fields[i_id]) {
                        unit_names.insert(v as u8, read_string(pkt, &fmt.fields[i_label]));
                    }
                }
            }
            "MULT" => {
                if let (Some(i_id), Some(i_mult)) = (find_field(&fmt, "Id"), find_field(&fmt, "Mult")) {
                    if let (Some(idv), Some(m)) = (read_num(pkt, &fmt.fields[i_id]), read_num(pkt, &fmt.fields[i_mult])) {
                        mults.insert(idv as u8, m);
                    }
                }
            }
            _ => {}
        }
        pos += fmt.length;
    }
    (fmtu, unit_names, mults)
}

struct Builder {
    has_instance: bool,
    t: Vec<f64>,
    v: Vec<f64>,
}

pub fn parse_bytes(buf: &[u8]) -> Result<ApLog, String> {
    if buf.len() < 3 || buf[0] != HEAD1 || buf[1] != HEAD2 {
        return Err("不是 ArduPilot DataFlash .bin（缺少 A3 95 头）".into());
    }
    let mut formats = collect_formats(buf);
    if formats.is_empty() {
        return Err("没有找到 FMT 格式定义".into());
    }
    let (fmtu, unit_names, mults) = collect_units(buf, &mut formats);

    let mut builders: HashMap<(u8, u8, usize), Builder> = HashMap::new();
    let mut params = Vec::new();
    let mut messages = Vec::new();
    let mut rows = 0usize;
    let mut pos = 0usize;

    while pos + 3 <= buf.len() {
        if buf[pos] != HEAD1 || buf[pos + 1] != HEAD2 {
            pos += 1;
            continue;
        }
        let id = buf[pos + 2];
        if id == FMT_ID {
            pos += FMT_PACKET_LEN;
            continue;
        }
        let Some(fmt) = formats.get(&id) else {
            pos += 1;
            continue;
        };
        if pos + fmt.length > buf.len() {
            break;
        }
        let pkt = &buf[pos..pos + fmt.length];

        if fmt.name == "PARM" {
            let name = find_field(fmt, "Name").map(|i| read_string(pkt, &fmt.fields[i]).to_string());
            let value = find_field(fmt, "Value").and_then(|i| read_num(pkt, &fmt.fields[i]));
            if let (Some(n), Some(v)) = (name, value) {
                if !n.is_empty() {
                    params.push((n, format!("{v}")));
                }
            }
            pos += fmt.length;
            continue;
        }
        if fmt.name == "MSG" {
            let time = find_field(fmt, "TimeUS").and_then(|i| read_num(pkt, &fmt.fields[i]).map(|t| t / 1e6));
            let text = find_field(fmt, "Message").map(|i| read_string(pkt, &fmt.fields[i]).to_string());
            if let (Some(t), Some(s)) = (time, text) {
                if !s.is_empty() {
                    messages.push((t, s));
                }
            }
            pos += fmt.length;
            continue;
        }

        let Some(ti) = find_field(fmt, "TimeUS") else {
            pos += fmt.length;
            continue;
        };
        let Some(t_us) = read_num(pkt, &fmt.fields[ti]) else {
            pos += fmt.length;
            continue;
        };
        let t = t_us / 1e6;
        let instance = fmt.instance_idx.and_then(|i| read_num(pkt, &fmt.fields[i]).map(|v| v as u8)).unwrap_or(0);
        let has_instance = fmt.instance_idx.is_some();

        for (fi, f) in fmt.fields.iter().enumerate() {
            if fi == ti || Some(fi) == fmt.instance_idx {
                continue;
            }
            if f.name.is_empty() || is_string(f.code) || f.code == b'a' {
                continue;
            }
            let Some(val) = read_num(pkt, f) else { continue };
            let b = builders
                .entry((fmt.id, instance, fi))
                .or_insert_with(|| Builder { has_instance, t: Vec::new(), v: Vec::new() });
            b.t.push(t);
            b.v.push(val);
        }
        rows += 1;
        pos += fmt.length;
    }

    let mut channels = Vec::new();
    for ((msg_id, instance, fi), mut b) in builders {
        let Some(fmt) = formats.get(&msg_id) else { continue };
        let f = &fmt.fields[fi];
        let mut name = fmt.name.clone();
        if b.has_instance {
            name.push_str(&format!("#{instance}."));
        } else {
            name.push('.');
        }
        name.push_str(&f.name);
        let u = fmtu.get(&msg_id);
        let unit = u
            .map(|u| u.units[fi])
            .filter(|c| *c != 0 && *c != b'-' && *c != b'#')
            .and_then(|c| unit_names.get(&c).cloned())
            .unwrap_or_default();
        if let (Some(u), Some(mults)) = (u, Some(&mults)) {
            if let Some(mc) = u.mults.get(fi) {
                if let Some(k) = mults.get(mc) {
                    for v in &mut b.v {
                        *v *= *k;
                    }
                }
            }
        }
        channels.push(ChannelData { name, unit, t: b.t, v: b.v });
    }
    channels.sort_by(|a, b| a.name.cmp(&b.name));

    Ok(ApLog { channels, params, messages, point_rows: rows })
}

pub fn parse_file(path: &Path) -> Result<ApLog, String> {
    let buf = fs::read(path).map_err(|e| format!("读取失败：{e}"))?;
    parse_bytes(&buf)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fmt_packet(id: u8, length: u8, name: &str, fmt: &str, labels: &str) -> Vec<u8> {
        let mut p = vec![HEAD1, HEAD2, FMT_ID, id, length];
        let mut name_b = [0u8; 4];
        let mut fmt_b = [0u8; 16];
        let mut labels_b = [0u8; 64];
        for (i, b) in name.as_bytes().iter().take(4).enumerate() { name_b[i] = *b; }
        for (i, b) in fmt.as_bytes().iter().take(16).enumerate() { fmt_b[i] = *b; }
        for (i, b) in labels.as_bytes().iter().take(64).enumerate() { labels_b[i] = *b; }
        p.extend_from_slice(&name_b);
        p.extend_from_slice(&fmt_b);
        p.extend_from_slice(&labels_b);
        assert_eq!(p.len(), FMT_PACKET_LEN);
        p
    }

    fn att_msg(t_us: u64, roll: f32, pitch: f32) -> Vec<u8> {
        let mut p = vec![HEAD1, HEAD2, 1];
        p.extend_from_slice(&t_us.to_le_bytes());
        p.extend_from_slice(&roll.to_le_bytes());
        p.extend_from_slice(&pitch.to_le_bytes());
        p
    }

    #[test]
    fn parses_minimal_dataflash() {
        let mut buf = fmt_packet(1, 19, "ATT", "Qff", "TimeUS,Roll,Pitch");
        buf.extend_from_slice(&att_msg(1_000_000, 0.5, -0.25));
        buf.extend_from_slice(&att_msg(2_500_000, 1.5, 0.75));
        let log = parse_bytes(&buf).unwrap();
        assert_eq!(log.channels.len(), 2);
        let roll = log.channels.iter().find(|c| c.name == "ATT.Roll").unwrap();
        assert_eq!(roll.t, vec![1.0, 2.5]);
        assert_eq!(roll.v, vec![0.5, 1.5]);
        let pitch = log.channels.iter().find(|c| c.name == "ATT.Pitch").unwrap();
        assert_eq!(pitch.v, vec![-0.25, 0.75]);
    }

    #[test]
    fn rejects_non_dataflash() {
        assert!(parse_bytes(b"hello").is_err());
    }

    #[test]
    #[ignore]
    fn parses_real_sample() {
        let path = std::env::var("APBIN_SAMPLE").unwrap_or_else(|_| "/tmp/pj-apbin/tests/logs/00000001.BIN".into());
        let log = parse_file(Path::new(&path)).unwrap();
        assert!(log.channels.len() > 50, "channels={}", log.channels.len());
        assert!(log.point_rows > 1000);
        eprintln!("channels={} rows={} params={} msgs={}", log.channels.len(), log.point_rows, log.params.len(), log.messages.len());

    }
}
