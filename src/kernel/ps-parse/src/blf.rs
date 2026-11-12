//! Vector BLF（二进制）CAN 日志解析。
//!
//! BLF 没有公开规范，结构依据 Vector 自家工具导出的实际文件（并与
//! python-can 的 `can/io/blf.py`（源于 vector_blf）逐字段对照）：
//!
//! ```text
//! 文件头 144 字节： "LOGG" + headerSize(u32) + … + fileSize(u64) + objectCount(u32) …
//! 之后是对象序列，每个对象：
//!   "LOBJ"(4) + headerSize(u16) + headerVersion(u16) + objSize(u32) + objType(u32)
//!   → 对象头 v1：flags(u32) clientIndex(u16) version(u16) timestamp(u64)  （共 32）
//!   → 对象头 v2：flags(u32) tsStatus(u8) version(u16) timestamp(u64) 原时间戳  （共 44）
//! ```
//!
//! 只取两类：`CAN_MESSAGE`(1)/`CAN_MESSAGE2`(86) 与 `CAN_FD_MESSAGE`(100)。
//! 报文通常压在 `LOG_CONTAINER`(10) 里（zlib 或不压缩），必须先解压再解析。

use super::can::{CanLog, Frame};
use std::collections::HashSet;
use std::io::Read;
use std::path::Path;

const CAN_MESSAGE: u32 = 1;
const LOG_CONTAINER: u32 = 10;
const CAN_MESSAGE2: u32 = 86;
const CAN_FD_MESSAGE: u32 = 100;
const CAN_MSG_EXT: u32 = 0x8000_0000;

pub fn parse_file(path: &Path) -> Result<CanLog, String> {
    let buf = std::fs::read(path).map_err(|e| format!("读不了 {}: {}", path.display(), e))?;
    parse_bytes(&buf)
}

pub fn parse_bytes(buf: &[u8]) -> Result<CanLog, String> {
    if buf.len() < 144 || &buf[0..4] != b"LOGG" {
        return Err("不是 BLF：文件头没有 LOGG 签名".into());
    }
    let header_size = u32::from_le_bytes([buf[4], buf[5], buf[6], buf[7]]) as usize;
    // 对象计数在 +32（+16 起是 fileSize/uncompressedSize 两个 u64）
    let object_count = u32::from_le_bytes([buf[32], buf[33], buf[34], buf[35]]);

    let mut frames = Vec::new();
    let mut notes = Vec::new();
    let mut containers = 0usize;
    let mut other_objects = 0usize;
    // 跨容器续接的半截对象
    let mut pending: Vec<u8> = Vec::new();

    // 顶层对象区：先扫容器，容器里的对象流再递归解析
    let mut pos = header_size.max(144);
    while pos + 16 <= buf.len() {
        if &buf[pos..pos + 4] != b"LOBJ" {
            // 末尾对齐/垃圾字节，往后挪一格找下一个对象头
            pos += 1;
            continue;
        }
        let obj_size = u32::from_le_bytes([
            buf[pos + 8],
            buf[pos + 9],
            buf[pos + 10],
            buf[pos + 11],
        ]) as usize;
        let obj_type = u32::from_le_bytes([
            buf[pos + 12],
            buf[pos + 13],
            buf[pos + 14],
            buf[pos + 15],
        ]);
        if obj_size < 16 || pos + obj_size > buf.len() {
            break;
        }
        let obj_header_size = u16::from_le_bytes([buf[pos + 4], buf[pos + 5]]) as usize;
        let body = &buf[pos + obj_header_size..pos + obj_size];

        match obj_type {
            LOG_CONTAINER => {
                containers += 1;
                if body.len() < 16 {
                    pos += obj_size;
                    continue;
                }
                let method = u16::from_le_bytes([body[0], body[1]]);
                let payload = &body[16..];
                match method {
                    0 => pending.extend_from_slice(payload),
                    2 => {
                        let mut out = Vec::new();
                        let mut d = flate2::read::ZlibDecoder::new(payload);
                        if d.read_to_end(&mut out).is_ok() {
                            pending.extend_from_slice(&out);
                        } else {
                            notes.push("有个 zlib 容器解压失败，已跳过".into());
                        }
                    }
                    m => notes.push(format!("未知压缩方式 {}，已跳过", m)),
                }
                // 对象可能跨容器：把上次剩下的半截接在新解压数据前面
                drain_objects(&mut pending, &mut frames, &mut other_objects);
            }
            _ => {
                other_objects += 1;
                parse_one(buf, pos, &mut frames);
            }
        }
        // BLFWriter 的填充不严格按 4 字节对齐（实测容器末尾会差几个字节），
        // 所以按 python-can 的做法：在 obj_size 之后的小窗口里找下一个 LOBJ
        pos = next_object(buf, pos + obj_size);
    }

    if frames.is_empty() {
        return Err(format!(
            "BLF 里没找到 CAN 报文（{} 个容器 / {} 个其他对象）",
            containers, other_objects
        ));
    }

    let ids: HashSet<(u8, String)> = frames.iter().map(|f| (f.bus, f.id_text())).collect();
    notes.push(format!(
        "BLF：{} 帧，{} 个 ID，{} 个容器；文件声明 {} 个对象",
        frames.len(),
        ids.len(),
        containers,
        object_count
    ));
    Ok(CanLog { frames, notes })
}

/// 容器解压后的对象流：只吃掉**完整**对象，剩下的半截留在 pending 里等下一个容器。
fn drain_objects(pending: &mut Vec<u8>, frames: &mut Vec<Frame>, other: &mut usize) {
    let mut pos = 0usize;
    while pos + 16 <= pending.len() {
        if &pending[pos..pos + 4] != b"LOBJ" {
            pos += 1;
            continue;
        }
        let obj_size =
            u32::from_le_bytes([pending[pos + 8], pending[pos + 9], pending[pos + 10], pending[pos + 11]])
                as usize;
        if obj_size < 16 || pos + obj_size > pending.len() {
            break; // 半截对象，等下一个容器
        }
        let obj_type =
            u32::from_le_bytes([pending[pos + 12], pending[pos + 13], pending[pos + 14], pending[pos + 15]]);
        match obj_type {
            CAN_MESSAGE | CAN_MESSAGE2 | CAN_FD_MESSAGE => parse_one(pending, pos, frames),
            _ => *other += 1,
        }
        pos = next_object(pending, pos + obj_size);
    }
    if pos > 0 {
        pending.drain(0..pos);
    }
}

/// 在 `from` 之后最多 16 字节内找下一个对象头（找不到就返回 from）。
fn next_object(data: &[u8], from: usize) -> usize {
    let end = (from + 16).min(data.len());
    let mut i = from;
    while i + 4 <= end {
        if &data[i..i + 4] == b"LOBJ" {
            return i;
        }
        i += 1;
    }
    from
}

fn parse_one(buf: &[u8], pos: usize, frames: &mut Vec<Frame>) {
    let hdr_size = u16::from_le_bytes([buf[pos + 4], buf[pos + 5]]) as usize;
    let obj_size = u32::from_le_bytes([
        buf[pos + 8],
        buf[pos + 9],
        buf[pos + 10],
        buf[pos + 11],
    ]) as usize;
    let obj_type = u32::from_le_bytes([
        buf[pos + 12],
        buf[pos + 13],
        buf[pos + 14],
        buf[pos + 15],
    ]);
    if hdr_size < 32 || pos + hdr_size > buf.len() {
        return;
    }
    // 时间戳在 v1/v2 对象头里都落在 +24
    let flags = u32::from_le_bytes([
        buf[pos + 16],
        buf[pos + 17],
        buf[pos + 18],
        buf[pos + 19],
    ]);
    let ts_raw = u64::from_le_bytes([
        buf[pos + 24],
        buf[pos + 25],
        buf[pos + 26],
        buf[pos + 27],
        buf[pos + 28],
        buf[pos + 29],
        buf[pos + 30],
        buf[pos + 31],
    ]);
    // flags bit1 = 时间戳单位是 ns，否则是 10 µs
    let t = if flags & 0x2 != 0 {
        ts_raw as f64 * 1e-9
    } else {
        ts_raw as f64 * 1e-5
    };

    let body = pos + hdr_size;
    if body + 8 > pos + obj_size || body + 8 > buf.len() {
        return;
    }
    // 通道号沿用 Vector 编码（与 ASC 的 "1"/"2" 同源），不再 +1
    let bus = u16::from_le_bytes([buf[body], buf[body + 1]]) as u8;
    let msg_flags = buf[body + 2];
    let dlc = buf[body + 3];
    let id_raw = u32::from_le_bytes([buf[body + 4], buf[body + 5], buf[body + 6], buf[body + 7]]);

    let (fd, data_len, data_start) = if obj_type == CAN_FD_MESSAGE {
        // <H B B L L L B B 5x 64s>
        let valid = if body + 25 < buf.len() {
            buf[body + 24] as usize
        } else {
            0
        };
        (true, valid.min(64), body + 32)
    } else {
        (false, (dlc as usize).min(8), body + 8)
    };
    let mut data = Vec::with_capacity(data_len);
    for i in 0..data_len {
        if data_start + i < buf.len() {
            data.push(buf[data_start + i]);
        }
    }

    frames.push(Frame {
        t,
        bus,
        id: id_raw & 0x1FFF_FFFF,
        ext: id_raw & CAN_MSG_EXT != 0,
        fd,
        rx: msg_flags & 0x1 != 0,
        data,
    });
}

/// 扩展帧的 `id_raw` 最高位是扩展标记，标准帧没有；这里只用于测试夹具校验。
#[allow(dead_code)]
fn is_extended(id_raw: u32) -> bool {
    id_raw & CAN_MSG_EXT != 0
}
