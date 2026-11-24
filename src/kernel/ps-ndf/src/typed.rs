//! Typed Table/Column 核心模型。
//!
//! 这一层先独立于旧的 `Series`/`channel` 视图存在，避免一次性打穿 C ABI。
//! 后续 ABI、loader、ring 都迁到这里；旧模型在 M6 删除。

use std::sync::Arc;

use crate::segments::{merge_order, PendingSegment};

/// 列值的原始类型。时间轴统一是 `i64` 纳秒，不放在这里。
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
    /// 对象引用：列内每行存一个 u64 句柄，payload 在 ObjectStore 里（S4）。
    Object,
}

impl ValueType {
    /// 单值在内存中的字节数；`Text` 是变长，返回 0 由调用方单独计算。
    /// `Object` 列内是 u64 句柄，故返回 8（payload 不在列内）。
    pub fn value_bytes(self) -> usize {
        match self {
            ValueType::F64 | ValueType::I64 | ValueType::U64 | ValueType::Object => 8,
            ValueType::F32 | ValueType::I32 | ValueType::U32 => 4,
            ValueType::I16 | ValueType::U16 => 2,
            ValueType::I8 | ValueType::U8 | ValueType::Bool => 1,
            ValueType::Text => 0,
        }
    }

    pub fn is_numeric(self) -> bool {
        matches!(self, ValueType::F64 | ValueType::F32 | ValueType::I64 | ValueType::U64
            | ValueType::I32 | ValueType::U32 | ValueType::I16 | ValueType::U16
            | ValueType::I8 | ValueType::U8 | ValueType::Bool)
    }
}

/// 对象列 payload 存储：连续字节池 + 稳定句柄表。
///
/// 句柄 = slot 索引 + 1（`0` 保留为 null）。slot 只增不复用，因此一个句柄
/// 永远标识同一个 payload；字节池 realloc 或后续追加都不会让已发出的句柄
/// 悬垂或串位——这是对象跨 C ABI 用句柄而非裸指针的核心（RFC D4）。
/// `refs` 是引用计数：dataset 自身持有第 1 个引用，C 侧可 retain/release 额外引用。
#[derive(Debug, Clone, Default)]
pub struct ObjectStore {
    bytes: Vec<u8>,
    entries: Vec<BlobEntry>,
}

#[derive(Debug, Clone)]
struct BlobEntry {
    offset: usize,
    len: usize,
    type_tag: u32,
    refs: u32,
    alive: bool,
}

impl ObjectStore {
    fn slot(handle: u64) -> Option<usize> {
        if handle == 0 { None } else { Some(handle as usize - 1) }
    }

    /// 写入 payload，返回稳定句柄（>0）。dataset 自身持有第 1 个引用。
    pub fn put(&mut self, payload: &[u8], type_tag: u32) -> u64 {
        let offset = self.bytes.len();
        self.bytes.extend_from_slice(payload);
        self.entries.push(BlobEntry {
            offset,
            len: payload.len(),
            type_tag,
            refs: 1,
            alive: true,
        });
        self.entries.len() as u64
    }

    /// 按句柄读 payload 与 type_tag。句柄无效或已释放返回 None（无悬垂读）。
    pub fn get(&self, handle: u64) -> Option<(&[u8], u32)> {
        let e = self.entries.get(Self::slot(handle)?)?;
        if !e.alive || e.refs == 0 {
            return None;
        }
        Some((&self.bytes[e.offset..e.offset + e.len], e.type_tag))
    }

    /// 增加一个外部引用。句柄无效/已释放返回 false。
    pub fn retain(&mut self, handle: u64) -> bool {
        let Some(i) = Self::slot(handle) else { return false; };
        match self.entries.get_mut(i) {
            Some(e) if e.alive && e.refs > 0 => {
                e.refs = e.refs.saturating_add(1);
                true
            }
            _ => false,
        }
    }

    /// 释放一个引用，归零则标记失效。释放不属于自己的引用返回 false。
    pub fn release(&mut self, handle: u64) -> bool {
        let Some(i) = Self::slot(handle) else { return false; };
        match self.entries.get_mut(i) {
            Some(e) if e.refs > 0 => {
                e.refs -= 1;
                if e.refs == 0 {
                    e.alive = false;
                }
                true
            }
            _ => false,
        }
    }

    /// 池内 blob 数量（含已释放的槽位；slot 不复用）。
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }
}

/// 列压缩编解码器。调用方按列或按表显式选用；绘图主通道 F64 默认不压。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Codec {
    /// 全列同值，只存一个值。
    Constant,
    /// 帧内引用：基值 + 每行定长偏移，适合小范围整数状态码。
    For,
    /// 字典：去重值表 + 每行索引，适合低基数枚举与文本。
    Dictionary,
    /// 布尔按位打包，8 行 1 字节。
    BitPack,
}

/// 编码单元。数值保留位模式，保证解压逐位一致。
#[derive(Debug, Clone, PartialEq)]
pub enum Atom {
    Bits(u64),
    I64(i64),
    U64(u64),
    Text(String),
}

impl Atom {
    fn as_f64(&self) -> Option<f64> {
        match self {
            Atom::Bits(b) => Some(f64::from_bits(*b)),
            Atom::I64(v) => Some(*v as f64),
            Atom::U64(v) => Some(*v as f64),
            Atom::Text(_) => None,
        }
    }

    fn as_text(&self) -> String {
        match self {
            Atom::Bits(b) => f64::from_bits(*b).to_string(),
            Atom::I64(v) => v.to_string(),
            Atom::U64(v) => v.to_string(),
            Atom::Text(s) => s.clone(),
        }
    }

    fn as_i64(&self) -> Option<i64> {
        match self {
            Atom::I64(v) => Some(*v),
            Atom::U64(v) => i64::try_from(*v).ok(),
            Atom::Bits(b) => Some(f64::from_bits(*b) as i64),
            Atom::Text(_) => None,
        }
    }

    fn as_u64(&self) -> Option<u64> {
        match self {
            Atom::U64(v) => Some(*v),
            Atom::I64(v) => u64::try_from(*v).ok(),
            Atom::Bits(b) => Some(f64::from_bits(*b) as u64),
            Atom::Text(_) => None,
        }
    }

    fn size_bytes(&self) -> usize {
        match self {
            Atom::Bits(_) | Atom::I64(_) | Atom::U64(_) => 8,
            Atom::Text(s) => s.len(),
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum EncodedPayload {
    Constant(Atom),
    For { base: i64, deltas: Vec<u32> },
    Dictionary { atoms: Vec<Atom>, indices: Vec<u32> },
    BitPack(Vec<u8>),
}

impl EncodedPayload {
    fn atom_at(&self, row: usize) -> Option<Atom> {
        match self {
            EncodedPayload::Constant(a) => Some(a.clone()),
            EncodedPayload::For { base, deltas } => {
                deltas.get(row).map(|d| Atom::I64(base.saturating_add(*d as i64)))
            }
            EncodedPayload::Dictionary { atoms, indices } => {
                indices.get(row).and_then(|i| atoms.get(*i as usize)).cloned()
            }
            EncodedPayload::BitPack(bits) => bits
                .get(row / 8)
                .map(|b| Atom::I64(((b >> (row % 8)) & 1) as i64)),
        }
    }

    fn atoms(&self, len: usize) -> Vec<Atom> {
        match self {
            EncodedPayload::Constant(a) => vec![a.clone(); len],
            EncodedPayload::For { base, deltas } => deltas
                .iter()
                .map(|d| Atom::I64(base.saturating_add(*d as i64)))
                .collect(),
            EncodedPayload::Dictionary { atoms, indices } => indices
                .iter()
                .map(|i| atoms[*i as usize].clone())
                .collect(),
            EncodedPayload::BitPack(bits) => (0..len)
                .map(|r| Atom::I64(((bits[r / 8] >> (r % 8)) & 1) as i64))
                .collect(),
        }
    }

    fn size_bytes(&self) -> usize {
        match self {
            EncodedPayload::Constant(a) => a.size_bytes(),
            EncodedPayload::For { deltas, .. } => 8 + deltas.len() * 4,
            EncodedPayload::Dictionary { atoms, indices } => {
                8 + atoms.iter().map(|a| 4 + a.size_bytes()).sum::<usize>() + indices.len() * 4
            }
            EncodedPayload::BitPack(bits) => bits.len(),
        }
    }
}

fn encode_payload(codec: Codec, logical: ValueType, atoms: &[Atom]) -> Result<EncodedPayload, String> {
    match codec {
        Codec::Constant => {
            if atoms.is_empty() {
                return Ok(EncodedPayload::Constant(Atom::I64(0)));
            }
            if atoms.iter().all(|a| a == &atoms[0]) {
                Ok(EncodedPayload::Constant(atoms[0].clone()))
            } else {
                Err("常数列要求所有行同值".into())
            }
        }
        Codec::For => {
            if !matches!(logical, ValueType::I8 | ValueType::I16 | ValueType::I32 | ValueType::I64
                | ValueType::U8 | ValueType::U16 | ValueType::U32 | ValueType::U64 | ValueType::Bool)
            {
                return Err("FOR 编码仅适用整数与布尔列".into());
            }
            let mut base = i64::MAX;
            for a in atoms {
                base = base.min(a.as_i64().ok_or("FOR 编码遇非整数值")?);
            }
            if atoms.is_empty() {
                return Ok(EncodedPayload::For { base: 0, deltas: Vec::new() });
            }
            let mut deltas = Vec::with_capacity(atoms.len());
            for a in atoms {
                let d = a.as_i64().ok_or("FOR 编码遇非整数值")? - base;
                deltas.push(u32::try_from(d).map_err(|_| "FOR 编码跨度超过 u32 范围")?);
            }
            Ok(EncodedPayload::For { base, deltas })
        }
        Codec::Dictionary => {
            let mut dict: Vec<Atom> = Vec::new();
            let mut indices = Vec::with_capacity(atoms.len());
            for a in atoms {
                let idx = match dict.iter().position(|x| x == a) {
                    Some(i) => i,
                    None => { dict.push(a.clone()); dict.len() - 1 }
                };
                indices.push(idx as u32);
            }
            Ok(EncodedPayload::Dictionary { atoms: dict, indices })
        }
        Codec::BitPack => {
            if logical != ValueType::Bool {
                return Err("位打包仅适用布尔列".into());
            }
            let mut bits = vec![0u8; atoms.len().div_ceil(8)];
            for (r, a) in atoms.iter().enumerate() {
                if a.as_i64().unwrap_or(0) != 0 {
                    bits[r / 8] |= 1 << (r % 8);
                }
            }
            Ok(EncodedPayload::BitPack(bits))
        }
    }
}

fn atoms_to_column(atoms: &[Atom], vt: ValueType) -> Result<ColumnData, String> {
    use ValueType as V;
    let col = match vt {
        V::F64 => ColumnData::F64(atoms.iter().map(|a| match a {
            Atom::Bits(b) => f64::from_bits(*b),
            Atom::I64(x) => *x as f64,
            Atom::U64(x) => *x as f64,
            Atom::Text(_) => f64::NAN,
        }).collect()),
        V::F32 => ColumnData::F32(atoms.iter().map(|a| match a {
            Atom::Bits(b) => f64::from_bits(*b) as f32,
            Atom::I64(x) => *x as f32,
            Atom::U64(x) => *x as f32,
            Atom::Text(_) => f32::NAN,
        }).collect()),
        V::I64 => ColumnData::I64(atoms.iter().map(|a| a.as_i64().unwrap_or(0)).collect()),
        V::U64 => ColumnData::U64(atoms.iter().map(|a| a.as_u64().unwrap_or(0)).collect()),
        V::I32 => ColumnData::I32(atoms.iter().map(|a| a.as_i64().unwrap_or(0) as i32).collect()),
        V::U32 => ColumnData::U32(atoms.iter().map(|a| a.as_u64().unwrap_or(0) as u32).collect()),
        V::I16 => ColumnData::I16(atoms.iter().map(|a| a.as_i64().unwrap_or(0) as i16).collect()),
        V::U16 => ColumnData::U16(atoms.iter().map(|a| a.as_u64().unwrap_or(0) as u16).collect()),
        V::I8 => ColumnData::I8(atoms.iter().map(|a| a.as_i64().unwrap_or(0) as i8).collect()),
        V::U8 => ColumnData::U8(atoms.iter().map(|a| a.as_u64().unwrap_or(0) as u8).collect()),
        V::Bool => ColumnData::Bool(atoms.iter().map(|a| a.as_i64().unwrap_or(0) != 0).collect()),
        V::Text => ColumnData::Text(atoms.iter().map(|a| a.as_text()).collect()),
        V::Object => return Err("对象列不参与压缩编码".into()),
    };
    Ok(col)
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
    /// 每行一个 ObjectStore 句柄；payload 在数据集的 ObjectStore 里（S4）。
    Object(Vec<u64>),
    /// 压缩列：逻辑类型 + codec + 载荷 + 行数。列内无连续原生数组，
    /// columnRaw 不支持整列裸借出；LOD 经单行解码聚合。
    Encoded { logical: ValueType, codec: Codec, payload: EncodedPayload, len: usize },
}

/// 把归并顺序压缩成"同源连续递增"的 run：`(source, start, len)`。
/// `order` 来自 k 路归并，本身已是"每次从某个源取下一个元素"的形态，
/// 因此同源且下标连续的一段可以直接整块拷贝，省掉逐元素的双层索引。
fn compress_runs(order: &[(usize, usize)]) -> Vec<(usize, usize, usize)> {
    let mut runs: Vec<(usize, usize, usize)> = Vec::new();
    let mut i = 0;
    while i < order.len() {
        let (src, start) = order[i];
        let mut j = i + 1;
        while j < order.len() && order[j].0 == src && order[j].1 == order[j - 1].1 + 1 {
            j += 1;
        }
        runs.push((src, start, j - i));
        i = j;
    }
    runs
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
            ColumnData::Object(_) => ValueType::Object,
            ColumnData::Encoded { logical, .. } => *logical,
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
            ColumnData::Object(v) => v.len(),
            ColumnData::Encoded { len, .. } => *len,
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
            ColumnData::Object(v) => v.clear(),
            ColumnData::Encoded { payload, len, .. } => {
                *payload = EncodedPayload::Constant(Atom::I64(0));
                *len = 0;
            }
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
            ColumnData::Object(v) => { v.drain(0..rows); }
            ColumnData::Encoded { logical, codec, payload, len } => {
                let atoms = payload.atoms(*len);
                let kept = if rows >= atoms.len() { Vec::new() } else { atoms[rows..].to_vec() };
                if let Ok(p) = encode_payload(*codec, *logical, &kept) {
                    *payload = p;
                    *len = kept.len();
                } else {
                    // 降级为字典编码（Constant/For 可能因裁剪后数据失配）。
                    if let Ok(p) = encode_payload(Codec::Dictionary, *logical, &kept) {
                        *payload = p;
                        *codec = Codec::Dictionary;
                        *len = kept.len();
                    } else {
                        *payload = EncodedPayload::Constant(Atom::I64(0));
                        *len = 0;
                    }
                }
            }
        }
    }

    /// 追加同类型数据；类型不一致返回错误，调用方负责回滚。
    /// 按行置换重排列值：`out[i] = self[order[i]]`（S2 乱序入口 reorder 用）。
    pub fn reorder_by(&self, order: &[usize]) -> ColumnData {
        fn gather<T: Clone>(v: &[T], order: &[usize]) -> Vec<T> {
            order.iter().map(|&i| v[i].clone()).collect()
        }
        match self {
            ColumnData::F64(v) => ColumnData::F64(gather(v, order)),
            ColumnData::F32(v) => ColumnData::F32(gather(v, order)),
            ColumnData::I64(v) => ColumnData::I64(gather(v, order)),
            ColumnData::U64(v) => ColumnData::U64(gather(v, order)),
            ColumnData::I32(v) => ColumnData::I32(gather(v, order)),
            ColumnData::U32(v) => ColumnData::U32(gather(v, order)),
            ColumnData::I16(v) => ColumnData::I16(gather(v, order)),
            ColumnData::U16(v) => ColumnData::U16(gather(v, order)),
            ColumnData::I8(v) => ColumnData::I8(gather(v, order)),
            ColumnData::U8(v) => ColumnData::U8(gather(v, order)),
            ColumnData::Bool(v) => ColumnData::Bool(gather(v, order)),
            ColumnData::Text(v) => ColumnData::Text(gather(v, order)),
            ColumnData::Object(v) => ColumnData::Object(gather(v, order)),
            ColumnData::Encoded { logical, codec, payload, len } => {
                let atoms = payload.atoms(*len);
                let picked: Vec<Atom> = order.iter().map(|&i| atoms[i].clone()).collect();
                Self::encode_from_atoms(*logical, *codec, &picked)
                    .or_else(|_| Self::encode_from_atoms(*logical, Codec::Dictionary, &picked))
                    .unwrap_or_else(|_| {
                        Self::Encoded { logical: *logical, codec: Codec::Dictionary,
                            payload: EncodedPayload::Dictionary { atoms: Vec::new(), indices: picked.iter().map(|_| 0).collect() }, len: picked.len() }
                    })
            }
        }
    }

    /// 按归并序列合并两列：`out[t] = is_new[t] ? new[order[t].1] : old[order[t].1]`。
    pub fn merge_gather(&self, new: &ColumnData, order: &[(bool, usize)]) -> ColumnData {
        fn g<T: Clone>(a: &[T], b: &[T], order: &[(bool, usize)]) -> Vec<T> {
            order.iter().map(|&(is_new, k)| if is_new { b[k].clone() } else { a[k].clone() }).collect()
        }
        if let ColumnData::Encoded { logical, codec, payload, len } = self {
            let a = payload.atoms(*len);
            let b = new.to_atoms();
            let picked: Vec<Atom> = order.iter()
                .map(|&(is_new, k)| if is_new { b[k].clone() } else { a[k].clone() })
                .collect();
            return Self::encode_from_atoms(*logical, *codec, &picked)
                .or_else(|_| Self::encode_from_atoms(*logical, Codec::Dictionary, &picked))
                .unwrap_or_else(|_| Self::Encoded {
                    logical: *logical, codec: Codec::Dictionary,
                    payload: EncodedPayload::Dictionary { atoms: Vec::new(), indices: picked.iter().map(|_| 0).collect() },
                    len: picked.len(),
                });
        }
        match (self, new) {
            (ColumnData::F64(a), ColumnData::F64(b)) => ColumnData::F64(g(a, b, order)),
            (ColumnData::F32(a), ColumnData::F32(b)) => ColumnData::F32(g(a, b, order)),
            (ColumnData::I64(a), ColumnData::I64(b)) => ColumnData::I64(g(a, b, order)),
            (ColumnData::U64(a), ColumnData::U64(b)) => ColumnData::U64(g(a, b, order)),
            (ColumnData::I32(a), ColumnData::I32(b)) => ColumnData::I32(g(a, b, order)),
            (ColumnData::U32(a), ColumnData::U32(b)) => ColumnData::U32(g(a, b, order)),
            (ColumnData::I16(a), ColumnData::I16(b)) => ColumnData::I16(g(a, b, order)),
            (ColumnData::U16(a), ColumnData::U16(b)) => ColumnData::U16(g(a, b, order)),
            (ColumnData::I8(a), ColumnData::I8(b)) => ColumnData::I8(g(a, b, order)),
            (ColumnData::U8(a), ColumnData::U8(b)) => ColumnData::U8(g(a, b, order)),
            (ColumnData::Bool(a), ColumnData::Bool(b)) => ColumnData::Bool(g(a, b, order)),
            (ColumnData::Text(a), ColumnData::Text(b)) => ColumnData::Text(g(a, b, order)),
            (ColumnData::Object(a), ColumnData::Object(b)) => ColumnData::Object(g(a, b, order)),
            _ => unreachable!("列类型已校验一致"),
        }
    }

    /// 多源归并收集：`out[t] = sources[order[t].0][order[t].1]`。
    /// 所有源类型一致；任一源为 Encoded 时统一走 atoms 再编码。
    pub fn gather_from_sources(sources: &[&ColumnData], order: &[(usize, usize)]) -> ColumnData {
        if sources.is_empty() {
            return ColumnData::F64(Vec::new());
        }
        if sources.iter().any(|s| matches!(s, ColumnData::Encoded { .. })) {
            let atoms: Vec<Vec<Atom>> = sources.iter().map(|s| s.to_atoms()).collect();
            let picked: Vec<Atom> = order.iter()
                .map(|&(src, i)| atoms[src][i].clone())
                .collect();
            let logical = sources[0].value_type();
            return Self::encode_from_atoms(logical, Codec::Dictionary, &picked)
                .unwrap_or_else(|_| Self::Encoded {
                    logical,
                    codec: Codec::Dictionary,
                    payload: EncodedPayload::Dictionary {
                        atoms: Vec::new(),
                        indices: picked.iter().map(|_| 0).collect(),
                    },
                    len: picked.len(),
                });
        }
        // 归并输出天然是"同源的连续 run"：补传 1 万行插进 100 万行时，run 长度约 100。
        // 逐元素 `slices[src][i]` 每行要做一次双层间接索引，编译器无法向量化，
        // 实测 109 万行 / 单列要 11ms。按 run 批量 extend_from_slice 可走 memcpy，
        // 代价是多一次 order 扫描（顺序访问，很轻）。
        let runs = compress_runs(order);
        macro_rules! g {
            ($var:ident, $ty:ty) => {{
                let slices: Vec<&[$ty]> = sources.iter().map(|s| match s {
                    ColumnData::$var(v) => v.as_slice(),
                    _ => unreachable!("列类型已校验一致"),
                }).collect();
                let mut out: Vec<$ty> = Vec::with_capacity(order.len());
                for &(src, start, len) in &runs {
                    out.extend_from_slice(&slices[src][start..start + len]);
                }
                ColumnData::$var(out)
            }};
        }
        match sources[0] {
            ColumnData::F64(_) => g!(F64, f64),
            ColumnData::F32(_) => g!(F32, f32),
            ColumnData::I64(_) => g!(I64, i64),
            ColumnData::U64(_) => g!(U64, u64),
            ColumnData::I32(_) => g!(I32, i32),
            ColumnData::U32(_) => g!(U32, u32),
            ColumnData::I16(_) => g!(I16, i16),
            ColumnData::U16(_) => g!(U16, u16),
            ColumnData::I8(_) => g!(I8, i8),
            ColumnData::U8(_) => g!(U8, u8),
            ColumnData::Bool(_) => g!(Bool, bool),
            ColumnData::Text(_) => g!(Text, String),
            ColumnData::Object(_) => g!(Object, u64),
            ColumnData::Encoded { .. } => unreachable!("Encoded 已在上方处理"),
        }
    }

    pub fn append(&mut self, other: ColumnData) -> Result<(), String> {
        if self.value_type() != other.value_type() {
            return Err("列类型不一致".into());
        }
        let self_codec = match self {
            ColumnData::Encoded { codec, .. } => Some(*codec),
            _ => None,
        };
        let logical = self.value_type();
        let other_encoded = matches!(other, ColumnData::Encoded { .. });
        if self_codec.is_some() || other_encoded {
            let mut a = self.to_atoms();
            let b = other.to_atoms();
            a.extend(b);
            let enc = if let Some(codec) = self_codec {
                Self::encode_from_atoms(logical, codec, &a)
                    .or_else(|_| Self::encode_from_atoms(logical, Codec::Dictionary, &a))?
            } else {
                // self 是原生、other 是编码：追加后仍以原生列落库，避免无谓改变主通道布局。
                return atoms_to_column(&a, logical).map(|c| *self = c);
            };
            *self = enc;
            return Ok(());
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
            (ColumnData::Object(a), ColumnData::Object(b)) => a.extend(b),
            _ => return Err("列类型不一致".into()),
        }
        Ok(())
    }

    /// 行区间切片 `[start, end)`：同类型复制出一段（2.7.5 数据集切片用）。
    pub fn slice_rows(&self, start: usize, end: usize) -> ColumnData {
        let end = end.min(self.len());
        let start = start.min(end);
        match self {
            ColumnData::F64(v) => ColumnData::F64(v[start..end].to_vec()),
            ColumnData::F32(v) => ColumnData::F32(v[start..end].to_vec()),
            ColumnData::I64(v) => ColumnData::I64(v[start..end].to_vec()),
            ColumnData::U64(v) => ColumnData::U64(v[start..end].to_vec()),
            ColumnData::I32(v) => ColumnData::I32(v[start..end].to_vec()),
            ColumnData::U32(v) => ColumnData::U32(v[start..end].to_vec()),
            ColumnData::I16(v) => ColumnData::I16(v[start..end].to_vec()),
            ColumnData::U16(v) => ColumnData::U16(v[start..end].to_vec()),
            ColumnData::I8(v) => ColumnData::I8(v[start..end].to_vec()),
            ColumnData::U8(v) => ColumnData::U8(v[start..end].to_vec()),
            ColumnData::Bool(v) => ColumnData::Bool(v[start..end].to_vec()),
            ColumnData::Text(v) => ColumnData::Text(v[start..end].to_vec()),
            ColumnData::Object(v) => ColumnData::Object(v[start..end].to_vec()),
            ColumnData::Encoded { logical, codec, payload, len } => {
                let atoms = payload.atoms(*len);
                let picked = atoms[start..end].to_vec();
                Self::encode_from_atoms(*logical, *codec, &picked)
                    .or_else(|_| Self::encode_from_atoms(*logical, Codec::Dictionary, &picked))
                    .unwrap_or_else(|_| Self::Encoded {
                        logical: *logical, codec: Codec::Dictionary,
                        payload: EncodedPayload::Dictionary { atoms: Vec::new(), indices: picked.iter().map(|_| 0).collect() },
                        len: picked.len(),
                    })
            }
        }
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
            // 对象列无标量数值语义；曲线/光标不应把它当数值画。
            ColumnData::Object(_) => None,
            // 单行 O(1) 解码；文本编码列无数值语义。
            ColumnData::Encoded { payload, len, .. } => {
                if row >= *len { None } else { payload.atom_at(row).and_then(|a| a.as_f64()) }
            }
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
            // 对象列显示成 `obj#<handle>`，仅用于占位提示；真实 payload 走 blob_get。
            ColumnData::Object(v) => v.get(row).map(|h| format!("obj#{h}")),
            ColumnData::Encoded { payload, len, .. } => {
                if row >= *len { None } else { payload.atom_at(row).map(|a| a.as_text()) }
            }
        }
    }

    /// 显式编码当前列。调用方按需对低基数/布尔/文本列调用；F64 主通道不调用即保零拷贝。
    pub fn encode(&self, codec: Codec) -> Result<ColumnData, String> {
        if matches!(self, ColumnData::Encoded { .. }) {
            return Err("列已编码".into());
        }
        let logical = self.value_type();
        let atoms = self.to_atoms();
        Self::encode_from_atoms(logical, codec, &atoms)
    }

    /// 解码编码列为原生列；原生列原样返回。
    pub fn decode(&self) -> Result<ColumnData, String> {
        match self {
            ColumnData::Encoded { logical, payload, len, .. } => atoms_to_column(&payload.atoms(*len), *logical),
            _ => Ok(self.clone()),
        }
    }

    /// 编码列返回 codec，原生列返回 None。
    pub fn codec(&self) -> Option<Codec> {
        match self { ColumnData::Encoded { codec, .. } => Some(*codec), _ => None }
    }

    /// 估算列的内存占用字节数（编码列取载荷实际占用），用于压缩前后对比。
    pub fn memory_bytes(&self) -> usize {
        match self {
            ColumnData::Encoded { payload, .. } => payload.size_bytes(),
            _ => self.len() * self.value_type().value_bytes()
                + if matches!(self, ColumnData::Text(_)) {
                    match self { ColumnData::Text(v) => v.iter().map(|s| s.len()).sum(), _ => 0 }
                } else { 0 },
        }
    }

    fn to_atoms(&self) -> Vec<Atom> {
        match self {
            ColumnData::F64(v) => v.iter().map(|x| Atom::Bits(x.to_bits())).collect(),
            ColumnData::F32(v) => v.iter().map(|x| Atom::Bits((*x as f64).to_bits())).collect(),
            ColumnData::I64(v) => v.iter().map(|x| Atom::I64(*x)).collect(),
            ColumnData::U64(v) => v.iter().map(|x| Atom::U64(*x)).collect(),
            ColumnData::I32(v) => v.iter().map(|x| Atom::I64(*x as i64)).collect(),
            ColumnData::U32(v) => v.iter().map(|x| Atom::U64(*x as u64)).collect(),
            ColumnData::I16(v) => v.iter().map(|x| Atom::I64(*x as i64)).collect(),
            ColumnData::U16(v) => v.iter().map(|x| Atom::U64(*x as u64)).collect(),
            ColumnData::I8(v) => v.iter().map(|x| Atom::I64(*x as i64)).collect(),
            ColumnData::U8(v) => v.iter().map(|x| Atom::U64(*x as u64)).collect(),
            ColumnData::Bool(v) => v.iter().map(|x| Atom::I64(if *x { 1 } else { 0 })).collect(),
            ColumnData::Text(v) => v.iter().map(|x| Atom::Text(x.clone())).collect(),
            ColumnData::Object(v) => v.iter().map(|x| Atom::U64(*x)).collect(),
            ColumnData::Encoded { payload, len, .. } => payload.atoms(*len),
        }
    }

    fn encode_from_atoms(logical: ValueType, codec: Codec, atoms: &[Atom]) -> Result<ColumnData, String> {
        let payload = encode_payload(codec, logical, atoms)?;
        Ok(ColumnData::Encoded { logical, codec, payload, len: atoms.len() })
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

    /// 行区间切片 `[start, end)`：null 标记逐位复制（2.7.5 数据集切片用）。
    pub fn slice_rows(&self, start: usize, end: usize) -> Self {
        let end = end.max(start);
        let mut out = NullBits::new(end - start);
        for i in start..end {
            if self.is_null(i) {
                out.set_null(i - start, true);
            }
        }
        out
    }

    /// 位图里是否存在任何 null。全 0（全有效）时 C ABI 可以省掉位图借用。
    pub fn has_any_null(&self) -> bool {
        self.bits.iter().any(|b| *b != 0)
    }

    /// 按行置换重排 null 位图：`out[i] = self[order[i]]`（S2 乱序入口 reorder 用）。
    pub fn reorder_by(&self, order: &[usize]) -> Self {
        let mut out = Self::new(order.len());
        for (i, &src) in order.iter().enumerate() {
            if self.is_null(src) {
                out.set_null(i, true);
            }
        }
        out
    }
}

/// 一列的 LOD 金字塔：`levels[l]` 每桶聚合 `4^(l+1)` **全局行**
/// （levels[0] = 每 4 行原始聚合，levels[1] = 每 16 行……）。
///
/// 行号锚定**全局行空间**（ring 裁剪不重排历史行号）：ring 裁剪
/// （`Column::drop_prefix`）不再使金字塔失效，只递增列偏移并释放
/// 完全落在裁剪线以下的前缀桶——裁剪成本 O(释放的桶数)，渲染查询
/// 零重算。
///
/// - 构建惰性：第一次视口渲染时 O(n) 建塔，之后每帧视口读 O(px·log₄n)。
/// - append 纯前缀增量续算：只重算被新行覆盖的桶，成本 O(新增行数)。
/// - set_null 仍使金字塔失效（桶内聚合值随位图变化，重建比增量便宜）。
#[derive(Debug, Clone, Default)]
pub struct LodPyramid {
    /// 全局行锚点：level l 桶 b 覆盖全局行 `[anchor + b·4^(l+1), anchor + (b+1)·4^(l+1))`。
    anchor: usize,
    /// 自 anchor 起已覆盖的全局行数（append 只扩展尾部）。
    built_rows: usize,
    /// 分层 min/max；桶内全 null/非有限时 min=max=NaN。
    levels: Vec<LodLevel>,
}

#[derive(Debug, Clone, Default)]
pub struct LodLevel {
    /// 已释放（完全落在裁剪线以下）的前缀桶数：桶 b 存于 `min[b - start]`。
    start: usize,
    pub min: Vec<f64>,
    pub max: Vec<f64>,
}

impl LodLevel {
    /// 桶 b 的 (min, max)；已释放或尚未写入返回 None。
    fn bucket(&self, b: usize) -> Option<(f64, f64)> {
        let i = b.checked_sub(self.start)?;
        self.min.get(i).map(|&m| (m, self.max[i]))
    }

    /// 写入/覆盖桶 b。`b < start`（已释放区）静默忽略——查询永不触达该区。
    fn set_bucket(&mut self, b: usize, mn: f64, mx: f64) {
        if b < self.start {
            return;
        }
        let i = b - self.start;
        while self.min.len() <= i {
            self.min.push(f64::NAN);
            self.max.push(f64::NAN);
        }
        self.min[i] = mn;
        self.max[i] = mx;
    }
}

impl LodPyramid {
    /// 桶行数：level l 对应 4^(l+1)。
    fn bucket_rows(level: usize) -> usize {
        4usize.pow(level as u32 + 1)
    }

    /// 覆盖是否到达全局行 `end`（= 列裁剪偏移 + 本地行数）。
    fn is_built_for(&self, end: usize) -> bool {
        self.anchor + self.built_rows >= end
    }

    /// 建塔：覆盖全局行 `[anchor, anchor + data.len())`。
    /// `anchor` 取建塔时刻的列裁剪偏移（此后恒定，裁剪只释放桶不动锚点）。
    fn build(data: &ColumnData, nulls: &NullBits, anchor: usize) -> Self {
        let n = data.len();
        let mut levels = Vec::new();
        if n == 0 {
            return Self { anchor, built_rows: 0, levels };
        }
        // level0：每 4 行一个桶，直接扫原始值。
        let l0_buckets = n.div_ceil(4);
        let mut l0 = LodLevel { start: 0, min: Vec::with_capacity(l0_buckets), max: Vec::with_capacity(l0_buckets) };
        for b in 0..l0_buckets {
            let lo = b * 4;
            let hi = (lo + 4).min(n);
            let (mn, mx) = aggregate_rows(data, nulls, lo, hi);
            l0.min.push(mn);
            l0.max.push(mx);
        }
        levels.push(l0);
        // 上层：由下一层桶聚合（min 的 min / max 的 max）。
        loop {
            let prev = levels.last().expect("level0 已就位");
            if prev.min.len() <= 1 {
                break;
            }
            let mut next = LodLevel::default();
            let buckets = prev.min.len().div_ceil(4);
            next.min.reserve(buckets);
            next.max.reserve(buckets);
            for b in 0..buckets {
                let lo = b * 4;
                let hi = (lo + 4).min(prev.min.len());
                let mut mn = f64::INFINITY;
                let mut mx = f64::NEG_INFINITY;
                for i in lo..hi {
                    if prev.min[i].is_finite() {
                        mn = mn.min(prev.min[i]);
                        mx = mx.max(prev.max[i]);
                    }
                }
                if mn.is_finite() {
                    next.min.push(mn);
                    next.max.push(mx);
                } else {
                    next.min.push(f64::NAN);
                    next.max.push(f64::NAN);
                }
            }
            levels.push(next);
        }
        Self { anchor, built_rows: n, levels }
    }

    /// append 后增量续算：重算与全局区间 `[anchor + built_rows, end)` 相交的桶
    /// （成本 O(新增行数)）。`dropped` 为列的当前全局偏移（本地行 r = 全局行 dropped+r）。
    fn extend_into(
        &mut self,
        data: &ColumnData,
        nulls: &NullBits,
        dropped: usize,
        end: usize,
    ) {
        let old_end = self.anchor + self.built_rows;
        if self.built_rows == 0 || end <= old_end {
            return;
        }
        for l in 0..self.levels.len() {
            let size = Self::bucket_rows(l);
            // 受影响的桶：与 [old_end, end) 相交的所有桶（含部分相交的首桶）。
            let first = (old_end - self.anchor) / size;
            let last = (end - self.anchor).div_ceil(size);
            for b in first..last {
                let (mn, mx) = if l == 0 {
                    let glo = self.anchor + b * size;
                    let ghi = (glo + size).min(end);
                    if ghi <= dropped {
                        // 桶已完全被 ring 裁剪消费：查询永不触达，占位 NaN。
                        (f64::NAN, f64::NAN)
                    } else {
                        let lo = glo.saturating_sub(dropped);
                        let hi = ghi - dropped;
                        aggregate_rows(data, nulls, lo, hi)
                    }
                } else {
                    aggregate_children(&self.levels[l - 1], b)
                };
                self.levels[l].set_bucket(b, mn, mx);
            }
        }
        self.built_rows = end - self.anchor;
    }

    /// ring 裁剪：全局裁剪线推进到 `new_dropped`。释放完全落在裁剪线以下的
    /// 前缀桶（每层独立判断），锚点与既有桶索引不漂移，查询零重算。
    fn drop_prefix(&mut self, new_dropped: usize) {
        for (l, lv) in self.levels.iter_mut().enumerate() {
            let size = Self::bucket_rows(l);
            // 完全落在裁剪线以下的桶总数（含历史已释放）：
            // 桶 b 完全在裁剪线下 ⟺ anchor + (b+1)·size ≤ new_dropped。
            let target = (new_dropped.saturating_sub(self.anchor) / size)
                .min(lv.start + lv.min.len());
            let delta = target - lv.start;
            lv.start += delta;
            lv.min.drain(0..delta);
            lv.max.drain(0..delta);
        }
    }

    /// 行区间 `[lo, hi)`（**本地行**）的 min/max（跳过 null/非有限）。全部无效返回 None。
    ///
    /// 自顶向下取整桶、边缘递归细层，最后 <4 行残余扫原始值——
    /// 成本 O(log₄n)，与行数无关。要求 `is_built_for` 已满足。
    fn range_minmax(
        &self,
        data: &ColumnData,
        nulls: &NullBits,
        dropped: usize,
        lo: usize,
        hi: usize,
    ) -> Option<(f64, f64)> {
        // 本地行 → 金字塔行（全局 - anchor）。
        let off = dropped - self.anchor;
        query_level(&self.levels, self.levels.len(), data, nulls, off, lo + off, hi + off)
    }
}

/// 单行读值（列数据 + null 位图）；null/越界返回 None。
fn raw_value(data: &ColumnData, nulls: &NullBits, row: usize) -> Option<f64> {
    if row >= data.len() || nulls.is_null(row) {
        return None;
    }
    data.value_as_f64(row)
}

/// 扫原始行 `[lo, hi)` 聚合出 (min, max)；全无效/空区间返回 (NaN, NaN)。
fn aggregate_rows(data: &ColumnData, nulls: &NullBits, lo: usize, hi: usize) -> (f64, f64) {
    let mut mn = f64::INFINITY;
    let mut mx = f64::NEG_INFINITY;
    for row in lo..hi {
        if let Some(v) = raw_value(data, nulls, row) {
            if v.is_finite() {
                mn = mn.min(v);
                mx = mx.max(v);
            }
        }
    }
    if mn.is_finite() { (mn, mx) } else { (f64::NAN, f64::NAN) }
}

/// level l 桶 b 由 level l-1 的 4 个子桶聚合（子桶全无效/已释放 → NaN）。
fn aggregate_children(child: &LodLevel, b: usize) -> (f64, f64) {
    let mut mn = f64::INFINITY;
    let mut mx = f64::NEG_INFINITY;
    for i in 0..4 {
        if let Some((cmin, cmax)) = child.bucket(b * 4 + i) {
            if cmin.is_finite() {
                mn = mn.min(cmin);
                mx = mx.max(cmax);
            }
        }
    }
    if mn.is_finite() { (mn, mx) } else { (f64::NAN, f64::NAN) }
}

fn combine(acc: Option<(f64, f64)>, mn: f64, mx: f64) -> Option<(f64, f64)> {
    if !mn.is_finite() {
        return acc; // 桶全无效
    }
    Some(match acc {
        Some((a, b)) => (a.min(mn), b.max(mx)),
        None => (mn, mx),
    })
}

/// 金字塔区间查询：自顶向下，整桶直接取，边缘递归下一层，最后 <4 行扫原始。
fn query_level(
    levels: &[LodLevel],
    level: usize,
    data: &ColumnData,
    nulls: &NullBits,
    off: usize,
    lo: usize,
    hi: usize,
) -> Option<(f64, f64)> {
    if lo >= hi {
        return None;
    }
    if level == 0 {
        // levels[0] 桶 = 4 行；残余边缘扫原始值。
        let mut acc: Option<(f64, f64)> = None;
        if let Some(l0) = levels.first() {
            let first = lo.div_ceil(4);
            let last = hi / 4;
            for b in first..last {
                if let Some((mn, mx)) = l0.bucket(b) {
                    acc = combine(acc, mn, mx);
                }
            }
            let mid_lo = (first * 4).min(hi);
            let mid_hi = (last * 4).max(lo);
            for row in lo..mid_lo {
                if let Some(v) = raw_value(data, nulls, row - off) {
                    if v.is_finite() {
                        acc = combine(acc, v, v);
                    }
                }
            }
            for row in mid_hi..hi {
                if let Some(v) = raw_value(data, nulls, row - off) {
                    if v.is_finite() {
                        acc = combine(acc, v, v);
                    }
                }
            }
            return acc;
        }
        // 没有任何层（理论不可达，built_rows>0 保证 level0 存在）。
        return aggregate_rows_checked(data, nulls, lo - off, hi - off);
    }
    let size = LodPyramid::bucket_rows(level - 1);
    let Some(lv) = levels.get(level - 1) else {
        return aggregate_rows_checked(data, nulls, lo - off, hi - off);
    };
    if hi - lo < size {
        return query_level(levels, level - 1, data, nulls, off, lo, hi);
    }
    let first = lo.div_ceil(size);
    let last = hi / size;
    if first >= last {
        return query_level(levels, level - 1, data, nulls, off, lo, hi);
    }
    let mut acc: Option<(f64, f64)> = None;
    for b in first..last {
        if let Some((mn, mx)) = lv.bucket(b) {
            acc = combine(acc, mn, mx);
        }
    }
    let mid_lo = (first * size).min(hi);
    let mid_hi = (last * size).max(lo);
    if mid_lo > lo {
        if let Some(x) = query_level(levels, level - 1, data, nulls, off, lo, mid_lo) {
            acc = combine(acc, x.0, x.1);
        }
    }
    if mid_hi < hi {
        if let Some(x) = query_level(levels, level - 1, data, nulls, off, mid_hi, hi) {
            acc = combine(acc, x.0, x.1);
        }
    }
    acc
}

fn aggregate_rows_checked(
    data: &ColumnData,
    nulls: &NullBits,
    lo: usize,
    hi: usize,
) -> Option<(f64, f64)> {
    let (mn, mx) = aggregate_rows(data, nulls, lo, hi);
    if mn.is_finite() { Some((mn, mx)) } else { None }
}

/// 一列：名字、单位、typed values、null bitmap、LOD 金字塔缓存。
#[derive(Debug, Clone)]
pub struct Column {
    pub name: String,
    pub unit: String,
    pub data: ColumnData,
    pub nulls: NullBits,
    /// 视口 LOD 金字塔（惰性构建；append 前缀增量续算）。
    pub lod: LodPyramid,
    /// ring 裁剪累计的全局行偏移：本地行 r 对应全局行 `dropped + r`。
    /// 金字塔按全局行锚定，裁剪只递增此值并释放前缀桶，不重算。
    dropped: usize,
}

impl Column {
    pub fn new(name: impl Into<String>, unit: impl Into<String>, data: ColumnData) -> Result<Self, String> {
        let name = name.into();
        if name.is_empty() {
            return Err("列名为空".into());
        }
        let len = data.len();
        let nulls = NullBits::new(len);
        Ok(Self { name, unit: unit.into(), data, nulls, lod: LodPyramid::default(), dropped: 0 })
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
        Ok(Self { name, unit: unit.into(), data, nulls, lod: LodPyramid::default(), dropped: 0 })
    }

    /// 金字塔就绪检查：未建或不覆盖当前全局行终点时建塔/续算。
    /// 常规路径只发生一次 O(n) 建塔 + 每次 append 的 O(新增行) 续算；
    /// ring 裁剪零成本（覆盖与当前数据仍有重叠时走增量续算）。
    pub fn ensure_lod(&mut self) {
        let end = self.dropped + self.data.len();
        if self.lod.is_built_for(end) {
            return;
        }
        let overlaps = self.lod.built_rows > 0
            && self.lod.anchor <= self.dropped
            && self.lod.anchor + self.lod.built_rows > self.dropped;
        if overlaps {
            LodPyramid::extend_into(&mut self.lod, &self.data, &self.nulls, self.dropped, end);
        } else {
            // 从未建塔，或覆盖已被 ring 裁剪完全消费 → 整塔重建。
            self.lod = LodPyramid::build(&self.data, &self.nulls, self.dropped);
        }
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
        // 金字塔前缀增量续算（2.7.2）：已建塔且覆盖未断时只重算受影响桶。
        let end = self.dropped + self.data.len();
        if self.lod.built_rows > 0 && !self.lod.is_built_for(end) {
            LodPyramid::extend_into(&mut self.lod, &self.data, &self.nulls, self.dropped, end);
        }
        Ok(())
    }

    pub fn value_type(&self) -> ValueType {
        self.data.value_type()
    }

    /// ring 裁剪：丢弃最旧的 `rows` 行，值和 null 位图同步前移。
    /// 金字塔按全局行锚定——只递增偏移并释放裁剪线以下的前缀桶，
    /// 不重算（裁剪成本 O(释放的桶数)，渲染查询零重算）。
    pub fn drop_prefix(&mut self, rows: usize) {
        if rows == 0 {
            return;
        }
        if rows >= self.len() {
            self.data.clear();
            self.nulls = NullBits::default();
            self.lod = LodPyramid::default();
            self.dropped = 0;
            return;
        }
        self.data.drop_prefix(rows);
        self.nulls.drop_prefix(rows);
        self.dropped += rows;
        self.lod.drop_prefix(self.dropped);
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
        // 单点 null 变化会让所属金字塔桶失真；置塔失效，下次渲染重建。
        self.lod = LodPyramid::default();
    }
}

/// 一张表：一份纳秒时间轴 + 表内序号 + 多列。
#[derive(Debug, Clone)]
pub struct Table {
    pub name: String,
    /// 所有列共享这一份时间轴。
    pub time_ns: Arc<Vec<i64>>,
    /// 表内序号，从 0 开始；同一纳秒内多条记录靠它排序。
    pub seq: Vec<u64>,
    pub columns: Vec<Column>,
    /// 该表的 ring 字节预算；`None` 表示不裁剪（离线 Dataset）。
    pub ring_budget_bytes: Option<usize>,
    /// 下一个 seq。ring 裁剪后仍保持表内单调递增，不归零。
    next_seq: u64,
    /// 尚未归并进主表的乱序补传段。写入只 push 段（O(批)），
    /// 读取前由 materialize 归并回主表。主表始终代表完整有序数据，
    /// 按行号直连的 ABI 与 LOD 全局行锚定都不受影响。
    pending: Vec<PendingSegment>,
    /// 段数阈值：达到则强制归并，避免段数无界导致每次读取归并退化。
    pending_limit: usize,
}

impl Table {
    pub fn new(name: impl Into<String>, time_ns: Vec<i64>) -> Result<Self, String> {
        let name = name.into();
        if name.is_empty() {
            return Err("表名为空".into());
        }
        if time_ns.windows(2).any(|w| w[1] < w[0]) {
            return Err(format!("表 {name} 的时间轴不是单调非降"));
        }
        let n = time_ns.len();
        let seq = (0..n as u64).collect();
        Ok(Self {
            name,
            time_ns: Arc::new(time_ns),
            seq,
            columns: Vec::new(),
            ring_budget_bytes: None,
            next_seq: n as u64,
            pending: Vec::new(),
            pending_limit: 64,
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
        Arc::make_mut(&mut self.time_ns).drain(0..drop);
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
        self.time_ns.len()
    }

    pub fn is_empty(&self) -> bool {
        self.time_ns.is_empty()
    }

    pub fn add_column(&mut self, column: Column) -> Result<(), String> {
        // 补传段（pending）里的 `columns`/`nulls` 是建段时的列快照，不含新列；
        // 若带着段直接 push，下次 materialize 会按新的列数遍历段内数组而越界。
        // 加列是低频 schema 操作，先把段归并进主表，付一次 O(N) 换取不变量：
        // 「pending 非空 ⇒ 段列数与主表列数一致」。无 pending 时零开销。
        self.materialize();
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

    /// 追加一批行：时间轴单调（含批次内）、列顺序/类型必须与首次建表一致；seq 由表内自动递增。
    /// 2.7.7：批次内乱序明确报错——时间轴是 lower_bound/upper_bound 二分的前提，
    /// 静默接受乱序会让窗口查询读错区间。
    pub fn append_rows(
        &mut self,
        time_ns: &[i64],
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
        let rows = time_ns.len();
        if rows == 0 {
            return Err("追加行数为 0".into());
        }
        if let Some(last) = self.time_ns.last() {
            if time_ns[0] < *last {
                return Err(format!("表 {} 的时间轴不是单调非降", self.name));
            }
        }
        if time_ns.windows(2).any(|w| w[1] < w[0]) {
            return Err(format!("表 {} 的追加批次内时间轴乱序", self.name));
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
        Arc::make_mut(&mut self.time_ns).extend_from_slice(time_ns);
        let start_seq = self.next_seq;
        self.next_seq += rows as u64;
        self.seq.extend(start_seq..start_seq + rows as u64);
        for (i, (data, nulls)) in columns.into_iter().enumerate() {
            self.columns[i].append(data, nulls)?;
        }
        self.trim_to_budget();
        Ok(())
    }

    /// 断线补传：把一段已按 time_ns 排序的历史行保留为 pending 段。
    /// 写入只付 O(批)——校验 + 分配 seq + push 段；真正的有序归并推迟到
    /// 读取前的 `materialize`。段数达阈值时就地归并，避免段数无界。
    pub fn merge_sorted_rows(
        &mut self,
        time_ns: &[i64],
        columns: Vec<(ColumnData, NullBits)>,
    ) -> Result<(), String> {
        if columns.len() != self.columns.len() {
            return Err(format!(
                "列数 {} 与表 {} 已有列数 {} 不一致",
                columns.len(), self.name, self.columns.len()
            ));
        }
        let m = time_ns.len();
        if m == 0 {
            return Ok(());
        }
        if time_ns.windows(2).any(|w| w[1] < w[0]) {
            return Err(format!("表 {} 的补传批次内时间轴乱序", self.name));
        }
        for (i, (data, _)) in columns.iter().enumerate() {
            if data.len() != m {
                return Err(format!(
                    "列 {} 长度 {} 与补传行数 {} 不一致",
                    self.columns[i].name, data.len(), m
                ));
            }
            if data.value_type() != self.columns[i].value_type() {
                return Err(format!("列 {} 类型不一致", self.columns[i].name));
            }
        }
        let seq: Vec<u64> = (self.next_seq..self.next_seq + m as u64).collect();
        self.next_seq += m as u64;
        let mut seg_columns = Vec::with_capacity(columns.len());
        let mut seg_nulls = Vec::with_capacity(columns.len());
        for (data, nulls) in columns {
            seg_columns.push(data);
            seg_nulls.push(nulls);
        }
        self.pending.push(PendingSegment {
            time_ns: time_ns.to_vec(),
            seq,
            columns: seg_columns,
            nulls: seg_nulls,
        });
        if self.pending.len() >= self.pending_limit {
            self.materialize();
        }
        Ok(())
    }

    /// 把主表与全部 pending 段按 `(time_ns, seq)` 归并回单一有序主表。
    /// 无 pending 时零开销；归并后 LOD 整塔失效、行偏移归零，保持全局行锚定语义。
    pub fn materialize(&mut self) {
        if self.pending.is_empty() {
            return;
        }
        let order = merge_order(&self.time_ns, &self.seq, &self.pending);
        let total = order.len();
        // 归并顺序天然是"同源连续 run"，时间轴/序号/列值都按 run 批量搬，走 memcpy。
        let runs = compress_runs(&order);

        let mut merged_time: Vec<i64> = Vec::with_capacity(total);
        let mut merged_seq: Vec<u64> = Vec::with_capacity(total);
        for &(src, start, len) in &runs {
            if src == 0 {
                merged_time.extend_from_slice(&self.time_ns[start..start + len]);
                merged_seq.extend_from_slice(&self.seq[start..start + len]);
            } else {
                let seg = &self.pending[src - 1];
                merged_time.extend_from_slice(&seg.time_ns[start..start + len]);
                merged_seq.extend_from_slice(&seg.seq[start..start + len]);
            }
        }

        let col_count = self.columns.len();
        for ci in 0..col_count {
            let mut sources: Vec<&ColumnData> = Vec::with_capacity(self.pending.len() + 1);
            sources.push(&self.columns[ci].data);
            for seg in &self.pending {
                sources.push(&seg.columns[ci]);
            }
            let merged_data = ColumnData::gather_from_sources(&sources, &order);
            // null 位图：全部源都没有 null 时直接留空位图（空位图语义 = 全有效），
            // 省掉逐行 is_null（109 万行实测占归并耗时的相当一部分）。
            let any_null = self.columns[ci].nulls.has_any_null()
                || self.pending.iter().any(|s| s.nulls[ci].has_any_null());
            let mut merged_nulls = NullBits::new(total);
            if any_null {
                for (dst, &(src, i)) in order.iter().enumerate() {
                    let is_null = if src == 0 {
                        self.columns[ci].nulls.is_null(i)
                    } else {
                        self.pending[src - 1].nulls[ci].is_null(i)
                    };
                    if is_null {
                        merged_nulls.set_null(dst, true);
                    }
                }
            }
            let col = &mut self.columns[ci];
            col.data = merged_data;
            col.nulls = merged_nulls;
            col.lod = LodPyramid::default();
            col.dropped = 0;
        }
        self.time_ns = Arc::new(merged_time);
        self.seq = merged_seq;
        self.pending.clear();
        self.trim_to_budget();
    }

    /// 是否有未归并的补传段。`table_data` 等只读视图据此判断是否需先归并。
    pub fn has_pending(&self) -> bool {
        !self.pending.is_empty()
    }

    /// 待归并段数与行数（benchmark / 诊断用）。
    pub fn pending_stats(&self) -> (usize, usize) {
        (self.pending.len(), self.pending.iter().map(|s| s.len()).sum())
    }

    pub fn column(&self, name: &str) -> Option<&Column> {
        self.column_index(name).and_then(|i| self.columns.get(i))
    }

    pub fn time_sec(&self, row: usize) -> Option<f64> {
        self.time_ns.get(row).map(|t| *t as f64 / 1_000_000_000.0)
    }

    /// 第一个 `time_ns >= t` 的行。
    pub fn lower_bound(&self, t_ns: i64) -> usize {
        self.time_ns.partition_point(|x| *x < t_ns)
    }

    /// 第一个 `time_ns > t` 的行。
    pub fn upper_bound(&self, t_ns: i64) -> usize {
        self.time_ns.partition_point(|x| *x <= t_ns)
    }

    /// 2.7.5 数据集切片：按行区间 `[row_start, row_end)` 与列子集抽出新表。
    ///
    /// - `columns = None` 取全部列；`Some(索引列表)` 按给定顺序取（重复索引报错）。
    /// - seq 原样保留（可追溯到原集行号），next_seq 继承，ring 预算不继承（切片是离线快照）。
    pub fn slice(
        &self,
        row_start: usize,
        row_end: usize,
        columns: Option<&[usize]>,
    ) -> Result<Table, String> {
        let rows = self.row_count();
        if row_start > row_end || row_end > rows {
            return Err(format!(
                "表 {} 切片行区间 [{row_start},{row_end}) 越界（共 {rows} 行）",
                self.name
            ));
        }
        let indices: Vec<usize> = match columns {
            None => (0..self.columns.len()).collect(),
            Some(list) => {
                let mut seen = std::collections::HashSet::new();
                let mut out = Vec::with_capacity(list.len());
                for &i in list {
                    if i >= self.columns.len() {
                        return Err(format!(
                            "表 {} 切片列序号 {i} 越界（共 {} 列）",
                            self.name,
                            self.columns.len()
                        ));
                    }
                    if !seen.insert(i) {
                        return Err(format!("表 {} 切片列序号 {i} 重复", self.name));
                    }
                    out.push(i);
                }
                out
            }
        };
        let time = self.time_ns[row_start..row_end].to_vec();
        let mut t = Table::new(self.name.clone(), time)?;
        t.seq = self.seq[row_start..row_end].to_vec();
        t.next_seq = self.next_seq;
        t.ring_budget_bytes = None;
        for i in indices {
            let c = &self.columns[i];
            let data = c.data.slice_rows(row_start, row_end);
            let nulls = c.nulls.slice_rows(row_start, row_end);
            t.add_column(Column::with_nulls(c.name.clone(), c.unit.clone(), data, nulls)?)?;
        }
        Ok(t)
    }

    /// 视口 LOD：窗口内分桶，输出 `(time_sec, min, max)` 三元组。
    ///
    /// M2.7.2：与行数解耦——
    /// - 桶边界用"与逐行扫描完全同公式"的谓词做二分查找（O(px·log n)），不再逐行扫；
    /// - 桶内 min/max 走 LOD 金字塔区间查询（O(log₄n)）；
    /// - 首次调用对全列 O(n) 建塔，append 走前缀增量续算。
    ///
    /// 输出语义（含桶的几何中心时间 x）与旧逐行扫描版完全一致。
    pub fn lod_minmax(
        &mut self,
        column: usize,
        t0_ns: i64,
        t1_ns: i64,
        px: usize,
        out: &mut [f64],
    ) -> usize {
        let cap = out.len() / 3;
        if cap == 0 || t1_ns <= t0_ns {
            return 0;
        }
        if column >= self.columns.len() {
            return 0;
        }
        let start = self.lower_bound(t0_ns);
        let end = self.upper_bound(t1_ns);
        if start >= end {
            return 0;
        }
        // 建塔/续算在渲染路径上（惰性，一次性 O(n)）。
        self.columns[column].ensure_lod();
        let px = px.max(1).min(cap);
        let span = (t1_ns - t0_ns) as f64;
        let t0_f = t0_ns as f64;
        // 与旧逐行扫描同公式：bucket(t) = floor(((t-t0)/span)*px)。
        // 用它做二分谓词，行区间与逐行扫描逐行一致。
        let first_row_at_least_bucket = |b: usize| -> usize {
            self.time_ns.partition_point(|&t| {
                let x = ((t as f64 - t0_f) / span) * px as f64;
                (x.floor() as i64) < b as i64
            })
        };
        let emit = |count: usize, b: usize, lo: usize, hi: usize, out: &mut [f64]| {
            let base = count * 3;
            // x = **桶的几何中心时间**，不是桶内第一行的时间。
            //
            // 分桶是按时间均匀切的，所以桶中心才是该桶在时间轴上的代表点。
            // 若写"桶内第一行时间"，源数据的时间戳抖动（ULog 采样并不严格
            // 等间隔）会让相邻桶的 x 间距忽大忽小——画布按 x 定位像素时
            // 就会跳格，曲线被画成一根根孤立竖线而不是连续包络。
            out[base] = (t0_f + span * (b as f64 + 0.5) / px as f64) / 1_000_000_000.0;
            match self.columns[column].lod.range_minmax(
                &self.columns[column].data,
                &self.columns[column].nulls,
                self.columns[column].dropped,
                lo,
                hi,
            ) {
                Some((mn, mx)) => {
                    out[base + 1] = mn;
                    out[base + 2] = mx;
                }
                None => {
                    out[base + 1] = f64::NAN;
                    out[base + 2] = f64::NAN;
                }
            }
        };
        let mut count = 0usize;
        for b in 0..px {
            if count >= cap {
                break;
            }
            let lo = first_row_at_least_bucket(b).min(end);
            let hi = if b + 1 >= px {
                end
            } else {
                first_row_at_least_bucket(b + 1).min(end)
            };
            if hi <= lo {
                continue; // 空桶（无行落入该时间片）
            }
            emit(count, b, lo, hi, out);
            count += 1;
        }
        count
    }
}

/// Dataset = 库，Table = 表；表名在 Dataset 内唯一。
#[derive(Debug, Clone, Default)]
pub struct Dataset {
    // BJ4a：表数据字段全部私有化——数据视图只能经 read() 守卫发放，
    // 外部 crate 无法再 `let d = &*ds;` 绕过统一读入口（D5 类缺陷编译期封死）。
    tables: Vec<Table>,
    /// 新建表的默认 ring 字节预算；`None` 表示离线 Dataset 不裁剪。
    default_ring_budget_bytes: Option<usize>,
    /// 数据代际：每次成功 append_frame 递增一次（建表也算一次）。
    /// 读侧用它做 generation 快照隔离——代际未变则派生视图无需重算。
    /// 单调不回绕（u64 溢出用 saturating_add 钳位，实际不可能达到）。
    generation: u64,
    /// 对象列 payload 池（S4）。对象列只存句柄，payload 在这里按稳定句柄寻址。
    objects: ObjectStore,
}

impl Dataset {
    pub fn new() -> Self {
        Self { tables: Vec::new(), default_ring_budget_bytes: None, generation: 0, objects: ObjectStore::default() }
    }

    /// 当前数据代际；供读侧快照比对，判断追加后是否需要刷新派生视图。
    pub fn generation(&self) -> u64 {
        self.generation
    }

    fn bump_generation(&mut self) {
        self.generation = self.generation.saturating_add(1);
    }

    /// 归并指定表的所有 pending 段。读取 ABI 在访问物理视图前调用，
    /// 保证按行号/裸缓冲/LOD 的读取看到完整有序数据。表名不存在时无操作。
    pub fn materialize_table(&mut self, name: &str) {
        if let Some(i) = self.table_index(name) {
            self.tables[i].materialize();
        }
    }

    /// 归并全部表的 pending 段（幂等：已归并的表零开销跳过）。
    pub fn materialize_all(&mut self) {
        for t in &mut self.tables {
            t.materialize();
        }
    }

    /// 唯一读入口：先归并全部 pending，再发放只读视图。
    ///
    /// BJ4a 契约：跨 crate 只能经此取得表数据视图，`MatRef` 无法手工构造，
    /// 所以「忘记物化」在编译期不可能发生（D5 类缺陷的最终封死）。
    pub fn read(&mut self) -> MatRef<'_> {
        self.materialize_all();
        MatRef { ds: self }
    }

    /// 同 [`Dataset::read`]，但只归并指定表（表名不存在时无操作）。
    /// 有明确目标表的读路径用它省掉全表遍历；其余表的 pending 状态不变。
    pub fn read_table(&mut self, name: &str) -> MatRef<'_> {
        self.materialize_table(name);
        MatRef { ds: self }
    }

    /// 对象池写入口（put/retain/release）。对象池不受 pending 归并影响，单独开放。
    pub fn objects_mut(&mut self) -> &mut ObjectStore {
        &mut self.objects
    }

    /// 对象池只读访问。payload 在写入时就已入池，与 pending 归并无关，
    /// 所以开放 `&self` 直访不构成「漏物化」绕过面。
    pub fn objects(&self) -> &ObjectStore {
        &self.objects
    }

    pub fn len(&self) -> usize {
        self.tables.len()
    }

    pub fn is_empty(&self) -> bool {
        self.tables.is_empty()
    }

    /// 指定表的 pending 概况（段数, 段内总行数）。元数据，不暴露数据视图。
    pub fn pending_stats(&self, name: &str) -> Option<(usize, usize)> {
        self.table_index(name).map(|i| self.tables[i].pending_stats())
    }

    pub fn table_index(&self, name: &str) -> Option<usize> {
        self.tables.iter().position(|t| t.name == name)
    }

    /// 按名取表（仅 crate 内部使用；外部走 read() 守卫）。
    pub(crate) fn find_table(&self, name: &str) -> Option<&Table> {
        self.table_index(name).and_then(|i| self.tables.get(i))
    }

    /// 取可变表：先归并该表 pending，再交出可变引用——写路径同样不漏归并。
    pub fn table_mut(&mut self, index: usize) -> Option<&mut Table> {
        self.tables.get_mut(index).map(|t| {
            t.materialize();
            t
        })
    }

    pub fn add_table(&mut self, name: impl Into<String>, time_ns: Vec<i64>) -> Result<usize, String> {
        let mut table = Table::new(name, time_ns)?;
        table.ring_budget_bytes = self.default_ring_budget_bytes;
        if self.tables.iter().any(|t| t.name == table.name) {
            return Err(format!("表已存在：{}", table.name));
        }
        table.trim_to_budget();
        self.tables.push(table);
        self.bump_generation();
        Ok(self.tables.len() - 1)
    }

    pub fn add_table_with_columns(
        &mut self,
        name: impl Into<String>,
        time_ns: Vec<i64>,
        columns: Vec<Column>,
    ) -> Result<usize, String> {
        let mut table = Table::new(name, time_ns)?;
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

    /// 压入一张已构建好的表（数据集切片等内部用途）；重名报错。
    pub fn push_table(&mut self, table: Table) -> Result<usize, String> {
        if self.tables.iter().any(|t| t.name == table.name) {
            return Err(format!("表已存在：{}", table.name));
        }
        self.tables.push(table);
        self.bump_generation();
        Ok(self.tables.len() - 1)
    }

    /// frame_batch 的宿主落库入口：表不存在则按首批列建表，存在则校验列顺序后追加行。
    /// `columns` 依次为 `(name, unit, data, nulls)`；nulls 只描述本批行。
    ///
    /// S2 乱序入口 reorder：上层"append 不管顺序"，本层按 time_ns 稳定排序后再灌入内核，
    /// 内核单调校验与二分地基不变。批首早于表尾时按断线补传走有序合并。
    pub fn append_frame(
        &mut self,
        table: &str,
        time_ns: Vec<i64>,
        columns: Vec<(String, String, ColumnData, NullBits)>,
    ) -> Result<(), String> {
        if time_ns.is_empty() {
            return Err("frame_batch 至少需要 1 行".into());
        }
        // 只在本批非单调时计算置换；已单调时 None，零开销直通。
        let order = if time_ns.windows(2).any(|w| w[1] < w[0]) {
            let mut ord: Vec<usize> = (0..time_ns.len()).collect();
            ord.sort_by_key(|&i| time_ns[i]); // stable：相等时间保持到达顺序
            Some(ord)
        } else {
            None
        };
        let (time_ns, columns) = match order {
            Some(ord) => {
                let sorted_t = ord.iter().map(|&i| time_ns[i]).collect();
                let sorted_c = columns.into_iter()
                    .map(|(n, u, d, nl)| (n, u, d.reorder_by(&ord), nl.reorder_by(&ord)))
                    .collect();
                (sorted_t, sorted_c)
            }
            None => (time_ns, columns),
        };

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
            let is_backfill = t.time_ns.last().is_some_and(|last| time_ns[0] < *last);
            let res = if is_backfill {
                self.tables[idx].merge_sorted_rows(&time_ns, rows)
            } else {
                self.tables[idx].append_rows(&time_ns, rows)
            };
            if res.is_ok() {
                self.bump_generation();
            }
            res
        } else {
            let mut cols = Vec::with_capacity(columns.len());
            for (name, unit, data, nulls) in columns {
                cols.push(Column::with_nulls(name, unit, data, nulls)?);
            }
            let res = self.add_table_with_columns(table, time_ns, cols).map(|_| ());
            if res.is_ok() {
                self.bump_generation();
            }
            res
        }
    }
}

/// 只读数据视图：仅可由 [`Dataset::read`] 构造（内部先归并全部 pending）。
///
/// 无法手工构造（字段私有、无 pub 构造器），所以拿到它就等于拿到了
/// 「已归并、完整有序」的保证。暴露面只有只读访问器，无任何写路径。
pub struct MatRef<'a> {
    ds: &'a Dataset,
}

impl<'a> MatRef<'a> {
    /// 全部物理表（已归并视图）。
    pub fn tables(&self) -> &[Table] {
        &self.ds.tables
    }

    pub fn find_table(&self, name: &str) -> Option<&Table> {
        self.ds.find_table(name)
    }

    pub fn table_index(&self, name: &str) -> Option<usize> {
        self.ds.table_index(name)
    }

    /// 对象池只读视图（payload 寻址）。
    pub fn objects(&self) -> &ObjectStore {
        &self.ds.objects
    }

    /// 数据代际（append_frame 递增；读侧快照隔离用）。
    pub fn generation(&self) -> u64 {
        self.ds.generation
    }

    pub fn len(&self) -> usize {
        self.ds.tables.len()
    }

    pub fn is_empty(&self) -> bool {
        self.ds.tables.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Instant;

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
        // x = 桶几何中心：时间轴为纳秒，窗口 [1e6,2e6] ns px=1 → 中心 1.5e6 ns = 0.0015 秒。
        assert_eq!(out[0], 0.0015);
        assert_eq!(out[1], 1.0);
        assert_eq!(out[2], 2.0);
    }

    #[test]
    fn time_order_is_validated() {
        assert!(Table::new("bad", vec![1, 0]).is_err());
    }

    // ===== M2.7.2 LOD 金字塔 =====

    fn lod_minmax_bruteforce(t: &Table, column: usize, t0: i64, t1: i64, px: usize) -> Vec<(f64, f64, f64)> {
        let col = &t.columns[column];
        let start = t.lower_bound(t0);
        let end = t.upper_bound(t1);
        let span = (t1 - t0) as f64;
        let mut out = Vec::new();
        let mut cur: Option<(usize, f64, f64)> = None;
        for row in start..end {
            let tt = t.time_ns[row] as f64;
            let b = (((tt - t0 as f64) / span) * px as f64).floor();
            let b = if b < 0.0 { 0 } else { (b as usize).min(px - 1) };
            match cur {
                Some((cb, mn, mx)) if cb == b => {
                    if let Some(v) = col.value_as_f64(row) {
                        if v.is_finite() {
                            cur = Some((cb, mn.min(v), mx.max(v)));
                        }
                    }
                }
                Some((_cb, mn, mx)) => {
                    out.push((0.0, mn, mx));
                    let v0 = col.value_as_f64(row).filter(|v| v.is_finite());
                    cur = Some((b, v0.unwrap_or(f64::INFINITY), v0.unwrap_or(f64::NEG_INFINITY)));
                }
                None => {
                    let v0 = col.value_as_f64(row).filter(|v| v.is_finite());
                    cur = Some((b, v0.unwrap_or(f64::INFINITY), v0.unwrap_or(f64::NEG_INFINITY)));
                }
            }
        }
        if let Some((_, mn, mx)) = cur {
            out.push((0.0, mn, mx));
        }
        out.into_iter()
            .map(|(_, mn, mx)| {
                if mn.is_finite() && mx.is_finite() {
                    (0.0, mn, mx)
                } else {
                    (0.0, f64::NAN, f64::NAN)
                }
            })
            .collect()
    }

    #[test]
    fn lod_pyramid_matches_bruteforce() {
        // 1000 行带 null / NaN / 尖峰的伪随机数据，多个视口逐一比对。
        let n = 1000usize;
        let times: Vec<i64> = (0..n).map(|i| (i * 917) as i64).collect();
        let values: Vec<f64> = (0..n)
            .map(|i| {
                let x = (i as f64) * 0.37;
                if i % 97 == 0 {
                    f64::NAN
                } else if i % 23 == 0 {
                    1e6 * (x.sin())
                } else {
                    x.sin() * 100.0
                }
            })
            .collect();
        let mut t = Table::new("att", times).unwrap();
        let mut c = Column::new("roll", "deg", ColumnData::F64(values)).unwrap();
        c.set_null(500, true);
        t.add_column(c).unwrap();
        for col in 0..1 {
            for (t0, t1, px) in [
                (0i64, (999 * 917) as i64, 240usize),
                (0, (999 * 917) as i64, 1),
                (0, (999 * 917) as i64, 1024),
                (100_000, 200_000, 64),
                (500_000, 500_100, 16),
            ] {
                let mut got = vec![0.0f64; px.max(1024) * 3];
                let k = t.lod_minmax(col, t0, t1, px, &mut got);
                let want = lod_minmax_bruteforce(&t, col, t0, t1, px);
                assert_eq!(k, want.len(), "t0={t0} t1={t1} px={px}");
                for i in 0..k {
                    let (gmn, gmx) = (got[i * 3 + 1], got[i * 3 + 2]);
                    let (wmn, wmx) = (want[i].1, want[i].2);
                    if wmn.is_nan() {
                        assert!(gmn.is_nan(), "桶 {i} 应为 NaN（t0={t0} px={px}）");
                    } else {
                        assert!((gmn - wmn).abs() < 1e-9 && (gmx - wmx).abs() < 1e-9,
                            "桶 {i} 不一致：got ({gmn},{gmx}) want ({wmn},{wmx})");
                    }
                }
            }
        }
    }

    #[test]
    fn lod_pyramid_fuzz_matches_bruteforce() {
        // 多种子模糊对拍：随机行数（含 <16、恰为桶倍数）、随机 null/NaN/尖峰、
        // 随机窗口与 px，金字塔结果必须与暴力扫描逐桶一致。
        let mut state = 0x2545_F491_4F6C_DD1Du64;
        let mut rnd = move || {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            state
        };
        for case in 0..40u32 {
            let n = match case % 6 {
                0 => 1,
                1 => 4,
                2 => 16,
                3 => (rnd() % 200) as usize,
                4 => (rnd() % 1500) as usize + 16,
                _ => 64 * ((rnd() % 30) as usize + 1), // 恰为桶倍数
            };
            let mut times = Vec::with_capacity(n);
            for i in 0..n {
                if i == 0 {
                    times.push(0);
                } else {
                    times.push(times[i - 1] + (rnd() % 10_000 + 1) as i64);
                }
            }
            let values: Vec<f64> = (0..n)
                .map(|_| {
                    let r = rnd();
                    if r % 23 == 0 {
                        f64::NAN
                    } else {
                        ((r % 200_001) as f64 - 100_000.0) / 500.0
                    }
                })
                .collect();
            let mut t = Table::new("fuzz", times).unwrap();
            let mut c = Column::new("v", "u", ColumnData::F64(values)).unwrap();
            for i in 0..n {
                if rnd() % 11 == 0 {
                    c.set_null(i, true);
                }
            }
            t.add_column(c).unwrap();
            let t_lo = t.time_ns[0];
            let t_hi = t.time_ns[n - 1];
            for _ in 0..8 {
                let a = t_lo + (rnd() % (t_hi - t_lo + 1) as u64) as i64;
                let b = t_lo + (rnd() % (t_hi - t_lo + 1) as u64) as i64;
                let (t0, t1) = (a.min(b), a.max(b));
                if t1 == t0 {
                    continue; // 实现契约：t1<=t0 返回 0 桶，参考实现不模拟该分支。
                }
                let px = ((rnd() % 1300) as usize).max(1);
                let mut got = vec![0.0f64; (px * 3).max(3)];
                let k = t.lod_minmax(0, t0, t1, px, &mut got);
                let want = lod_minmax_bruteforce(&t, 0, t0, t1, px);
                assert_eq!(k, want.len(), "case {case}: t0={t0} t1={t1} px={px}");
                for i in 0..k {
                    let (gmn, gmx) = (got[i * 3 + 1], got[i * 3 + 2]);
                    let (wmn, wmx) = (want[i].1, want[i].2);
                    let ok = if wmn.is_nan() {
                        gmn.is_nan()
                    } else {
                        (gmn - wmn).abs() < 1e-9 && (gmx - wmx).abs() < 1e-9
                    };
                    assert!(ok, "case {case} 桶 {i} 不一致：got ({gmn},{gmx}) want ({wmn},{wmx})");
                }
            }
        }
    }

    #[test]
    fn lod_pyramid_survives_drop_prefix() {
        // ring 裁剪（任意行数）后渲染必须与暴力扫描逐桶一致。
        // 覆盖三类裁剪量：4 的倍数（level0 桶对齐）、非对齐、一次裁很多。
        let mut t = Table::new("live", (0..2000i64).map(|i| i * 1000).collect::<Vec<_>>()).unwrap();
        let vals: Vec<f64> = (0..2000usize)
            .map(|i| if i % 37 == 0 { f64::NAN } else { ((i * 7) % 1000) as f64 / 10.0 })
            .collect();
        let mut c = Column::new("v", "u", ColumnData::F64(vals)).unwrap();
        for i in (0..2000usize).step_by(53) {
            c.set_null(i, true);
        }
        c.set_null(1999, true);
        t.add_column(c).unwrap();

        // 先渲染一次建塔。
        let mut out = vec![0.0f64; 240 * 3];
        assert!(t.lod_minmax(0, 0, 2_000_000, 240, &mut out) > 0);

        for drop in [4usize, 8, 16, 3, 7, 1, 500, 1000] {
            // 逐次裁剪累积，模拟 live ring。
            for col in &mut t.columns {
                col.drop_prefix(drop);
            }
            let n = t.time_ns.len();
            assert!(n > 0, "drop={drop} 裁空了表");
            let t_lo = t.time_ns[0];
            let t_hi = t.time_ns[n - 1];
            let mut got = vec![0.0f64; 240 * 3];
            let k = t.lod_minmax(0, t_lo - 1, t_hi + 1, 240, &mut got);
            let want = lod_minmax_bruteforce(&t, 0, t_lo - 1, t_hi + 1, 240);
            assert_eq!(k, want.len(), "drop={drop}");
            for i in 0..k {
                let (gmn, gmx) = (got[i * 3 + 1], got[i * 3 + 2]);
                let (wmn, wmx) = (want[i].1, want[i].2);
                let ok = if wmn.is_nan() {
                    gmn.is_nan()
                } else {
                    (gmn - wmn).abs() < 1e-9 && (gmx - wmx).abs() < 1e-9
                };
                assert!(ok, "drop={drop} 桶 {i} 不一致：got ({gmn},{gmx}) want ({wmn},{wmx})");
            }
        }
    }

    #[test]
    fn lod_pyramid_drop_prefix_releases_buckets() {
        // 裁剪必须释放前缀桶（内存可回收），且释放后查询仍正确。
        let mut t = Table::new("live", (0..4096i64).map(|i| i * 1000).collect::<Vec<_>>()).unwrap();
        let vals: Vec<f64> = (0..4096usize).map(|i| (i % 100) as f64).collect();
        t.add_column(Column::new("v", "u", ColumnData::F64(vals)).unwrap()).unwrap();
        let mut out = vec![0.0f64; 64 * 3];
        t.lod_minmax(0, 0, 4_096_000, 64, &mut out);
        let l0_before = t.columns[0].lod.levels[0].min.len();
        assert!(l0_before >= 1024);

        for col in &mut t.columns {
            col.drop_prefix(2048); // 2048 行 = level0 前 512 个桶（4 行/桶）
        }
        let l0_after = t.columns[0].lod.levels[0].min.len();
        assert_eq!(l0_after, l0_before - 512, "level0 应释放 512 个前缀桶");
        let l1_after = t.columns[0].lod.levels[1].min.len();
        assert_eq!(l1_after, l0_before / 4 - 128, "level1（16 行/桶）应释放 128 个前缀桶");

        // 释放后渲染仍正确。
        let mut got = vec![0.0f64; 64 * 3];
        let k = t.lod_minmax(0, t.time_ns[0] - 1, t.time_ns[2047] + 1, 64, &mut got);
        let want = lod_minmax_bruteforce(&t, 0, t.time_ns[0] - 1, t.time_ns[2047] + 1, 64);
        assert_eq!(k, want.len());
        for i in 0..k {
            let (gmn, gmx) = (got[i * 3 + 1], got[i * 3 + 2]);
            let (wmn, wmx) = (want[i].1, want[i].2);
            assert!((gmn - wmn).abs() < 1e-9 && (gmx - wmx).abs() < 1e-9, "桶 {i} 不一致");
        }
    }

    #[test]
    fn lod_pyramid_live_drop_append_cycle() {
        // live 场景：追加→渲染→裁剪循环 N 轮，每轮渲染与暴力扫描对拍。
        let mut t = Table::new("live", Vec::<i64>::new()).unwrap();
        t.add_column(Column::new("v", "u", ColumnData::F64(Vec::new())).unwrap()).unwrap();
        let mut next_t = 0i64;
        let mut seq_val = 0f64;
        let mut out = vec![0.0f64; 120 * 3];
        for round in 0..30u64 {
            // 每轮追加 100 行。
            let times: Vec<i64> = (0..100).map(|_i| { next_t += 1000; next_t - 1000 }).collect();
            let vals: Vec<f64> = (0..100)
                .map(|i| { seq_val += 1.0; if (round * 100 + i) % 29 == 0 { f64::NAN } else { seq_val } })
                .collect();
            let (st, sv) = (times.clone(), vals.clone());
            t.append_rows(&st, vec![(ColumnData::F64(sv), NullBits::new(100))]).unwrap();
            // 每两轮渲染一次 + 裁剪 120 行（非 4 倍数，制造桶部分相交）。
            if round % 2 == 1 {
                let n = t.time_ns.len();
                let k = t.lod_minmax(0, t.time_ns[0] - 1, t.time_ns[n - 1] + 1, 120, &mut out);
                let want = lod_minmax_bruteforce(&t, 0, t.time_ns[0] - 1, t.time_ns[n - 1] + 1, 120);
                assert_eq!(k, want.len(), "round {round}");
                for col in &mut t.columns {
                    col.drop_prefix(120.min(col.len().saturating_sub(1)));
                }
                let drop_n = 120.min(t.time_ns.len().saturating_sub(1));
                Arc::make_mut(&mut t.time_ns).drain(0..drop_n);
                t.seq.drain(0..drop_n);
            }
        }
    }

    #[test]
    fn lod_pyramid_incremental_append_matches_rebuild() {
        // append 增量续算结果必须与整塔重建逐桶一致。
        let mut t = Table::new("att", (0..100).map(|i| (i * 1_000) as i64).collect::<Vec<_>>()).unwrap();
        t.add_column(Column::new("v", "", ColumnData::F64((0..100).map(|i| (i % 13) as f64).collect())).unwrap())
            .unwrap();
        let mut got = vec![0.0f64; 3 * 32];
        t.lod_minmax(0, 0, 99_000, 32, &mut got);
        let after_append;
        {
            // 追加 50 行（金字塔增量续算）。
            let times: Vec<i64> = (100..150).map(|i| (i * 1_000) as i64).collect();
            let vals: Vec<f64> = (100..150).map(|i| (i % 7) as f64).collect();
            t.append_rows(&times, vec![(ColumnData::F64(vals), NullBits::new(50))]).unwrap();
            let mut out2 = vec![0.0f64; 3 * 64];
            t.lod_minmax(0, 0, 149_000, 64, &mut out2);
            after_append = out2;
        }
        // 同数据整塔重建（清金字塔）再算一遍。
        t.columns[0].lod = LodPyramid::default();
        let mut out3 = vec![0.0f64; 3 * 64];
        t.lod_minmax(0, 0, 149_000, 64, &mut out3);
        for i in 0..after_append.len() {
            assert!((after_append[i] - out3[i]).abs() < 1e-9 || (after_append[i].is_nan() && out3[i].is_nan()),
                "增量续算 vs 重建 第 {i} 项不一致");
        }
    }

    #[test]
    fn lod_pyramid_incremental_null_append() {
        let mut t = Table::new("att", (0..10).map(|i| (i * 1_000) as i64).collect::<Vec<_>>()).unwrap();
        t.add_column(Column::new("v", "", ColumnData::F64(vec![1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0])).unwrap())
            .unwrap();
        let mut out = [0.0f64; 3];
        t.lod_minmax(0, 0, 9_000, 1, &mut out);
        assert_eq!(out[1], 1.0);
        assert_eq!(out[2], 10.0);
        // 追加带 null 的行：增量续算后 NaN 不进 min/max。
        let mut nulls = NullBits::new(2);
        nulls.set_null(0, true);
        t.append_rows(&[10_000, 11_000], vec![(ColumnData::F64(vec![f64::NAN, -5.0]), nulls)]).unwrap();
        let mut out = [0.0f64; 3];
        t.lod_minmax(0, 0, 11_000, 1, &mut out);
        assert_eq!(out[1], -5.0);
        assert_eq!(out[2], 10.0);
    }

    // ===== M2.7.5 数据集切片 =====

    #[test]
    fn slice_preserves_rows_and_nulls() {
        let mut t = Table::new("att", vec![0, 1_000, 2_000, 3_000, 4_000]).unwrap();
        let mut c = Column::new("roll", "deg", ColumnData::F32(vec![1.0, 2.0, 3.0, 4.0, 5.0])).unwrap();
        c.set_null(2, true);
        t.add_column(c).unwrap();
        t.add_column(Column::new("pitch", "deg", ColumnData::I16(vec![10, 20, 30, 40, 50])).unwrap())
            .unwrap();
        let mut s = t.slice(1, 4, Some(&[1, 0])).unwrap();
        assert_eq!(s.row_count(), 3);
        assert_eq!(s.time_ns.as_slice(), &[1_000, 2_000, 3_000]);
        assert_eq!(s.seq, vec![1, 2, 3]);
        // 列顺序按给定索引（pitch 在前）。
        assert_eq!(s.columns[0].name, "pitch");
        assert_eq!(s.columns[0].value_as_f64(0), Some(20.0));
        assert_eq!(s.columns[1].name, "roll");
        assert_eq!(s.columns[1].value_as_f64(0), Some(2.0));
        // null 标记随行平移。
        assert!(s.columns[1].is_null(1));
        assert_eq!(s.columns[1].value_as_f64(1), None);
        // next_seq 继承，append 按列顺序继续增长。
        // 列类型顺序反了（F32 对上 I16）：必须报错。
        assert!(s.append_rows(&[4_000], vec![(ColumnData::F32(vec![6.0]), NullBits::new(1)),
                                            (ColumnData::I16(vec![60]), NullBits::new(1))])
            .is_err());
        assert_eq!(s.row_count(), 3); // 失败的 append 不进表
        let mut s2 = t.slice(0, 5, None).unwrap();
        s2.append_rows(&[5_000],
            vec![(ColumnData::F32(vec![6.0]), NullBits::new(1)),
                 (ColumnData::I16(vec![60]), NullBits::new(1))])
            .unwrap();
        assert_eq!(s2.seq[5], 5);
    }

    #[test]
    fn slice_rejects_bad_ranges_and_dup_columns() {
        let mut t = Table::new("att", vec![0, 1_000]).unwrap();
        t.add_column(Column::new("roll", "", ColumnData::F64(vec![1.0, 2.0])).unwrap()).unwrap();
        assert!(t.slice(0, 3, None).is_err());
        assert!(t.slice(2, 1, None).is_err());
        assert!(t.slice(0, 2, Some(&[0, 0])).is_err());
        assert!(t.slice(0, 2, Some(&[1])).is_err());
        assert!(t.slice(0, 0, None).is_ok()); // 空切片合法
    }

    // ===== M2.7.7 append 乱序校验 =====

    #[test]
    fn append_rejects_out_of_order_batch() {
        let mut t = Table::new("att", vec![0, 1_000]).unwrap();
        t.add_column(Column::new("roll", "", ColumnData::F64(vec![1.0, 2.0])).unwrap()).unwrap();
        // 批内乱序（首行仍 >= 表末行）：必须报错，杜绝二分静默出错。
        assert!(t.append_rows(&[5_000, 4_000],
            vec![(ColumnData::F64(vec![3.0, 4.0]), NullBits::new(2))]).is_err());
        // 乱序被拒后数据不能进表。
        assert_eq!(t.row_count(), 2);
        // 合法追加仍可用。
        assert!(t.append_rows(&[5_000, 6_000],
            vec![(ColumnData::F64(vec![3.0, 4.0]), NullBits::new(2))]).is_ok());
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
        assert_eq!(t.time_ns.as_slice(), &[0, 1_000, 2_000]);
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
        assert_eq!(t.time_ns.as_slice(), &[5_000, 6_000, 7_000, 8_000, 9_000]);
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
        assert_eq!(t.time_ns.as_slice(), &[8_000, 9_000, 10_000, 11_000, 12_000]);
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

    // ---------- S2 乱序入口 reorder ----------

    fn frame(_times: &[i64], vals: &[f64]) -> Vec<(String, String, ColumnData, NullBits)> {
        vec![(
            "v".into(), "".into(),
            ColumnData::F64(vals.to_vec()),
            NullBits::new(vals.len()),
        )]
    }

    fn f64_col(t: &Table, name: &str) -> Vec<f64> {
        match &t.column(name).unwrap().data {
            ColumnData::F64(v) => v.clone(),
            _ => panic!("期望 F64 列"),
        }
    }

    #[test]
    fn reorder_out_of_order_batch_keeps_time_monotonic_and_values_aligned() {
        let mut ds = Dataset::new();
        // 到达顺序时间乱序：t=30,10,20 对应值 3,1,2；reorder 后应为 (10,1),(20,2),(30,3)。
        ds.append_frame("att", vec![30_000, 10_000, 20_000], frame(&[], &[3.0, 1.0, 2.0])).unwrap();
        let t = ds.find_table("att").unwrap();
        assert_eq!(t.time_ns.as_slice(), &[10_000, 20_000, 30_000]);
        assert_eq!(f64_col(t, "v"), vec![1.0, 2.0, 3.0]);
        assert_eq!(t.lower_bound(20_000), 1);
    }

    #[test]
    fn reorder_matches_ordered_ingest_window_query() {
        let mut a = Dataset::new();
        let mut b = Dataset::new();
        let times = vec![50_000i64, 10_000, 40_000, 20_000, 30_000];
        let vals = vec![5.0f64, 1.0, 4.0, 2.0, 3.0];
        // a 乱序灌入；b 有序灌入作为对拍基准。
        a.append_frame("t", times.clone(), frame(&[], &vals)).unwrap();
        let (ot, ov) = (vec![10_000i64, 20_000, 30_000, 40_000, 50_000], vec![1.0f64, 2.0, 3.0, 4.0, 5.0]);
        b.append_frame("t", ot, frame(&[], &ov)).unwrap();
        let ta = a.find_table("t").unwrap();
        let tb = b.find_table("t").unwrap();
        assert_eq!(ta.time_ns.as_slice(), tb.time_ns.as_slice());
        assert_eq!(f64_col(ta, "v"), f64_col(tb, "v"));
        // 窗口查询对拍：[15ms, 45ms]，用 lower_bound/upper_bound 取区间
        let (s, e) = (ta.lower_bound(15_000), ta.upper_bound(45_000));
        let (s2, e2) = (tb.lower_bound(15_000), tb.upper_bound(45_000));
        assert_eq!((s, e), (s2, e2));
        assert_eq!(&f64_col(ta, "v")[s..e], &f64_col(tb, "v")[s2..e2]);
    }

    #[test]
    fn reorder_preserves_null_alignment() {
        let mut ds = Dataset::new();
        let mut nulls = NullBits::new(3);
        nulls.set_null(0, true); // 到达行 0（t=30）为 null
        ds.append_frame(
            "t",
            vec![30_000, 10_000, 20_000],
            vec![("v".into(), "".into(), ColumnData::F64(vec![3.0, 1.0, 2.0]), nulls)],
        ).unwrap();
        let t = ds.find_table("t").unwrap();
        // 原到达行 0 现在是排序后第 2 行（t=30）。
        assert_eq!(t.time_ns.as_slice(), &[10_000, 20_000, 30_000]);
        assert!(!t.column("v").unwrap().is_null(0));
        assert!(!t.column("v").unwrap().is_null(1));
        assert!(t.column("v").unwrap().is_null(2));
    }

    #[test]
    fn backfill_history_segment_merges_and_keeps_binary_search_correct() {
        let mut ds = Dataset::new();
        ds.append_frame("t", vec![10_000, 20_000, 30_000], frame(&[], &[1.0, 3.0, 5.0])).unwrap();
        // 断线补传一段更早的历史：5ms、25ms（补传内部也乱序到达 25 先到）。
        ds.append_frame("t", vec![25_000, 5_000], frame(&[], &[4.0, 0.0])).unwrap();
        // 写入先进 pending 段；读取前物化回主表，行为与整表重建一致。
        assert_eq!(ds.find_table("t").unwrap().pending_stats(), (1, 2));
        ds.materialize_table("t");
        let t = ds.find_table("t").unwrap();
        assert_eq!(t.pending_stats(), (0, 0));
        assert_eq!(t.time_ns.as_slice(), &[5_000, 10_000, 20_000, 25_000, 30_000]);
        assert_eq!(f64_col(t, "v"), vec![0.0, 1.0, 3.0, 4.0, 5.0]);
        assert_eq!(t.lower_bound(25_000), 3);
        assert_eq!(t.upper_bound(25_000), 4);
        assert_eq!(t.time_sec(0), Some(0.000005));
    }

    #[test]
    fn backfill_segments_merge_matches_full_sort_bitwise() {
        // 多批重叠补传 + 重复时间：归并结果与全量排序逐位一致。
        let mut ds = Dataset::new();
        ds.append_frame("t", vec![100, 300, 500], frame(&[], &[1.0, 3.0, 5.0])).unwrap();
        ds.append_frame("t", vec![200, 400], frame(&[], &[2.0, 4.0])).unwrap();
        ds.append_frame("t", vec![150, 350], frame(&[], &[1.5, 3.5])).unwrap();
        ds.append_frame("t", vec![300, 50], frame(&[], &[30.0, 0.5])).unwrap();
        assert_eq!(ds.find_table("t").unwrap().pending_stats().0, 3);
        ds.materialize_table("t");
        let t = ds.find_table("t").unwrap();
        assert_eq!(t.time_ns.as_slice(), &[50, 100, 150, 200, 300, 300, 350, 400, 500]);
        // 重复时间 300：先到达的 seq 小、值 3.0 在前，后补传的 30.0 在后。
        assert_eq!(f64_col(t, "v"), vec![0.5, 1.0, 1.5, 2.0, 3.0, 30.0, 3.5, 4.0, 5.0]);
        // seq 是到达序，按时间归并后不单调，但必须唯一。
        let seq = &t.seq;
        let mut sorted = seq.clone();
        sorted.sort_unstable();
        sorted.dedup();
        assert_eq!(sorted.len(), seq.len());
        assert_eq!(seq.len(), 9);
    }

    #[test]
    fn backfill_then_ring_trim_keeps_seq_and_null_aligned() {
        // 补传段物化后再 ring 裁剪：时间单调、seq 唯一、null 位图对齐。
        let mut ds = Dataset::new();
        let times: Vec<i64> = (0..1000).map(|i| i * 10).collect();
        let vals: Vec<f64> = (0..1000).map(|i| i as f64).collect();
        let mut nulls = NullBits::new(1000);
        for i in (0..1000).step_by(7) { nulls.set_null(i, true); }
        let cols = vec![("v".into(), "".into(), ColumnData::F64(vals), nulls)];
        ds.append_frame("t", times, cols).unwrap();
        let ti = ds.table_index("t").unwrap();
        ds.table_mut(ti).unwrap().set_ring_budget(Some(6000));
        // 补传更早的行后物化。
        ds.append_frame("t", vec![5, 15], frame(&[], &[-1.0, -2.0])).unwrap();
        ds.materialize_table("t");
        let t = ds.find_table("t").unwrap();
        assert!(t.time_ns.windows(2).all(|w| w[0] <= w[1]));
        let seq = &t.seq;
        let mut sorted = seq.clone(); sorted.sort_unstable(); sorted.dedup();
        assert_eq!(sorted.len(), seq.len(), "seq 必须唯一");
        let c = t.column("v").unwrap();
        assert_eq!(c.len(), t.row_count());
        // 时钟裁剪后行数受预算约束（含补传行）。
        assert!(t.row_count() <= 1002);
    }

    #[test]
    fn bench_backfill_batched_vs_per_batch_merge() {
        // 批量补传基准：主表 5 万行，分 20 批各 1000 行补到表中部。
        // 旧路径=每批立即归并（O(N)/批）；新路径=攒段最后归并一次（O(m)/批 + O(N)）。
        const BASE: usize = 50_000;
        const BATCHES: usize = 20;
        const BATCH: usize = 1_000;
        let make_base = || {
            let times: Vec<i64> = (0..BASE).map(|i| i as i64 * 10).collect();
            let vals: Vec<f64> = (0..BASE).map(|i| i as f64).collect();
            let mut ds = Dataset::new();
            ds.append_frame("t", times, frame(&[], &vals)).unwrap();
            ds
        };
        // 20 批补传，时间均匀落在表中部 [BASE/4*10, BASE/2*10)。
        let batches: Vec<(Vec<i64>, Vec<f64>)> = (0..BATCHES).map(|b| {
            let times: Vec<i64> = (0..BATCH).map(|i| (BASE / 4 * 10) as i64 + (b * BATCH + i) as i64).collect();
            let vals: Vec<f64> = (0..BATCH).map(|i| (b * BATCH + i) as f64 + 0.5).collect();
            (times, vals)
        }).collect();

        // 旧路径：每批 backfill 后立即 materialize。
        let t0 = Instant::now();
        {
            let mut ds = make_base();
            for (times, vals) in &batches {
                ds.append_frame("t", times.clone(), frame(&[], vals)).unwrap();
                ds.materialize_table("t"); // 模拟旧实现每批整表重建
            }
        }
        let per_batch = t0.elapsed();

        // 新路径：攒段，最后归并一次。
        let t1 = Instant::now();
        {
            let mut ds = make_base();
            for (times, vals) in &batches {
                ds.append_frame("t", times.clone(), frame(&[], vals)).unwrap();
            }
            assert_eq!(ds.find_table("t").unwrap().pending_stats().0, BATCHES);
            ds.materialize_table("t");
        }
        let batched = t1.elapsed();

        println!("\n[bench BJ4] base={BASE} batches={BATCHES}x{BATCH}");
        println!("  逐批立即归并(旧 O(N)): {:?}", per_batch);
        println!("  攒段最后归并(新 O(m)): {:?}", batched);
        println!("  加速比: {:.2}x", per_batch.as_secs_f64() / batched.as_secs_f64().max(1e-9));

        // 两条路径结果必须逐位一致（行为保持）。
        let mut a = make_base();
        for (times, vals) in &batches { a.append_frame("t", times.clone(), frame(&[], vals)).unwrap(); a.materialize_table("t"); }
        let mut b = make_base();
        for (times, vals) in &batches { b.append_frame("t", times.clone(), frame(&[], vals)).unwrap(); }
        b.materialize_table("t");
        let ta = a.find_table("t").unwrap();
        let tb = b.find_table("t").unwrap();
        assert_eq!(ta.time_ns, tb.time_ns);
        assert_eq!(f64_col(ta, "v"), f64_col(tb, "v"));

        // 批大小远小于主表时，攒段应显著快于逐批归并。
        assert!(batched < per_batch, "攒段归并应快于逐批 O(N) 重建");
    }

    #[test]
    fn backfill_segment_limit_forces_materialize() {
        // 段数达阈值自动归并，避免段数无界。
        let mut ds = Dataset::new();
        ds.append_frame("t", vec![0], frame(&[], &[0.0])).unwrap();
        let limit = ds.find_table("t").unwrap().pending_limit;
        for i in 1..=limit {
            ds.append_frame("t", vec![i as i64 * 10 - 5], frame(&[], &[i as f64])).unwrap();
        }
        // 达到阈值时触发归并，pending 清零。
        assert_eq!(ds.find_table("t").unwrap().pending_stats().0, 0);
        let t = ds.find_table("t").unwrap();
        assert_eq!(t.row_count(), limit + 1);
        // 时间轴仍单调。
        assert!(t.time_ns.windows(2).all(|w| w[0] <= w[1]));
    }

    // ---------- S4 对象列 ObjectStore ----------

    #[test]
    fn object_store_handle_is_stable_across_growth_and_bytes_roundtrip() {
        let mut store = ObjectStore::default();
        let h1 = store.put(&[1, 2, 3, 4], 7);
        assert_eq!(h1, 1);
        // 再写几条，触发 bytes Vec 扩容；h1 必须仍指向原 payload。
        let h2 = store.put(&[9, 8, 7, 6, 5], 8);
        let _h3 = store.put(&[0, 0], 9);
        let (b, tag) = store.get(h1).unwrap();
        assert_eq!(b, &[1, 2, 3, 4]);
        assert_eq!(tag, 7);
        let (b2, tag2) = store.get(h2).unwrap();
        assert_eq!(b2, &[9, 8, 7, 6, 5]);
        assert_eq!(tag2, 8);
    }

    #[test]
    fn object_store_refcount_prevents_dangling_and_rejects_double_release() {
        let mut store = ObjectStore::default();
        let h = store.put(b"cloud", 1);
        // 数据集自持 1 引用；retain 后 2。
        assert!(store.retain(h));
        assert!(store.release(h)); // 剩 1（dataset 自持），仍有效
        assert_eq!(store.get(h).unwrap().0, b"cloud");
        assert!(store.release(h)); // 归零，失效
        assert!(store.get(h).is_none(), "释放后句柄必须读不到（无悬垂）");
        assert!(!store.release(h), "重复释放应失败");
        assert!(!store.retain(h), "已失效句柄不可再 retain");
        // 句柄 0 与越界句柄都读不到。
        assert!(store.get(0).is_none());
        assert!(store.get(9999).is_none());
    }

    #[test]
    fn object_column_aligned_with_scalar_on_same_timeline() {
        // 一张表同时有标量 v 与对象列 obj，共享 time_ns；reorder 后两者仍按同一 t 对齐。
        let mut ds = Dataset::new();
        let h_a = ds.objects.put(b"payload-a", 1);
        let h_b = ds.objects.put(b"payload-b", 1);
        let h_c = ds.objects.put(b"payload-c", 1);
        let cols = vec![
            ("v".into(), "".into(), ColumnData::F64(vec![3.0, 1.0, 2.0]), NullBits::new(3)),
            ("obj".into(), "".into(), ColumnData::Object(vec![h_a, h_b, h_c]), NullBits::new(3)),
        ];
        // 到达乱序：t=30,10,20；reorder 后应为 (10,1,h_b),(20,2,h_c),(30,3,h_a)。
        ds.append_frame("topic", vec![30_000, 10_000, 20_000], cols).unwrap();
        let t = ds.find_table("topic").unwrap();
        assert_eq!(t.time_ns.as_slice(), &[10_000, 20_000, 30_000]);
        assert_eq!(f64_col(t, "v"), vec![1.0, 2.0, 3.0]);
        let handles = match &t.column("obj").unwrap().data {
            ColumnData::Object(v) => v.clone(),
            _ => panic!("期望 Object 列"),
        };
        assert_eq!(handles, vec![h_b, h_c, h_a], "对象句柄必须随标量一起按 t 重排");
        // 同一 t 同时取到标量值与对象 payload：t=10ms 行 → v=1.0, obj=payload-b。
        let (bytes, tag) = ds.objects.get(handles[0]).unwrap();
        assert_eq!(bytes, b"payload-b");
        assert_eq!(tag, 1);
        assert_eq!(f64_col(t, "v")[0], 1.0);
        // 对象列无数值语义，value_as_f64 返回 None（不会被当曲线画）。
        assert_eq!(t.column("obj").unwrap().value_as_f64(0), None);
    }

    // ===== S5 选择性列编码压缩 =====

    fn decode_to_f64(enc: &ColumnData) -> Vec<Option<f64>> {
        let n = enc.len();
        (0..n).map(|r| enc.value_as_f64(r)).collect()
    }

    #[test]
    fn codec_constant_roundtrip_and_memory_drop() {
        let raw = ColumnData::I64(vec![42; 1000]);
        let raw_bytes = raw.memory_bytes();
        let enc = raw.encode(Codec::Constant).unwrap();
        assert_eq!(enc.value_type(), ValueType::I64);
        assert_eq!(enc.codec(), Some(Codec::Constant));
        assert_eq!(enc.len(), 1000);
        assert_eq!(decode_to_f64(&enc), vec![Some(42.0); 1000]);
        let dec = enc.decode().unwrap();
        assert_eq!(dec, raw, "解压逐位一致");
        assert!(enc.memory_bytes() < raw_bytes / 10, "常数列内存应显著下降");
    }

    #[test]
    fn codec_for_roundtrip_low_cardinality_status() {
        // 状态码小范围整数：FOR 基值 + u32 偏移。
        let vals: Vec<i64> = (0..1000).map(|i| 100 + (i % 5) as i64).collect();
        let raw = ColumnData::I64(vals.clone());
        let enc = raw.encode(Codec::For).unwrap();
        assert_eq!(enc.codec(), Some(Codec::For));
        let dec = enc.decode().unwrap();
        assert_eq!(dec, raw, "FOR 解码逐位一致");
        for (i, v) in vals.iter().enumerate() {
            assert_eq!(enc.value_as_f64(i), Some(*v as f64));
        }
        assert!(enc.memory_bytes() < raw.memory_bytes(), "FOR 应比原生 i64 省内存");
    }

    #[test]
    fn codec_dictionary_roundtrip_text_and_null_bitmap() {
        let cats = ["alpha", "beta", "gamma"];
        let vals: Vec<String> = (0..900).map(|i| cats[i % 3].to_string()).collect();
        let raw = ColumnData::Text(vals.clone());
        let enc = raw.encode(Codec::Dictionary).unwrap();
        assert_eq!(enc.codec(), Some(Codec::Dictionary));
        let dec = enc.decode().unwrap();
        assert_eq!(dec, raw, "字典解码逐位一致（含文本）");
        // null 位图独立于列存储，编码不改变它。
        let mut nulls = NullBits::new(900);
        nulls.set_null(5, true);
        nulls.set_null(500, true);
        let col = Column::with_nulls("cat", "", enc, nulls.clone()).unwrap();
        assert!(col.is_null(5));
        assert!(col.is_null(500));
        assert_eq!(col.value_text(0).as_deref(), Some("alpha"));
        assert_eq!(col.value_text(6).as_deref(), Some("alpha")); // 6%3==0
        assert_eq!(col.value_text(2).as_deref(), Some("gamma")); // 2%3==2，非 null
        assert_eq!(col.value_text(500), None, "null 行不返回文本值");
    }

    #[test]
    fn codec_bitpack_roundtrip_bool() {
        let vals: Vec<bool> = (0..1000).map(|i| i % 3 == 0).collect();
        let raw = ColumnData::Bool(vals.clone());
        let enc = raw.encode(Codec::BitPack).unwrap();
        assert_eq!(enc.codec(), Some(Codec::BitPack));
        let dec = enc.decode().unwrap();
        assert_eq!(dec, raw, "位打包解码逐位一致");
        for (i, v) in vals.iter().enumerate() {
            assert_eq!(enc.value_as_f64(i), Some(if *v { 1.0 } else { 0.0 }));
        }
        assert!(enc.memory_bytes() <= raw.memory_bytes() / 8 + 1, "布尔位打包应约 1/8 内存");
    }

    #[test]
    fn codec_encoded_column_lod_matches_raw() {
        // 编码列的 LOD 建塔经 value_as_f64 单行解码，须与原生列结果一致（对拍基线）。
        let vals: Vec<f64> = (0..1000).map(|i| (i % 100) as f64).collect();
        let mut raw_col = column("v", ColumnData::F64(vals.clone()));
        let enc_data = ColumnData::F64(vals).encode(Codec::Dictionary).unwrap();
        let mut enc_col = column("v", enc_data);
        raw_col.ensure_lod();
        enc_col.ensure_lod();
        // 逐点取值一致
        for r in 0..1000 {
            assert_eq!(raw_col.value_as_f64(r), enc_col.value_as_f64(r), "行 {r} 解码值须与原生一致");
        }
        // 多区间 LOD 查询一致
        for &(lo, hi) in &[(0usize, 1000usize), (10, 50), (333, 999), (997, 1000)] {
            let a = raw_col.lod.range_minmax(&raw_col.data, &raw_col.nulls, 0, lo, hi);
            let b = enc_col.lod.range_minmax(&enc_col.data, &enc_col.nulls, 0, lo, hi);
            assert_eq!(a, b, "LOD 区间 [{lo},{hi}) 须一致");
        }
    }

    #[test]
    fn codec_append_decode_append_reencode() {
        let mut enc = ColumnData::I64(vec![10, 20, 30]).encode(Codec::For).unwrap();
        let add = ColumnData::I64(vec![40, 50]);
        enc.append(add).unwrap();
        assert_eq!(enc.len(), 5);
        assert_eq!(enc.decode().unwrap(), ColumnData::I64(vec![10, 20, 30, 40, 50]));
    }

    #[test]
    fn codec_reorder_and_slice_keep_values() {
        let enc = ColumnData::I64(vec![5, 1, 3, 9]).encode(Codec::Dictionary).unwrap();
        let reordered = enc.reorder_by(&[3, 0, 2, 1]);
        assert_eq!(reordered.decode().unwrap(), ColumnData::I64(vec![9, 5, 3, 1]));
        let sliced = enc.slice_rows(1, 3);
        assert_eq!(sliced.decode().unwrap(), ColumnData::I64(vec![1, 3]));
    }

    #[test]
    fn codec_primary_f64_channel_unchanged() {
        // 不调用 encode 时 F64 走原生零拷贝路径，value_type/len/columnRaw 语义不变。
        let raw = ColumnData::F64(vec![1.5, 2.5, 3.5]);
        assert_eq!(raw.codec(), None);
        assert_eq!(raw.value_type(), ValueType::F64);
        assert_eq!(raw.memory_bytes(), 3 * 8);
        assert_eq!(raw.value_as_f64(1), Some(2.5));
    }
}
