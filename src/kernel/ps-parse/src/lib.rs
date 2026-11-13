//! 格式嗅探：只读文件头部若干字节，给出最可能的解析器家族。
//! 真正的解析器在 src/plugin 下按家族实现（D16：plugin 是官方解析器）。

pub mod apbin;
pub mod asc;
pub mod blf;
pub mod can;
pub mod csv;
pub mod ulog;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Format {
    Csv,
    ApBin,
    Ulog,
    Blf,
    Asc,
    Nmea,
    Unknown,
}

/// 输入建议不超过 4 KiB（各格式的 magic 都在头部）。
pub fn sniff(head: &[u8]) -> Format {
    // ArduPilot DataFlash：0xA3 0x95 + msgid；比 ASCII 嗅探优先
    if head.len() >= 3 && head[0] == 0xA3 && head[1] == 0x95 {
        return Format::ApBin;
    }
    if head.len() >= 7 && &head[0..7] == b"ULog\0\x01" {
        return Format::Ulog;
    }
    if head.len() >= 4 && &head[0..4] == b"ULog" {
        return Format::Ulog;
    }
    if head.starts_with(b"$GP") || head.starts_with(b"$GN") || head.starts_with(b"$GL") {
        return Format::Nmea;
    }
    // BLF 文件头签名是 "LOGG"（不是 "BLF"）
    if head.len() >= 4 && &head[0..4] == b"LOGG" {
        return Format::Blf;
    }
    let ascii_csv = head
        .iter()
        .take(4096)
        .all(|b| *b == b'\t' || *b == b'\n' || *b == b'\r' || (0x20..0x7f).contains(b));
    if ascii_csv && head.iter().any(|b| *b == b',' || *b == b'\t' || *b == b';') {
        return Format::Csv;
    }
    // ASC：文本头 `date …` / `base hex` / `Begin Triggerblock`，正文有 Rx/Tx 帧行
    if head.starts_with(b"date ")
        || head.windows(11).any(|w| w == b"base hex  t")
        || text_has(head, b"Triggerblock")
        || (text_has(head, b" CANFD ") && text_has(head, b" Rx "))
    {
        return Format::Asc;
    }
    Format::Unknown
}

fn text_has(hay: &[u8], needle: &[u8]) -> bool {
    hay.windows(needle.len()).any(|w| w == needle)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn detects_ulog() {
        assert_eq!(sniff(b"ULog\x00\x01\x0c\x00"), Format::Ulog);
    }

    #[test]
    fn detects_apbin() {
        assert_eq!(sniff(&[0xA3, 0x95, 0x80]), Format::ApBin);
    }

    fn detects_csv() {
        assert_eq!(sniff(b"time,att.roll\n0.0,1.2\n"), Format::Csv);
    }

    #[test]
    fn detects_blf_and_asc() {
        assert_eq!(sniff(b"LOGG\x90\x00\x00\x00"), Format::Blf);
        assert_eq!(sniff(b"date Thu Nov 12 10:00:00.000 2026"), Format::Asc);
        assert_eq!(
            sniff(b"   0.010000 CANFD   1 Rx  2B0  1 0 9 12 11"),
            Format::Asc
        );
    }

    #[test]
    fn unknown_for_binary_garbage() {
        assert_eq!(sniff(&[0xff, 0xfe, 0x00, 0x01]), Format::Unknown);
    }
}
