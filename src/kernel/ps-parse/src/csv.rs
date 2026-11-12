//! CSV / TSV 解析：第一列是时间（自动按表头识别 ns/us/ms/s），其余列为通道。
//! 只做"读成列"，不做单位换算与派生；派生交给 ps-ops / Lua。

use std::fs;
use std::path::Path;

#[derive(Debug, Clone, Default)]
pub struct Table {
    /// 表头（含第一列时间列名）
    pub headers: Vec<String>,
    /// 列数据；columns[0] 已换算成秒
    pub columns: Vec<Vec<f64>>,
    /// 时间列原始单位换算到秒的系数（用于界面上提示"识别为 ms"）
    pub time_scale: f64,
}

fn delimiter_of(line: &str) -> char {
    let comma = line.matches(',').count();
    let semi = line.matches(';').count();
    let tab = line.matches('\t').count();
    if tab >= comma && tab >= semi && tab > 0 {
        '\t'
    } else if semi > comma {
        ';'
    } else {
        ','
    }
}

fn time_scale_of(header: &str) -> (f64, &'static str) {
    let h = header.trim().to_ascii_lowercase();
    if h.contains("[ns]") || h.contains("_ns") || h.contains("(ns)") {
        (1e-9, "ns")
    } else if h.contains("[us]") || h.contains("_us") || h.contains("(us)") || h.contains("µs") {
        (1e-6, "us")
    } else if h.contains("[ms]") || h.contains("_ms") || h.contains("(ms)") {
        (1e-3, "ms")
    } else {
        (1.0, "s")
    }
}

fn parse_cell(raw: &str) -> f64 {
    let t = raw.trim().trim_matches('"');
    if t.is_empty() {
        return f64::NAN;
    }
    t.parse::<f64>().unwrap_or(f64::NAN)
}

/// 解析整份文本。返回 Err 只在"没有可用的表格结构"时发生；坏单元格记 NaN，不整份报废。
pub fn parse(text: &str) -> Result<Table, String> {
    let mut lines = text.lines().filter(|l| !l.trim().is_empty());
    let header_line = lines.next().ok_or_else(|| "空文件".to_string())?;
    let delim = delimiter_of(header_line);
    let headers: Vec<String> = header_line
        .split(delim)
        .map(|s| s.trim().trim_matches('"').to_string())
        .collect();
    if headers.len() < 2 {
        return Err(format!("只识别到 {} 列，至少需要时间列 + 1 个通道", headers.len()));
    }

    let (scale, _unit) = time_scale_of(&headers[0]);
    let mut columns: Vec<Vec<f64>> = vec![Vec::new(); headers.len()];
    for line in lines {
        let cells: Vec<&str> = line.split(delim).collect();
        for (i, col) in columns.iter_mut().enumerate() {
            let raw = cells.get(i).copied().unwrap_or("");
            let mut v = parse_cell(raw);
            if i == 0 && v.is_finite() {
                v *= scale;
            }
            col.push(v);
        }
    }

    Ok(Table { headers, columns, time_scale: scale })
}

pub fn parse_file(path: impl AsRef<Path>) -> Result<Table, String> {
    let text = fs::read_to_string(path.as_ref()).map_err(|e| format!("读取失败: {e}"))?;
    parse(&text)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_comma_with_ms_time() {
        let t = parse("time_ms,att.roll,acc_x\n0,1.0,2.0\n10,1.5,\n").unwrap();
        assert_eq!(t.headers.len(), 3);
        assert_eq!(t.columns[0], vec![0.0, 0.01]);
        assert_eq!(t.columns[1], vec![1.0, 1.5]);
        assert!(t.columns[2][1].is_nan());
        assert!((t.time_scale - 1e-3).abs() < 1e-12);
    }

    #[test]
    fn detects_semicolon() {
        let t = parse("t;x\n0;1\n1;2\n").unwrap();
        assert_eq!(t.headers, vec!["t", "x"]);
        assert_eq!(t.columns[1], vec![1.0, 2.0]);
    }

    #[test]
    fn rejects_single_column() {
        assert!(parse("only_time\n0\n1\n").is_err());
    }
}
