//! Typed Table/Column 核心模型（M0）。
//!
//! 这一层先独立于旧的 `Series`/`channel` 视图存在，避免一次性打穿 C ABI。
//! 后续 ABI、loader、ring 都迁到这里；旧模型在 M6 删除。

use std::sync::Arc;

/// 列值的原始类型。时间轴统一是 `i64` 微秒，不放在这里。
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ValueType {
    F64,
    F32,
    I64,
    U64,
    I32,
    U32,
    I16,
    U16,
    I8,
    U8,
    Bool,
    Text,
}

impl ValueType {
    /// 单值在内存中的字节数；`Text` 是变长，返回 0 由调用方单独计算。
    pub fn value_bytes(self) -> usize {
        match self {
            ValueType::F64 | ValueType::I64 | ValueType::U64 => 8,
            ValueType::F32 | ValueType::I32 | ValueType::U32 => 4,
            ValueType::I16 | ValueType::U16 => 2,
            ValueType::I8 | ValueType::U8 | ValueType::Bool => 1,
            ValueType::Text => 0,
        }
    }

    pub fn is_numeric(self) -> bool {
        self != ValueType::Text
    }
}

/// 一列的物理存储。列式布局，所有变体长度必须一致。
#[derive(Debug, Clone, PartialEq)]
pub enum ColumnData {
    F64(Vec<f64>),
    F32(Vec<f32>),
    I64(Vec<i64>),
    U64(Vec<u64>),
    I32(Vec<i32>),
    U32(Vec<u32>),
    I16(Vec<i16>),
    U16(Vec<u16>),
    I8(Vec<i8>),
    U8(Vec<u8>),
    Bool(Vec<bool>),
    Text(Vec<String>),
}

impl ColumnData {
    pub fn value_type(&self) -> ValueType {
        match self {
            ColumnData::F64(_) => ValueType::F64,
            ColumnData::F32(_) => ValueType::F32,
            ColumnData::I64(_) => ValueType::I64,
            ColumnData::U64(_) => ValueType::U64,
            ColumnData::I32(_) => ValueType::I32,
            ColumnData::U32(_) => ValueType::U32,
            ColumnData::I16(_) => ValueType::I16,
            ColumnData::U16(_) => ValueType::U16,
            ColumnData::I8(_) => ValueType::I8,
            ColumnData::U8(_) => ValueType::U8,
            ColumnData::Bool(_) => ValueType::Bool,
            ColumnData::Text(_) => ValueType::Text,
        }
    }

    pub fn len(&self) -> usize {
        match self {
            ColumnData::F64(v) => v.len(),
            ColumnData::F32(v) => v.len(),
            ColumnData::I64(v) => v.len(),
            ColumnData::U64(v) => v.len(),
            ColumnData::I32(v) => v.len(),
            ColumnData::U32(v) => v.len(),
            ColumnData::I16(v) => v.len(),
            ColumnData::U16(v) => v.len(),
            ColumnData::I8(v) => v.len(),
            ColumnData::U8(v) => v.len(),
            ColumnData::Bool(v) => v.len(),
            ColumnData::Text(v) => v.len(),
        }
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub fn clear(&mut self) {
        match self {
            ColumnData::F64(v) => v.clear(),
            ColumnData::F32(v) => v.clear(),
            ColumnData::I64(v) => v.clear(),
            ColumnData::U64(v) => v.clear(),
            ColumnData::I32(v) => v.clear(),
            ColumnData::U32(v) => v.clear(),
            ColumnData::I16(v) => v.clear(),
            ColumnData::U16(v) => v.clear(),
            ColumnData::I8(v) => v.clear(),
            ColumnData::U8(v) => v.clear(),
            ColumnData::Bool(v) => v.clear(),
            ColumnData::Text(v) => v.clear(),
        }
    }

    pub fn drop_prefix(&mut self, rows: usize) {
        if rows == 0 {
            return;
        }
        if rows >= self.len() {
            self.clear();
            return;
        }
        match self {
            ColumnData::F64(v) => { v.drain(0..rows); }
            ColumnData::F32(v) => { v.drain(0..rows); }
            ColumnData::I64(v) => { v.drain(0..rows); }
            ColumnData::U64(v) => { v.drain(0..rows); }
            ColumnData::I32(v) => { v.drain(0..rows); }
            ColumnData::U32(v) => { v.drain(0..rows); }
            ColumnData::I16(v) => { v.drain(0..rows); }
            ColumnData::U16(v) => { v.drain(0..rows); }
            ColumnData::I8(v) => { v.drain(0..rows); }
            ColumnData::U8(v) => { v.drain(0..rows); }
            ColumnData::Bool(v) => { v.drain(0..rows); }
            ColumnData::Text(v) => { v.drain(0..rows); }
        }
    }

    /// 追加同类型数据；类型不一致返回错误，调用方负责回滚。
    pub fn append(&mut self, other: ColumnData) -> Result<(), String> {
        if self.value_type() != other.value_type() {
            return Err("列类型不一致".into());
        }
        match (self, other) {
            (ColumnData::F64(a), ColumnData::F64(b)) => a.extend(b),
            (ColumnData::F32(a), ColumnData::F32(b)) => a.extend(b),
            (ColumnData::I64(a), ColumnData::I64(b)) => a.extend(b),
            (ColumnData::U64(a), ColumnData::U64(b)) => a.extend(b),
            (ColumnData::I32(a), ColumnData::I32(b)) => a.extend(b),
            (ColumnData::U32(a), ColumnData::U32(b)) => a.extend(b),
            (ColumnData::I16(a), ColumnData::I16(b)) => a.extend(b),
            (ColumnData::U16(a), ColumnData::U16(b)) => a.extend(b),
            (ColumnData::I8(a), ColumnData::I8(b)) => a.extend(b),
            (ColumnData::U8(a), ColumnData::U8(b)) => a.extend(b),
            (ColumnData::Bool(a), ColumnData::Bool(b)) => a.extend(b),
            (ColumnData::Text(a), ColumnData::Text(b)) => a.extend(b),
            _ => return Err("列类型不一致".into()),
        }
        Ok(())
    }

    /// 按行读取并转成 f64，供绘图/光标使用。`Text` 尝试解析数字。
    pub fn value_as_f64(&self, row: usize) -> Option<f64> {
        match self {
            ColumnData::F64(v) => v.get(row).copied(),
            ColumnData::F32(v) => v.get(row).map(|x| *x as f64),
            ColumnData::I64(v) => v.get(row).map(|x| *x as f64),
            ColumnData::U64(v) => v.get(row).map(|x| *x as f64),
            ColumnData::I32(v) => v.get(row).map(|x| *x as f64),
            ColumnData::U32(v) => v.get(row).map(|x| *x as f64),
            ColumnData::I16(v) => v.get(row).map(|x| *x as f64),
            ColumnData::U16(v) => v.get(row).map(|x| *x as f64),
            ColumnData::I8(v) => v.get(row).map(|x| *x as f64),
            ColumnData::U8(v) => v.get(row).map(|x| *x as f64),
            ColumnData::Bool(v) => v.get(row).map(|x| if *x { 1.0 } else { 0.0 }),
            ColumnData::Text(v) => v.get(row).and_then(|x| x.parse::<f64>().ok()),
        }
    }

    pub fn value_text(&self, row: usize) -> Option<String> {
        match self {
            ColumnData::F64(v) => v.get(row).map(|x| x.to_string()),
            ColumnData::F32(v) => v.get(row).map(|x| x.to_string()),
            ColumnData::I64(v) => v.get(row).map(|x| x.to_string()),
            ColumnData::U64(v) => v.get(row).map(|x| x.to_string()),
            ColumnData::I32(v) => v.get(row).map(|x| x.to_string()),
            ColumnData::U32(v) => v.get(row).map(|x| x.to_string()),
            ColumnData::I16(v) => v.get(row).map(|x| x.to_string()),
            ColumnData::U16(v) => v.get(row).map(|x| x.to_string()),
            ColumnData::I8(v) => v.get(row).map(|x| x.to_string()),
            ColumnData::U8(v) => v.get(row).map(|x| x.to_string()),
            ColumnData::Bool(v) => v.get(row).map(|x| x.to_string()),
            ColumnData::Text(v) => v.get(row).cloned(),
        }
    }
}

/// 每列一份 null 位图：1 表示该行无效。空位图表示全部有效。
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct NullBits {
    bits: Vec<u8>,
}

impl NullBits {
    pub fn new(len: usize) -> Self {
        Self { bits: vec![0; len.div_ceil(8)] }
    }

    pub fn is_null(&self, row: usize) -> bool {
        self.bits
            .get(row / 8)
            .map(|b| (b & (1 << (row % 8))) != 0)
            .unwrap_or(true)
    }

    pub fn set_null(&mut self, row: usize, null: bool) {
        let byte = row / 8;
        if byte >= self.bits.len() {
            self.bits.resize(byte + 1, 0);
        }
        let mask = 1u8 << (row % 8);
        if null {
            self.bits[byte] |= mask;
        } else {
            self.bits[byte] &= !mask;
        }
    }

    pub fn ensure_len(&mut self, len: usize) {
        let bytes = len.div_ceil(8);
        if self.bits.len() < bytes {
            self.bits.resize(bytes, 0);
        }
    }

    pub fn from_raw(bytes: &[u8], len: usize) -> Self {
        let mut bits = bytes.to_vec();
        bits.resize(len.div_ceil(8), 0);
        Self { bits }
    }

    pub fn raw(&self) -> &[u8] {
        &self.bits
    }

    /// 丢弃最旧的 `rows` 行，位图整体前移。尾部多余位清零。
    pub fn drop_prefix(&mut self, rows: usize) {
        if rows == 0 {
            return;
        }
        let total = self.bits.len() * 8;
        if rows >= total {
            self.bits.clear();
            return;
        }
        let mut shifted = vec![0u8; self.bits.len()];
        for row in rows..total {
            if self.is_null(row) {
                let dst = row - rows;
                shifted[dst / 8] |= 1u8 << (dst % 8);
            }
        }
        self.bits = shifted;
    }

    pub fn is_empty(&self) -> bool {
        self.bits.is_empty()
    }
}

/// 一列：名字、单位、typed values、null bitmap。
#[derive(Debug, Clone)]
pub struct Column {
    pub name: String,
    pub unit: String,
    pub data: ColumnData,
    pub nulls: NullBits,
}

impl Column {
    pub fn new(name: impl Into<String>, unit: impl Into<String>, data: ColumnData) -> Result<Self, String> {
        let name = name.into();
        if name.is_empty() {
            return Err("列名为空".into());
        }
        let len = data.len();
        let nulls = NullBits::new(len);
        Ok(Self { name, unit: unit.into(), data, nulls })
    }

    pub fn with_nulls(
        name: impl Into<String>,
        unit: impl Into<String>,
        data: ColumnData,
        mut nulls: NullBits,
    ) -> Result<Self, String> {
        let name = name.into();
        if name.is_empty() {
            return Err("列名为空".into());
        }
        nulls.ensure_len(data.len());
        Ok(Self { name, unit: unit.into(), data, nulls })
    }

    pub fn len(&self) -> usize {
        self.data.len()
    }

    pub fn is_empty(&self) -> bool {
        self.data.is_empty()
    }

    /// 追加一批值 + 对应 null 位图（位图从 0 开始，只描述本批）。
    pub fn append(&mut self, values: ColumnData, nulls: NullBits) -> Result<(), String> {
        if self.value_type() != values.value_type() {
            return Err("列类型不一致".into());
        }
        let add = values.len();
        let start = self.len();
        self.data.append(values)?;
        self.nulls.ensure_len(self.len());
        for i in 0..add {
            if nulls.is_null(i) {
                self.nulls.set_null(start + i, true);
            }
        }
        Ok(())
    }

    pub fn value_type(&self) -> ValueType {
        self.data.value_type()
    }

    /// ring 裁剪：丢弃最旧的 `rows` 行，值和 null 位图同步前移。
    pub fn drop_prefix(&mut self, rows: usize) {
        if rows == 0 {
            return;
        }
        if rows >= self.len() {
            self.data.clear();
            self.nulls = NullBits::default();
            return;
        }
        self.data.drop_prefix(rows);
        self.nulls.drop_prefix(rows);
    }

    pub fn is_null(&self, row: usize) -> bool {
        self.nulls.is_null(row)
    }

    /// 绘图/光标用：null 或越界返回 None。
    pub fn value_as_f64(&self, row: usize) -> Option<f64> {
        if row >= self.data.len() || self.nulls.is_null(row) {
            return None;
        }
        self.data.value_as_f64(row)
    }

    pub fn value_text(&self, row: usize) -> Option<String> {
        if row >= self.data.len() || self.nulls.is_null(row) {
            return None;
        }
        self.data.value_text(row)
    }

    pub fn set_null(&mut self, row: usize, null: bool) {
        self.nulls.set_null(row, null);
    }
}

/// 一张表：一份 UTC 微秒时间轴 + 表内序号 + 多列。
#[derive(Debug, Clone)]
pub struct Table {
    pub name: String,
    /// 所有列共享这一份时间轴。
    pub time_us: Arc<Vec<i64>>,
    /// 表内序号，从 0 开始；同一微秒内多条记录靠它排序。
    pub seq: Vec<u64>,
    pub columns: Vec<Column>,
    /// 该表的 ring 字节预算；`None` 表示不裁剪（离线 Dataset）。
    pub ring_budget_bytes: Option<usize>,
    /// 下一个 seq。ring 裁剪后仍保持表内单调递增，不归零。
    next_seq: u64,
}

impl Table {
    pub fn new(name: impl Into<String>, time_us: Vec<i64>) -> Result<Self, String> {
        let name = name.into();
        if name.is_empty() {
            return Err("表名为空".into());
        }
        if time_us.windows(2).any(|w| w[1] < w[0]) {
            return Err(format!("表 {name} 的时间轴不是单调非降"));
        }
        let n = time_us.len();
        let seq = (0..n as u64).collect();
        Ok(Self {
            name,
            time_us: Arc::new(time_us),
            seq,
            columns: Vec::new(),
            ring_budget_bytes: None,
            next_seq: n as u64,
        })
    }

    /// 单行近似字节数：时间 + seq + 各列值 + 每列 1 bit null。
    /// Text 列按固定 32 字节估算（变长字符串不适合精确预算）。
    pub fn row_bytes(&self) -> usize {
        let mut bytes = 16usize;
        for c in &self.columns {
            bytes += match c.value_type() {
                ValueType::Text => 32,
                t => t.value_bytes(),
            };
        }
        bytes + self.columns.len().div_ceil(8)
    }

    /// 按字节预算裁剪最旧的行。只在 append 时调用，避免每帧扫描。
    pub fn trim_to_budget(&mut self) {
        let Some(budget) = self.ring_budget_bytes else { return };
        let row_bytes = self.row_bytes();
        if row_bytes == 0 || budget == 0 {
            return;
        }
        let max_rows = (budget / row_bytes).max(1);
        let rows = self.row_count();
        if rows <= max_rows {
            return;
        }
        let drop = rows - max_rows;
        Arc::make_mut(&mut self.time_us).drain(0..drop);
        self.seq.drain(0..drop);
        for c in &mut self.columns {
            c.drop_prefix(drop);
        }
    }

    pub fn set_ring_budget(&mut self, bytes: Option<usize>) {
        self.ring_budget_bytes = bytes;
        self.trim_to_budget();
    }

    pub fn row_count(&self) -> usize {
        self.time_us.len()
    }

    pub fn is_empty(&self) -> bool {
        self.time_us.is_empty()
    }

    pub fn add_column(&mut self, column: Column) -> Result<(), String> {
        if column.len() != self.row_count() {
            return Err(format!(
                "列 {} 长度 {} 与表 {} 行数 {} 不一致",
                column.name,
                column.len(),
                self.name,
                self.row_count()
            ));
        }
        if self.columns.iter().any(|c| c.name == column.name) {
            return Err(format!("列已存在：{}", column.name));
        }
        self.columns.push(column);
        Ok(())
    }

    pub fn column_index(&self, name: &str) -> Option<usize> {
        self.columns.iter().position(|c| c.name == name)
    }

    /// 追加一批行：时间轴单调、列顺序/类型必须与首次建表一致；seq 由表内自动递增。
    pub fn append_rows(
        &mut self,
        time_us: &[i64],
        columns: Vec<(ColumnData, NullBits)>,
    ) -> Result<(), String> {
        if columns.len() != self.columns.len() {
            return Err(format!(
                "列数 {} 与表 {} 已有列数 {} 不一致",
                columns.len(),
                self.name,
                self.columns.len()
            ));
        }
        let rows = time_us.len();
        if rows == 0 {
            return Err("追加行数为 0".into());
        }
        if let Some(last) = self.time_us.last() {
            if time_us[0] < *last {
                return Err(format!("表 {} 的时间轴不是单调非降", self.name));
            }
        }
        for (i, (data, _)) in columns.iter().enumerate() {
            if data.len() != rows {
                return Err(format!(
                    "列 {} 长度 {} 与追加行数 {} 不一致",
                    self.columns[i].name,
                    data.len(),
                    rows
                ));
            }
            if data.value_type() != self.columns[i].value_type() {
                return Err(format!("列 {} 类型不一致", self.columns[i].name));
            }
        }
        Arc::make_mut(&mut self.time_us).extend_from_slice(time_us);
        let start_seq = self.next_seq;
        self.next_seq += rows as u64;
        self.seq.extend(start_seq..start_seq + rows as u64);
        for (i, (data, nulls)) in columns.into_iter().enumerate() {
            self.columns[i].append(data, nulls)?;
        }
        self.trim_to_budget();
        Ok(())
    }

    pub fn column(&self, name: &str) -> Option<&Column> {
        self.column_index(name).and_then(|i| self.columns.get(i))
    }

    pub fn time_sec(&self, row: usize) -> Option<f64> {
        self.time_us.get(row).map(|t| *t as f64 / 1_000_000.0)
    }

    /// 第一个 `time_us >= t` 的行。
    pub fn lower_bound(&self, t_us: i64) -> usize {
        self.time_us.partition_point(|x| *x < t_us)
    }

    /// 第一个 `time_us > t` 的行。
    pub fn upper_bound(&self, t_us: i64) -> usize {
        self.time_us.partition_point(|x| *x <= t_us)
    }

    /// 视口 LOD：窗口内分桶，输出 `(time_sec, min, max)` 三元组。
    /// 只扫描窗口行；列是 typed 的，读值时转 f64。
    pub fn lod_minmax(
        &self,
        column: usize,
        t0_us: i64,
        t1_us: i64,
        px: usize,
        out: &mut [f64],
    ) -> usize {
        let cap = out.len() / 3;
        if cap == 0 || !(t1_us > t0_us) {
            return 0;
        }
        let Some(col) = self.columns.get(column) else {
            return 0;
        };
        let start = self.lower_bound(t0_us);
        let end = self.upper_bound(t1_us);
        if start >= end {
            return 0;
        }
        let px = px.max(1).min(cap);
        let span = (t1_us - t0_us) as f64;
        let mut count = 0usize;
        let mut cur_bucket = usize::MAX;

        for row in start..end {
            let t = self.time_us[row] as f64;
            let b = (((t - t0_us as f64) / span) * px as f64).floor();
            let b = if b < 0.0 { 0 } else { (b as usize).min(px - 1) };
            if b != cur_bucket {
                if count >= cap {
                    break;
                }
                cur_bucket = b;
                let base = count * 3;
                out[base] = self.time_us[row] as f64 / 1_000_000.0;
                out[base + 1] = f64::INFINITY;
                out[base + 2] = f64::NEG_INFINITY;
                count += 1;
            }
            if let Some(v) = col.value_as_f64(row) {
                if v.is_finite() {
                    let base = (count - 1) * 3;
                    out[base + 1] = out[base + 1].min(v);
                    out[base + 2] = out[base + 2].max(v);
                }
            }
        }

        for i in 0..count {
            let base = i * 3;
            if !out[base + 1].is_finite() || !out[base + 2].is_finite() {
                out[base + 1] = f64::NAN;
                out[base + 2] = f64::NAN;
            }
        }
        count
    }
}

/// Dataset = 库，Table = 表；表名在 Dataset 内唯一。
#[derive(Debug, Clone, Default)]
pub struct Dataset {
    pub tables: Vec<Table>,
    /// 新建表的默认 ring 字节预算；`None` 表示离线 Dataset 不裁剪。
    pub default_ring_budget_bytes: Option<usize>,
}

impl Dataset {
    pub fn new() -> Self {
        Self { tables: Vec::new(), default_ring_budget_bytes: None }
    }

    pub fn len(&self) -> usize {
        self.tables.len()
    }

    pub fn is_empty(&self) -> bool {
        self.tables.is_empty()
    }

    pub fn table(&self, index: usize) -> Option<&Table> {
        self.tables.get(index)
    }

    pub fn table_mut(&mut self, index: usize) -> Option<&mut Table> {
        self.tables.get_mut(index)
    }

    pub fn table_index(&self, name: &str) -> Option<usize> {
        self.tables.iter().position(|t| t.name == name)
    }

    pub fn find_table(&self, name: &str) -> Option<&Table> {
        self.table_index(name).and_then(|i| self.tables.get(i))
    }

    pub fn add_table(&mut self, name: impl Into<String>, time_us: Vec<i64>) -> Result<usize, String> {
        let mut table = Table::new(name, time_us)?;
        table.ring_budget_bytes = self.default_ring_budget_bytes;
        if self.tables.iter().any(|t| t.name == table.name) {
            return Err(format!("表已存在：{}", table.name));
        }
        table.trim_to_budget();
        self.tables.push(table);
        Ok(self.tables.len() - 1)
    }

    pub fn add_table_with_columns(
        &mut self,
        name: impl Into<String>,
        time_us: Vec<i64>,
        columns: Vec<Column>,
    ) -> Result<usize, String> {
        let mut table = Table::new(name, time_us)?;
        table.ring_budget_bytes = self.default_ring_budget_bytes;
        for c in columns {
            table.add_column(c)?;
        }
        if self.tables.iter().any(|t| t.name == table.name) {
            return Err(format!("表已存在：{}", table.name));
        }
        table.trim_to_budget();
        self.tables.push(table);
        Ok(self.tables.len() - 1)
    }

    /// 设置 ring 预算：`table = None` 设默认值并应用到所有表；指定表名只改该表。
    pub fn set_ring_budget(&mut self, table: Option<&str>, bytes: Option<usize>) -> Result<(), String> {
        if let Some(name) = table {
            let idx = self.table_index(name).ok_or_else(|| format!("表不存在：{name}"))?;
            self.tables[idx].set_ring_budget(bytes);
        } else {
            self.default_ring_budget_bytes = bytes;
            for t in &mut self.tables {
                t.set_ring_budget(bytes);
            }
        }
        Ok(())
    }

    pub fn add_column(&mut self, table: usize, column: Column) -> Result<(), String> {
        self.tables.get_mut(table).ok_or_else(|| "表不存在".to_string())?.add_column(column)
    }

    /// frame_batch 的宿主落库入口：表不存在则按首批列建表，存在则校验列顺序后追加行。
    /// `columns` 依次为 `(name, unit, data, nulls)`；nulls 只描述本批行。
    pub fn append_frame(
        &mut self,
        table: &str,
        time_us: Vec<i64>,
        columns: Vec<(String, String, ColumnData, NullBits)>,
    ) -> Result<(), String> {
        if time_us.is_empty() {
            return Err("frame_batch 至少需要 1 行".into());
        }
        if let Some(idx) = self.table_index(table) {
            let t = &self.tables[idx];
            if columns.len() != t.columns.len() {
                return Err(format!("表 {table} 列数不一致"));
            }
            for (i, (name, unit, _, _)) in columns.iter().enumerate() {
                if t.columns[i].name != *name || t.columns[i].unit != *unit {
                    return Err(format!("表 {table} 第 {i} 列名称/单位与建表时不一致"));
                }
            }
            let rows = columns.into_iter().map(|(_, _, data, nulls)| (data, nulls)).collect();
            self.tables[idx].append_rows(&time_us, rows)
        } else {
            let mut cols = Vec::with_capacity(columns.len());
            for (name, unit, data, nulls) in columns {
                cols.push(Column::with_nulls(name, unit, data, nulls)?);
            }
            self.add_table_with_columns(table, time_us, cols).map(|_| ())
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn column(name: &str, data: ColumnData) -> Column {
        Column::new(name, "", data).unwrap()
    }

    #[test]
    fn typed_values_and_nulls() {
        let mut c = column("roll", ColumnData::F32(vec![1.0, 2.0, 3.0]));
        assert_eq!(c.value_type(), ValueType::F32);
        assert_eq!(c.value_as_f64(1), Some(2.0));
        c.set_null(1, true);
        assert!(c.is_null(1));
        assert_eq!(c.value_as_f64(1), None);
        assert_eq!(c.value_as_f64(2), Some(3.0));
    }

    #[test]
    fn integer_types_convert() {
        assert_eq!(column("i32", ColumnData::I32(vec![-2, 7])).value_as_f64(1), Some(7.0));
        assert_eq!(column("u8", ColumnData::U8(vec![0, 255])).value_as_f64(1), Some(255.0));
        assert_eq!(column("b", ColumnData::Bool(vec![false, true])).value_as_f64(1), Some(1.0));
        assert_eq!(column("s", ColumnData::Text(vec!["1.5".into(), "x".into()])).value_as_f64(0), Some(1.5));
        assert_eq!(column("s", ColumnData::Text(vec!["x".into()])).value_as_f64(0), None);
    }

    #[test]
    fn table_requires_same_row_count() {
        let mut t = Table::new("att", vec![0, 1_000_000]).unwrap();
        t.add_column(column("roll", ColumnData::F64(vec![1.0, 2.0]))).unwrap();
        assert!(t.add_column(column("pitch", ColumnData::F64(vec![1.0]))).is_err());
        assert!(t.add_column(column("roll", ColumnData::F64(vec![1.0, 2.0]))).is_err());
        assert_eq!(t.lower_bound(1_000_000), 1);
        assert_eq!(t.upper_bound(1_000_000), 2);
    }

    #[test]
    fn dataset_tables_are_unique() {
        let mut ds = Dataset::new();
        ds.add_table("att", vec![0, 1]).unwrap();
        assert!(ds.add_table("att", vec![0, 1]).is_err());
        assert!(ds.find_table("att").is_some());
        assert_eq!(ds.add_table_with_columns(
            "gps",
            vec![0, 1],
            vec![column("lat", ColumnData::F64(vec![1.0, 2.0]))],
        ).unwrap(), 1);
    }

    #[test]
    fn lod_skips_null_and_reads_typed() {
        let mut t = Table::new("att", vec![0, 100_000, 200_000, 300_000]).unwrap();
        let mut c = column("roll", ColumnData::F32(vec![1.0, 2.0, 3.0, 4.0]));
        c.set_null(1, true);
        t.add_column(c).unwrap();
        let mut out = [0.0; 9];
        let n = t.lod_minmax(0, 0, 300_000, 3, &mut out);
        assert_eq!(n, 3);
        assert_eq!(out[1], 1.0);
        assert!(out[4].is_nan());
        assert_eq!(out[8], 4.0);
    }

    #[test]
    fn lod_window_does_not_scan_outside() {
        let mut t = Table::new("att", vec![0, 1_000_000, 2_000_000, 3_000_000]).unwrap();
        t.add_column(column("roll", ColumnData::F64(vec![0.0, 1.0, 2.0, 3.0]))).unwrap();
        let mut out = [0.0; 3];
        let n = t.lod_minmax(0, 1_000_000, 2_000_000, 1, &mut out);
        assert_eq!(n, 1);
        assert_eq!(out[0], 1.0);
        assert_eq!(out[1], 1.0);
        assert_eq!(out[2], 2.0);
    }

    #[test]
    fn time_order_is_validated() {
        assert!(Table::new("bad", vec![1, 0]).is_err());
    }

    #[test]
    fn append_frame_creates_then_extends() {
        let mut ds = Dataset::new();
        let mut first_nulls = NullBits::new(2);
        first_nulls.set_null(1, true);
        ds.append_frame(
            "ATTITUDE",
            vec![0, 1_000],
            vec![(
                "roll".into(),
                "deg".into(),
                ColumnData::F32(vec![1.0, 2.0]),
                first_nulls,
            )],
        )
        .unwrap();
        let mut second_nulls = NullBits::new(1);
        second_nulls.set_null(0, true);
        ds.append_frame(
            "ATTITUDE",
            vec![2_000],
            vec![(
                "roll".into(),
                "deg".into(),
                ColumnData::F32(vec![3.0]),
                second_nulls,
            )],
        )
        .unwrap();
        let t = ds.find_table("ATTITUDE").unwrap();
        assert_eq!(t.row_count(), 3);
        assert_eq!(t.seq, vec![0, 1, 2]);
        assert_eq!(t.time_us.as_slice(), &[0, 1_000, 2_000]);
        assert_eq!(t.columns[0].value_as_f64(0), Some(1.0));
        assert_eq!(t.columns[0].value_as_f64(1), None);
        assert_eq!(t.columns[0].value_as_f64(2), None);
    }

    #[test]
    fn ring_drops_oldest_and_keeps_seq_monotonic() {
        let mut ds = Dataset::new();
        ds.set_ring_budget(None, Some(125)).unwrap(); // 每行约 25 字节，最多 5 行
        let times: Vec<i64> = (0..10).map(|i| i * 1_000).collect();
        let values: Vec<f64> = (0..10).map(|i| i as f64).collect();
        ds.append_frame(
            "att",
            times,
            vec![(
                "roll".into(),
                "deg".into(),
                ColumnData::F64(values),
                NullBits::new(10),
            )],
        )
        .unwrap();
        let t = ds.find_table("att").unwrap();
        assert_eq!(t.row_count(), 5);
        assert_eq!(t.time_us.as_slice(), &[5_000, 6_000, 7_000, 8_000, 9_000]);
        assert_eq!(t.seq, vec![5, 6, 7, 8, 9]);
        assert_eq!(t.columns[0].value_as_f64(0), Some(5.0));

        // 再追加 3 行，ring 继续丢最旧，seq 不归零。
        ds.append_frame(
            "att",
            vec![10_000, 11_000, 12_000],
            vec![(
                "roll".into(),
                "deg".into(),
                ColumnData::F64(vec![10.0, 11.0, 12.0]),
                NullBits::new(3),
            )],
        )
        .unwrap();
        let t = ds.find_table("att").unwrap();
        assert_eq!(t.row_count(), 5);
        assert_eq!(t.time_us.as_slice(), &[8_000, 9_000, 10_000, 11_000, 12_000]);
        assert_eq!(t.seq, vec![8, 9, 10, 11, 12]);
    }

    #[test]
    fn ring_keeps_null_bitmap_aligned() {
        let mut ds = Dataset::new();
        ds.set_ring_budget(None, Some(125)).unwrap();
        let mut nulls = NullBits::new(10);
        nulls.set_null(3, true);
        nulls.set_null(7, true);
        ds.append_frame(
            "att",
            (0..10).map(|i| i * 1_000).collect(),
            vec![(
                "roll".into(),
                "deg".into(),
                ColumnData::F64((0..10).map(|i| i as f64).collect()),
                nulls,
            )],
        )
        .unwrap();
        let t = ds.find_table("att").unwrap();
        // 原来的第 7 行变成现在的第 2 行；原第 3 行已被裁剪。
        assert!(!t.columns[0].is_null(0));
        assert!(t.columns[0].is_null(2));
        assert_eq!(t.columns[0].value_as_f64(2), None);
    }
}
