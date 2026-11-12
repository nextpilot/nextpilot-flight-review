//! CSV / TSV：先"探测"再"按用户选项解析"两步走，规则对齐 PlotJuggler 的
//! `data_load_csv`（`csv_dialog.cpp` / `csv_parser.hpp`）。
//!
//! 关键一条：**不假定第一列是时间**。PJ 的时间轴有三种模式——行号、
//! 指定列、日期列 + 时间列合并；默认是行号，必须由用户选过才按列走。
//!
//! 其余从 PJ 抄来的容错：
//! - 分隔符自动探测（`,` `;` `\t` 算"硬"分隔符，` ` 只在没有硬分隔符时才算）；
//!   两种以上硬分隔符同时出现 → 判为歧义，交用户选
//! - 首行是否表头要判断（整行都能解析成数字 → 当数据，自动生成列名）
//! - 重复列名加后缀；列数不匹配的行跳过并记警告；空行跳过
//! - 时间列非单调 → 自动按时间排序并告警；时间戳非法 → 该行跳过并计数
//! - 引号、BOM、CRLF 都吃掉

use serde::Serialize;
use std::fs;
use std::path::Path;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ColKind {
    Numeric,
    DateOnly,
    TimeOnly,
    Text,
}

#[derive(Debug, Clone, Serialize)]
pub struct Column {
    pub name: String,
    pub kind: ColKind,
}

/// 探测结果：够 UI 画一个"加载选项"对话框。
#[derive(Debug, Clone, Serialize)]
pub struct Probe {
    pub delimiter: char,
    pub delimiter_ambiguous: bool,
    pub has_header: bool,
    pub columns: Vec<Column>,
    pub preview: Vec<Vec<String>>,
    /// 猜测的时间列（None = 用行号）；只是建议，UI 里由用户确认
    pub suggested_time: Option<usize>,
    pub warnings: Vec<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TimeMode {
    RowNumber,
    Column(usize),
    /// 日期列 + 时间列合并成一个时间轴（PJ 的 "Multiple Columns"）
    Combined(usize, usize),
}

#[derive(Debug, Clone)]
pub struct Options {
    pub delimiter: char,
    pub has_header: bool,
    pub skip_rows: usize,
    pub time: TimeMode,
}

impl Default for Options {
    fn default() -> Self {
        Options { delimiter: ',', has_header: true, skip_rows: 0, time: TimeMode::RowNumber }
    }
}

#[derive(Debug, Clone, Default)]
pub struct Table {
    /// 表头；`headers[0]` 是时间轴
    pub headers: Vec<String>,
    /// 列数据；`columns[0]` 已换算成秒（行号模式则是序号）
    pub columns: Vec<Vec<f64>>,
    pub time_scale: f64,
    pub warnings: Vec<String>,
    pub rows: usize,
}

// ---------------------------------------------------------------- 词法

/// 按分隔符切一行，支持 `"..."` 引号与 `""` 转义；引号内的分隔符不切。
pub fn split_line(line: &str, sep: char, out: &mut Vec<String>) {
    out.clear();
    let mut cell = String::new();
    let mut in_quotes = false;
    let mut chars = line.chars().peekable();
    while let Some(c) = chars.next() {
        match c {
            '"' => {
                if in_quotes && chars.peek() == Some(&'"') {
                    chars.next();
                    cell.push('"');
                } else {
                    in_quotes = !in_quotes;
                }
            }
            _ if c == sep && !in_quotes => {
                out.push(cell.trim().to_string());
                cell.clear();
            }
            _ => cell.push(c),
        }
    }
    out.push(cell.trim().to_string());
}

pub struct DelimiterGuess {
    pub delimiter: char,
    pub ambiguous: bool,
}

/// 与 PJ `DetectDelimiterEx` 同规则：硬分隔符里数出现最多的；两个以上都出现 → 歧义。
pub fn detect_delimiter(line: &str) -> DelimiterGuess {
    let mut counts = [(',', 0usize), (';', 0), ('\t', 0), (' ', 0)];
    // 引号内的分隔符不算
    let mut in_quotes = false;
    for c in line.chars() {
        if c == '"' {
            in_quotes = !in_quotes;
            continue;
        }
        if in_quotes {
            continue;
        }
        for slot in counts.iter_mut() {
            if c == slot.0 {
                slot.1 += 1;
            }
        }
    }
    let present = counts
        .iter()
        .filter(|(c, n)| *c != ' ' && *n > 0)
        .count();
    let best = counts
        .iter()
        .filter(|(c, n)| *n > 0 && (*c != ' ' || present == 0))
        .max_by_key(|(_, n)| *n)
        .map(|(c, _)| *c);
    let delimiter = match best {
        // 只有空格能切时，说明是空格分隔的文本表
        Some(c) => c,
        None => ',',
    };
    DelimiterGuess { delimiter, ambiguous: present >= 2 }
}

// ---------------------------------------------------------------- 时间解析

/// 公历年月日 → 距 1970-01-01 的天数（Howard Hinnant 的 days_from_civil）
fn days_from_civil(y: i64, m: u32, d: u32) -> i64 {
    let y = if m <= 2 { y - 1 } else { y };
    let era = if y >= 0 { y } else { y - 399 } / 400;
    let yoe = (y - era * 400) as u32;
    let mp = if m > 2 { m - 3 } else { m + 9 };
    let doy = (153 * mp + 2) / 5 + d - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    (era * 146097 + doe as i64) - 719468
}

fn parse_date(s: &str) -> Option<i64> {
    // 2026-11-12 / 2026/11/12
    let parts: Vec<&str> = s.split(['-', '/']).collect();
    if parts.len() != 3 {
        return None;
    }
    let y: i64 = parts[0].parse().ok()?;
    let m: u32 = parts[1].parse().ok()?;
    let d: u32 = parts[2].parse().ok()?;
    if !(1..=12).contains(&m) || !(1..=31).contains(&d) {
        return None;
    }
    Some(days_from_civil(y, m, d) * 86400)
}

fn parse_time_of_day(s: &str) -> Option<f64> {
    // 10:00:00.123
    let parts: Vec<&str> = s.split(':').collect();
    if parts.len() < 2 || parts.len() > 3 {
        return None;
    }
    let h: f64 = parts[0].parse().ok()?;
    let m: f64 = parts[1].parse().ok()?;
    let sec: f64 = parts.get(2).map(|x| x.parse().unwrap_or(0.0)).unwrap_or(0.0);
    if !(0.0..24.0).contains(&h) || !(0.0..60.0).contains(&m) || !(0.0..61.0).contains(&sec) {
        return None;
    }
    Some(h * 3600.0 + m * 60.0 + sec)
}

/// 文本时间戳：`2026-11-12T10:00:00.123Z`、`2026-11-12 10:00:00`、纯 `10:00:00.5`
fn parse_datetime_text(s: &str) -> Option<f64> {
    let t = s.trim().trim_end_matches('Z');
    if let Some(pos) = t.find(['T', 't']) {
        let (d, rest) = t.split_at(pos);
        return Some(parse_date(d)? as f64 + parse_time_of_day(&rest[1..])?);
    }
    if let Some((d, rest)) = t.split_once(' ') {
        if let (Some(days), Some(tod)) = (parse_date(d), parse_time_of_day(rest)) {
            return Some(days as f64 + tod);
        }
    }
    parse_time_of_day(t)
}

fn is_numeric(s: &str) -> bool {
    s.trim().parse::<f64>().is_ok()
}

fn cell_kind(s: &str) -> ColKind {
    let t = s.trim();
    if t.is_empty() {
        return ColKind::Numeric; // 空单元格不算证据
    }
    if is_numeric(t) {
        return ColKind::Numeric;
    }
    if parse_date(t).is_some() {
        return ColKind::DateOnly;
    }
    if parse_time_of_day(t).is_some() {
        return ColKind::TimeOnly;
    }
    if parse_datetime_text(t).is_some() {
        return ColKind::Numeric; // 完整时间戳，按时间处理
    }
    ColKind::Text
}

/// 表头提示的时间单位（`time_ms` / `t[us]` …）
fn header_unit(name: &str) -> Option<f64> {
    let h = name.to_ascii_lowercase();
    for (needle, scale) in [("ns", 1e-9), ("us", 1e-6), ("µs", 1e-6), ("ms", 1e-3), ("sec", 1.0)] {
        if h.contains(needle) {
            return Some(scale);
        }
    }
    None
}

/// 数值时间戳的单位：优先表头提示，否则按数量级猜（epoch 有 10/13/16/19 位）
fn numeric_time_scale(values: &[f64], header: &str) -> f64 {
    if let Some(s) = header_unit(header) {
        return s;
    }
    let max = values.iter().filter(|v| v.is_finite()).fold(0.0f64, |a, b| a.max(b.abs()));
    if max >= 1e17 {
        1e-9
    } else if max >= 1e14 {
        1e-6
    } else if max >= 1e11 {
        1e-3
    } else {
        1.0
    }
}

// ---------------------------------------------------------------- 探测

fn dedupe_names(names: &mut [String]) {
    use std::collections::HashMap;
    let mut seen: HashMap<String, usize> = HashMap::new();
    for n in names.iter_mut() {
        let base = if n.trim().is_empty() { "Column".to_string() } else { n.trim().to_string() };
        let cnt = seen.entry(base.clone()).or_insert(0);
        *cnt += 1;
        *n = if *cnt > 1 { format!("{}_{}", base, cnt) } else { base };
    }
}

pub fn probe(text: &str) -> Result<Probe, String> {
    probe_with(text, None)
}

/// `forced` = 用户手选的分隔符（None 表示自动探测）。
pub fn probe_with(text: &str, forced: Option<char>) -> Result<Probe, String> {
    let text = text.strip_prefix('\u{feff}').unwrap_or(text);
    let mut lines = text.lines().filter(|l| !l.trim().is_empty());
    let first = lines.next().ok_or_else(|| "空文件".to_string())?;
    let mut guess = detect_delimiter(first);
    if let Some(d) = forced {
        guess.delimiter = d;
        guess.ambiguous = false;
    }

    let mut parts = Vec::new();
    split_line(first, guess.delimiter, &mut parts);
    if parts.len() < 2 {
        return Err(format!("只切出 {} 列，分隔符可能选错了", parts.len()));
    }

    // 首行是表头还是数据：只要有一个非空单元格不像数字/时间戳，就当表头
    let has_header = parts.iter().any(|p| !p.is_empty() && cell_kind(p) == ColKind::Text);
    let mut names: Vec<String> = if has_header {
        parts.clone()
    } else {
        (1..=parts.len()).map(|i| format!("Column {}", i)).collect()
    };
    dedupe_names(&mut names);

    // 预览：首行若是数据也算进去，最多 9 行
    let mut preview: Vec<Vec<String>> = Vec::new();
    if !has_header {
        preview.push(parts.clone());
    }
    for line in lines.take(9) {
        let mut p = Vec::new();
        split_line(line, guess.delimiter, &mut p);
        preview.push(p);
    }

    // 类型：靠预览里非空单元格投票
    let mut kinds = vec![ColKind::Numeric; names.len()];
    for (ci, kind) in kinds.iter_mut().enumerate() {
        let (mut text_votes, mut date_votes, mut time_votes) = (0usize, 0usize, 0usize);
        for row in &preview {
            if let Some(v) = row.get(ci) {
                match cell_kind(v) {
                    ColKind::Text => text_votes += 1,
                    ColKind::DateOnly => date_votes += 1,
                    ColKind::TimeOnly => time_votes += 1,
                    ColKind::Numeric => {}
                }
            }
        }
        *kind = if text_votes > 0 {
            ColKind::Text
        } else if date_votes > 0 {
            ColKind::DateOnly
        } else if time_votes > 0 {
            ColKind::TimeOnly
        } else {
            ColKind::Numeric
        };
    }

    // 建议时间列：优先名字里带 time/stamp/时间 的数值列，否则第一个数值列
    let suggested_time = names
        .iter()
        .enumerate()
        .find(|(i, n)| {
            let h = n.to_ascii_lowercase();
            kinds[*i] == ColKind::Numeric && (h.contains("time") || h.contains("stamp") || h == "t" || h.contains("时间"))
        })
        .map(|(i, _)| i)
        .or_else(|| (names.len() > 1 && kinds[0] == ColKind::Numeric).then_some(0));

    let mut warnings = Vec::new();
    if guess.ambiguous {
        warnings.push("首行里出现两种以上硬分隔符，自动探测不可靠，请手动选".into());
    }
    if !has_header {
        warnings.push("首行看起来是数据而不是表头，已自动生成列名（可手动改）".into());
    }
    let n_cols = names.len();
    let mismatch = preview.iter().filter(|row| row.len() != n_cols).count();
    if mismatch > 0 {
        warnings.push(format!("预览中有 {} 行列数不是 {}，加载时会跳过", mismatch, n_cols));
    }

    Ok(Probe {
        delimiter: guess.delimiter,
        delimiter_ambiguous: guess.ambiguous,
        has_header,
        columns: names
            .into_iter()
            .zip(kinds)
            .map(|(name, kind)| Column { name, kind })
            .collect(),
        preview,
        suggested_time,
        warnings,
    })
}

// ---------------------------------------------------------------- 解析

pub fn parse_with(text: &str, opt: &Options) -> Result<Table, String> {
    let text = text.strip_prefix('\u{feff}').unwrap_or(text);
    let lines: Vec<&str> = text.lines().filter(|l| !l.trim().is_empty()).skip(opt.skip_rows).collect();
    if lines.is_empty() {
        return Err("空文件".into());
    }

    let mut headers: Vec<String> = Vec::new();
    let data_start = if opt.has_header {
        let mut head = Vec::new();
        split_line(lines[0], opt.delimiter, &mut head);
        headers = head;
        dedupe_names(&mut headers);
        1
    } else {
        0
    };

    let mut raw: Vec<Vec<String>> = Vec::with_capacity(lines.len());
    for line in &lines[data_start..] {
        let mut p = Vec::new();
        split_line(line, opt.delimiter, &mut p);
        raw.push(p);
    }

    // 没有表头就按最大列数生成名字
    if headers.is_empty() {
        let n = raw.iter().map(|r| r.len()).max().unwrap_or(0);
        headers = (1..=n).map(|i| format!("Column {}", i)).collect();
    }
    let n_cols = headers.len();

    let mut warnings: Vec<String> = Vec::new();
    let mut skipped_shape = 0usize;
    let mut kept: Vec<Vec<String>> = Vec::with_capacity(raw.len());
    for row in raw {
        if row.len() != n_cols {
            skipped_shape += 1;
            continue;
        }
        kept.push(row);
    }
    if skipped_shape > 0 {
        warnings.push(format!("{} 行列数不匹配，已跳过", skipped_shape));
    }
    if kept.is_empty() {
        return Err("没有一条数据行可用".into());
    }

    // 时间轴
    let (t_raw, time_name, time_scale, bad_ts) = build_time(&kept, &headers, opt);
    if bad_ts > 0 {
        warnings.push(format!("{} 行时间戳非法，已跳过", bad_ts));
    }

    // 只保留时间合法的行
    let mut rows_ok: Vec<&Vec<String>> = Vec::with_capacity(kept.len());
    let mut t_ok: Vec<f64> = Vec::with_capacity(kept.len());
    for (i, row) in kept.iter().enumerate() {
        if t_raw[i].is_finite() {
            rows_ok.push(row);
            t_ok.push(t_raw[i]);
        }
    }
    if rows_ok.is_empty() {
        return Err("时间列没有一条是合法数值".into());
    }

    // 非单调 → 排序（与 PJ 一样"自动排序并告警"）
    let mut idx: Vec<usize> = (0..t_ok.len()).collect();
    let sorted = t_ok.windows(2).all(|w| w[1] >= w[0]);
    if !sorted {
        idx.sort_by(|a, b| t_ok[*a].partial_cmp(&t_ok[*b]).unwrap_or(std::cmp::Ordering::Equal));
        warnings.push("时间列不是单调递增，已按时间重排".into());
    }

    // 时间列不再重复当通道（合并模式要跳两列）
    let skip: Vec<usize> = match opt.time {
        TimeMode::RowNumber => Vec::new(),
        TimeMode::Column(c) => vec![c],
        TimeMode::Combined(d, t) => vec![d, t],
    };

    let mut columns: Vec<Vec<f64>> = Vec::with_capacity(n_cols + 1);
    columns.push(idx.iter().map(|&i| t_ok[i]).collect());
    let mut out_headers = vec![time_name];
    for c in 0..n_cols {
        if skip.contains(&c) {
            continue;
        }
        columns.push(idx.iter().map(|&i| parse_number(&rows_ok[i][c])).collect());
        out_headers.push(headers[c].clone());
    }
    Ok(Table {
        headers: out_headers,
        columns,
        time_scale,
        warnings,
        rows: idx.len(),
    })
}

fn parse_number(s: &str) -> f64 {
    let t = s.trim();
    if t.is_empty() {
        return f64::NAN;
    }
    t.parse::<f64>().unwrap_or(f64::NAN)
}

/// 返回 (秒值, 时间列名, 单位系数, 非法行数)
fn build_time(rows: &[Vec<String>], headers: &[String], opt: &Options) -> (Vec<f64>, String, f64, usize) {
    match opt.time {
        TimeMode::RowNumber => {
            let t: Vec<f64> = (0..rows.len()).map(|i| i as f64).collect();
            (t, "row #".to_string(), 1.0, 0)
        }
        TimeMode::Column(c) => {
            let name = headers.get(c).cloned().unwrap_or_else(|| format!("Column {}", c + 1));
            let mut vals = Vec::with_capacity(rows.len());
            let mut bad = 0usize;
            for row in rows {
                let cell = row.get(c).map(|s| s.as_str()).unwrap_or("");
                if let Some(sec) = parse_datetime_text(cell) {
                    vals.push(sec);
                } else if is_numeric(cell) {
                    vals.push(parse_number(cell));
                } else {
                    vals.push(f64::NAN);
                    bad += 1;
                }
            }
            // 只有纯数值时才谈单位；文本时间戳已经在 parse_datetime_text 里换算成秒
            let all_numeric = rows.iter().all(|r| {
                r.get(c).map(|s| s.trim().is_empty() || is_numeric(s)).unwrap_or(true)
            });
            let scale = if all_numeric { numeric_time_scale(&vals, &name) } else { 1.0 };
            let t = vals.iter().map(|v| v * scale).collect();
            let bad = if all_numeric { bad } else { 0 };
            (t, name, scale, bad)
        }
        TimeMode::Combined(d, tm) => {
            let mut vals = Vec::with_capacity(rows.len());
            let mut bad = 0usize;
            for row in rows {
                let ds = row.get(d).map(|s| s.as_str()).unwrap_or("");
                let ts = row.get(tm).map(|s| s.as_str()).unwrap_or("");
                match (parse_date(ds), parse_time_of_day(ts)) {
                    (Some(days), Some(tod)) => vals.push(days as f64 + tod),
                    _ => {
                        vals.push(f64::NAN);
                        bad += 1;
                    }
                }
            }
            let name = format!(
                "{} + {}",
                headers.get(d).map(|s| s.as_str()).unwrap_or("date"),
                headers.get(tm).map(|s| s.as_str()).unwrap_or("time")
            );
            (vals, name, 1.0, bad)
        }
    }
}

/// 兼容旧接口：自动探测 + 采纳建议（表头猜测、建议时间列）。
pub fn parse(text: &str) -> Result<Table, String> {
    let p = probe(text)?;
    let time = match p.suggested_time {
        Some(i) => TimeMode::Column(i),
        None => TimeMode::RowNumber,
    };
    parse_with(text, &Options { delimiter: p.delimiter, has_header: p.has_header, skip_rows: 0, time })
}

pub fn parse_file(path: impl AsRef<Path>) -> Result<Table, String> {
    let text = fs::read_to_string(path.as_ref()).map_err(|e| format!("读取失败: {e}"))?;
    parse(&text)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn detects_header_and_time_column() {
        let p = probe("time_ms,roll\n0,1.0\n10,1.5\n").unwrap();
        assert!(p.has_header);
        assert_eq!(p.suggested_time, Some(0));
        assert_eq!(p.columns.len(), 2);
    }

    #[test]
    fn no_header_when_first_row_numeric() {
        let p = probe("0,1.0\n10,1.5\n").unwrap();
        assert!(!p.has_header, "首行全是数字就该当数据");
        assert_eq!(p.columns[0].name, "Column 1");
    }

    #[test]
    fn time_is_not_assumed_to_be_first_column() {
        // 时间是第 2 列，第 1 列是标签
        let text = "label,t_ms,val\na,0,5\nb,10,6\nc,20,7\n";
        let p = probe(text).unwrap();
        assert_eq!(p.columns[1].kind, ColKind::Numeric);
        let t = parse_with(
            text,
            &Options { delimiter: ',', has_header: true, skip_rows: 0, time: TimeMode::Column(1) },
        )
        .unwrap();
        assert_eq!(t.headers[0], "t_ms");
        assert!((t.columns[0][1] - 0.01).abs() < 1e-12, "ms → 秒");
        assert!(t.columns[1][0].is_nan(), "label 列不是数字");
    }

    #[test]
    fn row_number_mode_ignores_values() {
        let t = parse_with(
            "a,b\nx,1\ny,2\n",
            &Options { delimiter: ',', has_header: true, skip_rows: 0, time: TimeMode::RowNumber },
        )
        .unwrap();
        assert_eq!(t.columns[0], vec![0.0, 1.0]);
        assert_eq!(t.headers[0], "row #");
    }

    #[test]
    fn non_monotonic_gets_sorted() {
        let text = "t,v\n1,10\n0,20\n2,30\n";
        let t = parse_with(
            text,
            &Options { delimiter: ',', has_header: true, skip_rows: 0, time: TimeMode::Column(0) },
        )
        .unwrap();
        assert_eq!(t.columns[0], vec![0.0, 1.0, 2.0]);
        assert_eq!(t.columns[1], vec![20.0, 10.0, 30.0]);
        assert!(t.warnings.iter().any(|w| w.contains("单调")));
    }

    #[test]
    fn combined_date_time_columns() {
        let text = "date,tod,val\n2026-11-12,10:00:00.500,1\n2026-11-12,10:00:01.500,2\n";
        let p = probe(text).unwrap();
        assert_eq!(p.columns[0].kind, ColKind::DateOnly);
        assert_eq!(p.columns[1].kind, ColKind::TimeOnly);
        let t = parse_with(
            text,
            &Options { delimiter: ',', has_header: true, skip_rows: 0, time: TimeMode::Combined(0, 1) },
        )
        .unwrap();
        assert!(t.columns[0][0] > 1.6e9);
        assert!((t.columns[0][1] - t.columns[0][0] - 1.0).abs() < 1e-9);
    }

    #[test]
    fn skip_rows_and_shape_mismatch() {
        let text = "# exported by tool\nt,v\n0,1\n1,2\nbroken,row,extra\n";
        let t = parse_with(
            text,
            &Options { delimiter: ',', has_header: true, skip_rows: 1, time: TimeMode::Column(0) },
        )
        .unwrap();
        assert_eq!(t.rows, 2);
        assert!(t.warnings.iter().any(|w| w.contains("列数不匹配")));
    }

    #[test]
    fn quoted_fields_with_separator_inside() {
        let mut parts = Vec::new();
        split_line("a,\"b,c\",d", ',', &mut parts);
        assert_eq!(parts, vec!["a", "b,c", "d"]);
    }
}
