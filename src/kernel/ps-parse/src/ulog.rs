//! ULog 解析（PX4/ArduPilot 飞行日志）。
//!
//! 规范依据：PX4 `docs/en/dev_log/ulog_file_format.md`（16 字节头 + 消息流）。
//! 命名与展平规则对齐 PlotJuggler 的 `data_load_ulog`（pj-official-plugins）：
//!   * `timestamp` 字段与 `_padding*` 字段不产出曲线；
//!   * 嵌套结构用 `.` 连接，数组元素下标补两位（`.00` / `.01`）；
//!   * `char[N]` 是字符串（一条序列），不是 N 个数值点；标量 `char` 当数值；
//!   * 同一 topic 只要有一个实例 `multi_id > 0`，全部分支加 `.NN` 后缀。
//!
//! 这里只产出"时间 + 数值"通道，字符串/参数/日志文本进 note，不进曲线表。

use std::collections::HashMap;
use std::path::Path;

const MAGIC: &[u8; 7] = b"ULog\x01\x12\x35";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Prim {
    I8,
    U8,
    I16,
    U16,
    I32,
    U32,
    I64,
    U64,
    F32,
    F64,
    Bool,
    Char,
}

impl Prim {
    fn from_name(s: &str) -> Option<Prim> {
        Some(match s {
            "int8_t" => Prim::I8,
            "uint8_t" => Prim::U8,
            "int16_t" => Prim::I16,
            "uint16_t" => Prim::U16,
            "int32_t" => Prim::I32,
            "uint32_t" => Prim::U32,
            "int64_t" => Prim::I64,
            "uint64_t" => Prim::U64,
            "float" => Prim::F32,
            "double" => Prim::F64,
            "bool" => Prim::Bool,
            "char" => Prim::Char,
            _ => return None,
        })
    }

    fn size(self) -> usize {
        match self {
            Prim::I8 | Prim::U8 | Prim::Bool | Prim::Char => 1,
            Prim::I16 | Prim::U16 => 2,
            Prim::I32 | Prim::U32 | Prim::F32 => 4,
            Prim::I64 | Prim::U64 | Prim::F64 => 8,
        }
    }

    fn read(self, b: &[u8], off: usize) -> Option<f64> {
        let end = off + self.size();
        if end > b.len() {
            return None;
        }
        let s = &b[off..end];
        Some(match self {
            Prim::I8 => s[0] as i8 as f64,
            Prim::U8 => s[0] as f64,
            Prim::Bool => (s[0] != 0) as u8 as f64,
            Prim::Char => s[0] as f64,
            Prim::I16 => i16::from_le_bytes([s[0], s[1]]) as f64,
            Prim::U16 => u16::from_le_bytes([s[0], s[1]]) as f64,
            Prim::I32 => i32::from_le_bytes([s[0], s[1], s[2], s[3]]) as f64,
            Prim::U32 => u32::from_le_bytes([s[0], s[1], s[2], s[3]]) as f64,
            Prim::F32 => f32::from_le_bytes([s[0], s[1], s[2], s[3]]) as f64,
            Prim::I64 => i64::from_le_bytes(s[..8].try_into().ok()?) as f64,
            Prim::U64 => u64::from_le_bytes(s[..8].try_into().ok()?) as f64,
            Prim::F64 => f64::from_le_bytes(s[..8].try_into().ok()?),
        })
    }
}

#[derive(Clone, Debug)]
enum Base {
    Prim(Prim),
    Msg(String),
}

#[derive(Clone, Debug)]
struct Field {
    name: String,
    base: Base,
    array: usize, // 0 = 标量
}

#[derive(Clone, Debug)]
struct Leaf {
    path: String,
    off: usize,
    prim: Option<Prim>,
    emit: bool, // false = timestamp / _padding，不产出曲线但要留出偏移
}

#[derive(Default)]
struct Subscription {
    topic: String,
    multi_id: u8,
    leaves: Vec<Leaf>,
    ts_off: Option<usize>,
    t: Vec<f64>,
    vals: Vec<Vec<f64>>, // 每个数值叶子一条
}

/// 一条曲线：`name` 已拼好 topic + 字段路径。
pub struct ChannelData {
    pub name: String,
    pub t: Vec<f64>,
    pub v: Vec<f64>,
}

pub struct Log {
    pub channels: Vec<ChannelData>,
    pub notes: Vec<String>,
    pub point_rows: usize,
}

fn split_format(text: &str) -> Option<(String, Vec<Field>)> {
    let (name, rest) = text.split_once(':')?;
    let mut fields = Vec::new();
    for raw in rest.split(';') {
        let f = raw.trim();
        if f.is_empty() {
            continue;
        }
        let mut parts = f.splitn(2, ' ');
        let ty = parts.next()?;
        let fname = parts.next().unwrap_or("").trim();
        if fname.is_empty() {
            return None;
        }
        // type / type[N]
        let (base_name, array) = match ty.split_once('[') {
            Some((b, r)) => (b, r.trim_end_matches(']').parse::<usize>().unwrap_or(0)),
            None => (ty, 0),
        };
        let base = match Prim::from_name(base_name) {
            Some(p) => Base::Prim(p),
            None => Base::Msg(base_name.to_string()),
        };
        fields.push(Field { name: fname.to_string(), base, array });
    }
    Some((name.to_string(), fields))
}

fn keep(path: &str) -> bool {
    let last = path.rsplit('.').next().unwrap_or(path);
    last != "timestamp" && !last.starts_with("_padding")
}

/// 递归展开一个 format 的全部叶子（含嵌套结构、数组、padding 占位）。
fn flatten(
    formats: &HashMap<String, Vec<Field>>,
    name: &str,
    off: usize,
    prefix: &str,
    out: &mut Vec<Leaf>,
    depth: usize,
) -> Option<usize> {
    if depth > 16 {
        return None; // 防御环状定义
    }
    let fields = formats.get(name)?;
    let mut cur = off;
    for f in fields {
        let path = if prefix.is_empty() {
            f.name.clone()
        } else {
            format!("{prefix}.{}", f.name)
        };
        let repeated = f.array.max(1);
        match &f.base {
            Base::Prim(Prim::Char) if f.array > 0 => {
                // char[N] 是一条字符串序列，不是 N 个点
                for k in 0..repeated {
                    let leaf_path = if repeated > 1 { format!("{path}.{k:02}") } else { path.clone() };
                    // char[N] 是字符串，本体不进曲线表（只占位，保证后续偏移正确）
                    out.push(Leaf { emit: false, path: leaf_path, off: cur, prim: None });
                    cur += f.array;
                }
            }
            Base::Prim(p) => {
                for k in 0..repeated {
                    let leaf_path = if repeated > 1 { format!("{path}.{k:02}") } else { path.clone() };
                    out.push(Leaf { emit: keep(&leaf_path), path: leaf_path, off: cur, prim: Some(*p) });
                    cur += p.size();
                }
            }
            Base::Msg(m) => {
                for k in 0..repeated {
                    let sub_prefix = if repeated > 1 { format!("{path}.{k:02}") } else { path.clone() };
                    cur += flatten(formats, m, cur, &sub_prefix, out, depth + 1)?;
                }
            }
        }
    }
    Some(cur - off)
}

fn u16_at(b: &[u8], o: usize) -> Option<usize> {
    Some(u16::from_le_bytes([*b.get(o)?, *b.get(o + 1)?]) as usize)
}

pub fn parse_bytes(buf: &[u8]) -> Result<Log, String> {
    if buf.len() < 16 {
        return Err("文件太短，不是 ULog".into());
    }
    if &buf[..7] != MAGIC {
        return Err("magic 不匹配（不是 ULog v1）".into());
    }
    let version = buf[7];
    if version != 1 {
        return Err(format!("不支持的 ULog 版本 v{}", version));
    }

    let mut formats: HashMap<String, Vec<Field>> = HashMap::new();
    let mut subs: Vec<Subscription> = Vec::new();
    let mut by_id: HashMap<u16, usize> = HashMap::new();
    let mut notes: Vec<String> = Vec::new();
    let mut rows = 0usize;
    let mut pos = 16usize;

    while pos + 3 <= buf.len() {
        let size = match u16_at(buf, pos) {
            Some(s) => s,
            None => break,
        };
        let ty = buf[pos + 2];
        let body = pos + 3;
        if body + size > buf.len() {
            notes.push(format!("截断消息（类型 {}，{} 字节）", ty as char, size));
            break;
        }
        let payload = &buf[body..body + size];
        match ty {
            b'F' => {
                if let Ok(s) = std::str::from_utf8(payload) {
                    if let Some((name, fields)) = split_format(s) {
                        formats.insert(name, fields);
                    }
                }
            }
            b'A' => {
                if payload.len() < 3 {
                    pos = body + size;
                    continue;
                }
                let multi_id = payload[0];
                let msg_id = u16::from_le_bytes([payload[1], payload[2]]);
                let name = match std::str::from_utf8(&payload[3..]) {
                    Ok(s) => s.to_string(),
                    Err(_) => {
                        pos = body + size;
                        continue;
                    }
                };
                let Some(fields) = formats.get(&name).cloned() else {
                    notes.push(format!("订阅 {name} 没有格式定义，跳过"));
                    pos = body + size;
                    continue;
                };
                let mut leaves = Vec::new();
                if flatten(&formats, &name, 0, "", &mut leaves, 0).is_none() {
                    notes.push(format!("格式 {name} 展开失败"));
                    pos = body + size;
                    continue;
                }
                let ts_off = leaves.iter().find(|l| l.path == "timestamp").map(|l| l.off);
                if ts_off.is_none() {
                    notes.push(format!("订阅 {name} 无 timestamp 字段，跳过"));
                    pos = body + size;
                    continue;
                }
                let numeric = leaves.iter().filter(|l| l.prim.is_some() && l.emit).count();
                let _ = fields;
                let sub_idx = subs.len();
                subs.push(Subscription {
                    topic: name,
                    multi_id,
                    vals: vec![Vec::new(); numeric],
                    leaves,
                    ts_off,
                    t: Vec::new(),
                });
                by_id.insert(msg_id, sub_idx);
            }
            b'D' => {
                let Some(msg_id) = u16_at(payload, 0).map(|v| v as u16) else {
                    pos = body + size;
                    continue;
                };
                let Some(&si) = by_id.get(&msg_id) else {
                    pos = body + size;
                    continue;
                };
                let data = &payload[2..];
                let sub = &mut subs[si];
                let Some(ts_off) = sub.ts_off else {
                    pos = body + size;
                    continue;
                };
                let Some(ts) = Prim::U64.read(data, ts_off) else {
                    pos = body + size;
                    continue;
                };
                let t = ts / 1e6; // µs → s
                sub.t.push(t);
                let mut k = 0usize;
                for leaf in &sub.leaves {
                    if !leaf.emit {
                        continue;
                    }
                    let Some(p) = leaf.prim else { continue };
                    // 尾随 padding 可能没写进数据段，读不到就当缺测，不整体失败
                    let v = p.read(data, leaf.off).unwrap_or(f64::NAN);
                    sub.vals[k].push(v);
                    k += 1;
                }
                rows += 1;
            }
            b'L' | b'C' => {
                // 日志文本：进 note，不进曲线
                if notes.len() < 64 {
                    let txt = if ty == b'L' {
                        String::from_utf8_lossy(&payload[8.min(payload.len())..]).to_string()
                    } else {
                        String::from_utf8_lossy(payload).to_string()
                    };
                    notes.push(format!("log: {}", txt.trim()));
                }
            }
            b'B' => {
                if let Some(&flag) = payload.first() {
                    if flag & 0x01 != 0 {
                        notes.push("文件带 data-appended 段（M0 忽略）".into());
                    }
                }
            }
            _ => {}
        }
        pos = body + size;
    }

    // 同一 topic 只要存在 multi_id > 0 的实例，全部实例加 .NN 后缀（PJ 规则）
    let mut multi_topics: HashMap<String, u8> = HashMap::new();
    for s in &subs {
        let e = multi_topics.entry(s.topic.clone()).or_insert(0);
        *e = (*e).max(s.multi_id);
    }

    let mut channels = Vec::new();
    for s in subs {
        if s.t.is_empty() {
            continue;
        }
        let topic = if multi_topics.get(&s.topic).copied().unwrap_or(0) > 0 {
            format!("{}.{:02}", s.topic, s.multi_id)
        } else {
            s.topic.clone()
        };
        let mut numeric = s.vals.into_iter();
        for leaf in &s.leaves {
            if leaf.prim.is_none() || !leaf.emit {
                continue;
            }
            let Some(v) = numeric.next() else { break };
            channels.push(ChannelData { name: format!("{topic}.{}", leaf.path), t: s.t.clone(), v });
        }
    }

    if channels.is_empty() {
        return Err("没有可绘制的数值通道（可能只有参数/文本）".into());
    }
    Ok(Log { channels, notes, point_rows: rows })
}

pub fn parse_file(path: &Path) -> Result<Log, String> {
    let buf = std::fs::read(path).map_err(|e| format!("读文件失败：{e}"))?;
    parse_bytes(&buf)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn msg(ty: u8, payload: &[u8]) -> Vec<u8> {
        let mut v = Vec::new();
        v.extend_from_slice(&(payload.len() as u16).to_le_bytes());
        v.push(ty);
        v.extend_from_slice(payload);
        v
    }

    fn build_log() -> Vec<u8> {
        let mut out = Vec::new();
        out.extend_from_slice(MAGIC);
        out.push(1);
        out.extend_from_slice(&0u64.to_le_bytes());

        let f1 = b"attitude:uint64_t timestamp;float roll;float pitch;uint8_t flags;";
        out.extend(msg(b'F', f1));
        let f2 = b"multi_t:uint64_t timestamp;uint8_t[2] arr;float v;";
        out.extend(msg(b'F', f2));

        // 订阅 attitude(msg_id=0)、multi_t 两个实例（multi_id 0/1）
        let mut a = vec![0u8, 0, 0];
        a.extend_from_slice(b"attitude");
        out.extend(msg(b'A', &a));
        for (id, mid) in [(1u16, 0u8), (2u16, 1u8)] {
            let mut a = vec![mid];
            a.extend_from_slice(&id.to_le_bytes());
            a.extend_from_slice(b"multi_t");
            out.extend(msg(b'A', &a));
        }

        for i in 0..3u64 {
            let mut d = vec![0u8, 0];
            d.extend_from_slice(&(i * 100_000).to_le_bytes());
            d.extend_from_slice(&(i as f32 * 1.5).to_le_bytes());
            d.extend_from_slice(&(-(i as f32)).to_le_bytes());
            d.push(i as u8);
            out.extend(msg(b'D', &d));
        }
        // multi_t 两个实例各来两行：验证 .00 / .01 后缀与数组下标
        for (id, mid) in [(1u16, 0u8), (2u16, 1u8)] {
            for i in 0..2u64 {
                let mut d = id.to_le_bytes().to_vec();
                d.extend_from_slice(&(i * 200_000).to_le_bytes());
                d.push(7 + i as u8);
                d.push(9 - i as u8);
                d.extend_from_slice(&(mid as f32 * 10.0).to_le_bytes());
                out.extend(msg(b'D', &d));
            }
        }
        out
    }

    #[test]
    fn parses_flat_and_multi() {
        let log = parse_bytes(&build_log()).unwrap();
        let names: Vec<&str> = log.channels.iter().map(|c| c.name.as_str()).collect();
        // multi_id>0 存在 → 两个实例都加 .00/.01
        assert!(names.contains(&"multi_t.00.arr.00"), "{names:?}");
        assert!(names.contains(&"multi_t.01.v"), "{names:?}");
        // attitude 没有多实例 → 不加后缀，且 timestamp 不产出
        assert!(names.contains(&"attitude.roll"));
        assert!(!names.iter().any(|n| n.ends_with("timestamp")));
        let roll = log.channels.iter().find(|c| c.name == "attitude.roll").unwrap();
        assert_eq!(roll.t, vec![0.0, 0.1, 0.2]);
        assert_eq!(roll.v, vec![0.0, 1.5, 3.0]);
    }
}
