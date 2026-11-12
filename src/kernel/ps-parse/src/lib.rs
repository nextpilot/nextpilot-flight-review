//! 格式嗅探：只读文件头部若干字节，给出最可能的解析器家族。
//! 真正的解析器在 src/plugin 下按家族实现（D16：plugin 是官方解析器）。

pub mod csv;
pub mod ulog;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Format {
    Csv,
    Ulog,
    Blf,
    Asc,
    Nmea,
    Unknown,
}

/// 输入建议不超过 4 KiB（各格式的 magic 都在头部）。
pub fn sniff(head: &[u8]) -> Format {
    if head.len() >= 7 && &head[0..7] == b"ULog\0\x01" {
        return Format::Ulog;
    }
    if head.len() >= 4 && &head[0..4] == b"ULog" {
        return Format::Ulog;
    }
    if head.starts_with(b"$GP") || head.starts_with(b"$GN") || head.starts_with(b"$GL") {
        return Format::Nmea;
    }
    if head.len() >= 4 && &head[0..4] == [0x42, 0x4C, 0x46, 0x00] {
        return Format::Blf;
    }
    let ascii_csv = head
        .iter()
        .take(4096)
        .all(|b| *b == b'\t' || *b == b'\n' || *b == b'\r' || (0x20..0x7f).contains(b));
    if ascii_csv && head.iter().any(|b| *b == b',' || *b == b'\t' || *b == b';') {
        return Format::Csv;
    }
    if head.starts_with(b"date ") && head.windows(3).any(|w| w == b"ASC") {
        return Format::Asc;
    }
    Format::Unknown
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn detects_ulog() {
        assert_eq!(sniff(b"ULog\x00\x01\x0c\x00"), Format::Ulog);
    }

    #[test]
    fn detects_csv() {
        assert_eq!(sniff(b"time,att.roll\n0.0,1.2\n"), Format::Csv);
    }

    #[test]
    fn unknown_for_binary_garbage() {
        assert_eq!(sniff(&[0xff, 0xfe, 0x00, 0x01]), Format::Unknown);
    }
}
