//! 数据集二进制缓存：只接收不透明路径与源文件指纹，不认 loader / 选项。
//! 缓存 key 与失效策略仍在 app；这里只负责“把 Dataset 原子写盘、校验后读回”。

use ps_ndf::{Dataset, Series};
use std::fs::{self, File};
use std::io::{BufReader, BufWriter, Read, Write};
use std::path::{Path, PathBuf};
use std::sync::Arc;

const MAGIC: u32 = 0x5053_4331; // PSC1
const VERSION: u32 = 1;
const PARAMS_MAGIC: u32 = 0x5053_5031; // PSP1
const MAX_CHANNELS: u32 = 65_536;
const MAX_POINTS: u32 = 50_000_000;

fn write_u32(w: &mut impl Write, v: u32) -> Result<(), String> {
    w.write_all(&v.to_le_bytes()).map_err(|e| e.to_string())
}

fn write_i64(w: &mut impl Write, v: i64) -> Result<(), String> {
    w.write_all(&v.to_le_bytes()).map_err(|e| e.to_string())
}

fn read_u32(r: &mut impl Read) -> Result<u32, String> {
    let mut b = [0u8; 4];
    r.read_exact(&mut b).map_err(|e| e.to_string())?;
    Ok(u32::from_le_bytes(b))
}

fn read_i64(r: &mut impl Read) -> Result<i64, String> {
    let mut b = [0u8; 8];
    r.read_exact(&mut b).map_err(|e| e.to_string())?;
    Ok(i64::from_le_bytes(b))
}

fn read_f64s(r: &mut impl Read, n: usize) -> Result<Vec<f64>, String> {
    let mut out = Vec::with_capacity(n);
    for _ in 0..n {
        let mut b = [0u8; 8];
        r.read_exact(&mut b).map_err(|e| e.to_string())?;
        out.push(f64::from_le_bytes(b));
    }
    Ok(out)
}

/// 原子写：先写临时文件，成功后 rename。`source_size/mtime` 是源文件指纹。
pub fn save_dataset(
    path: &Path,
    ds: &Dataset,
    source_size: i64,
    source_mtime: i64,
) -> Result<(), String> {
    if ds.channels.len() > MAX_CHANNELS as usize {
        return Err("通道数超过缓存上限".into());
    }
    let tmp = temp_path(path);
    {
        let f = File::create(&tmp).map_err(|e| e.to_string())?;
        let mut w = BufWriter::new(f);
        write_u32(&mut w, MAGIC)?;
        write_u32(&mut w, VERSION)?;
        write_i64(&mut w, source_size)?;
        write_i64(&mut w, source_mtime)?;
        write_u32(&mut w, ds.channels.len() as u32)?;
        for ch in &ds.channels {
            if ch.t.len() > MAX_POINTS as usize || ch.v.len() > MAX_POINTS as usize {
                return Err("通道点数超过缓存上限".into());
            }
            let name = ch.name.as_bytes();
            let unit = ch.unit.as_bytes();
            write_u32(&mut w, name.len() as u32)?;
            write_u32(&mut w, unit.len() as u32)?;
            write_u32(&mut w, ch.t.len() as u32)?;
            w.write_all(name).map_err(|e| e.to_string())?;
            w.write_all(unit).map_err(|e| e.to_string())?;
            for x in ch.t.iter() {
                w.write_all(&x.to_le_bytes()).map_err(|e| e.to_string())?;
            }
            for x in &ch.v {
                w.write_all(&x.to_le_bytes()).map_err(|e| e.to_string())?;
            }
        }
        w.flush().map_err(|e| e.to_string())?;
    }
    fs::rename(&tmp, path).map_err(|e| {
        let _ = fs::remove_file(&tmp);
        e.to_string()
    })
}


/// 参数 JSON 缓存：与数据集缓存同一套指纹/原子写，避免 app 再维护 QDataStream。
pub fn save_params(path: &Path, json: &str, source_size: i64, source_mtime: i64) -> Result<(), String> {
    if json.len() > 4 * 1024 * 1024 {
        return Err("参数 JSON 超过 4 MiB".into());
    }
    let tmp = temp_path(path);
    {
        let f = File::create(&tmp).map_err(|e| e.to_string())?;
        let mut w = BufWriter::new(f);
        write_u32(&mut w, PARAMS_MAGIC)?;
        write_u32(&mut w, VERSION)?;
        write_i64(&mut w, source_size)?;
        write_i64(&mut w, source_mtime)?;
        write_u32(&mut w, json.len() as u32)?;
        w.write_all(json.as_bytes()).map_err(|e| e.to_string())?;
        w.flush().map_err(|e| e.to_string())?;
    }
    fs::rename(&tmp, path).map_err(|e| {
        let _ = fs::remove_file(&tmp);
        e.to_string()
    })
}

pub fn load_params(path: &Path, expected_size: i64, expected_mtime: i64) -> Result<Option<String>, String> {
    if !path.is_file() {
        return Ok(None);
    }
    let f = File::open(path).map_err(|e| e.to_string())?;
    let mut r = BufReader::new(f);
    if read_u32(&mut r)? != PARAMS_MAGIC || read_u32(&mut r)? != VERSION {
        return Ok(None);
    }
    if read_i64(&mut r)? != expected_size || read_i64(&mut r)? != expected_mtime {
        return Ok(None);
    }
    let len = read_u32(&mut r)? as usize;
    if len == 0 || len > 4 * 1024 * 1024 {
        return Ok(None);
    }
    let mut buf = vec![0u8; len];
    r.read_exact(&mut buf).map_err(|e| e.to_string())?;
    Ok(Some(String::from_utf8_lossy(&buf).into_owned()))
}

/// 返回 `Ok(None)` 表示缓存过期/不存在；`Err` 表示文件损坏。
pub fn load_dataset(
    path: &Path,
    expected_size: i64,
    expected_mtime: i64,
) -> Result<Option<Dataset>, String> {
    if !path.is_file() {
        return Ok(None);
    }
    let f = File::open(path).map_err(|e| e.to_string())?;
    let mut r = BufReader::new(f);
    if read_u32(&mut r)? != MAGIC || read_u32(&mut r)? != VERSION {
        return Ok(None);
    }
    let size = read_i64(&mut r)?;
    let mtime = read_i64(&mut r)?;
    if size != expected_size || mtime != expected_mtime {
        return Ok(None);
    }
    let channels = read_u32(&mut r)?;
    if channels == 0 || channels > MAX_CHANNELS {
        return Ok(None);
    }
    let mut ds = Dataset::new();
    for _ in 0..channels {
        let name_len = read_u32(&mut r)? as usize;
        let unit_len = read_u32(&mut r)? as usize;
        let count = read_u32(&mut r)? as usize;
        if name_len > 65_536 || unit_len > 4_096 || count > MAX_POINTS as usize {
            return Ok(None);
        }
        let mut name = vec![0u8; name_len];
        let mut unit = vec![0u8; unit_len];
        r.read_exact(&mut name).map_err(|e| e.to_string())?;
        r.read_exact(&mut unit).map_err(|e| e.to_string())?;
        let t = read_f64s(&mut r, count)?;
        let v = read_f64s(&mut r, count)?;
        let series = Series {
            name: String::from_utf8_lossy(&name).into_owned(),
            unit: String::from_utf8_lossy(&unit).into_owned(),
            t: Arc::new(t),
            v,
        };
        ds.add_channel(series)?;
    }
    Ok(Some(ds))
}

fn temp_path(path: &Path) -> PathBuf {
    let mut tmp = path.as_os_str().to_owned();
    tmp.push(format!(".tmp-{}", std::process::id()));
    PathBuf::from(tmp)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn save_and_load_roundtrip() {
        let dir = std::env::temp_dir().join(format!("ps_cache_test_{}", std::process::id()));
        let _ = fs::create_dir_all(&dir);
        let path = dir.join("x.pscache");
        let mut ds = Dataset::new();
        let mut s = Series::new("roll");
        s.unit = "deg".into();
        s.push(0.0, 1.0);
        s.push(1.0, 2.0);
        ds.add_channel(s).unwrap();
        save_dataset(&path, &ds, 12, 34).unwrap();
        let hit = load_dataset(&path, 12, 34).unwrap().unwrap();
        assert_eq!(hit.channels.len(), 1);
        assert_eq!(hit.channels[0].name, "roll");
        assert_eq!(hit.channels[0].v, vec![1.0, 2.0]);
        assert!(load_dataset(&path, 13, 34).unwrap().is_none());

        let ppath = dir.join("x.psparams");
        save_params(&ppath, "{\"a\":1}", 12, 34).unwrap();
        assert_eq!(load_params(&ppath, 12, 34).unwrap().unwrap(), "{\"a\":1}");
        assert!(load_params(&ppath, 13, 34).unwrap().is_none());
        let _ = fs::remove_dir_all(&dir);
    }
}

// ===== PSC2：typed Table/Column 缓存 =====

use ps_ndf::typed::{Column, ColumnData, Dataset as TypedDataset, NullBits, ValueType};

const TYPED_MAGIC: u32 = 0x5053_4332; // PSC2
const TYPED_VERSION: u32 = 2;
const MAX_COLUMNS: u32 = 4096;

fn write_u64(w: &mut impl Write, v: u64) -> Result<(), String> {
    w.write_all(&v.to_le_bytes()).map_err(|e| e.to_string())
}

fn read_u64(r: &mut impl Read) -> Result<u64, String> {
    let mut b = [0u8; 8];
    r.read_exact(&mut b).map_err(|e| e.to_string())?;
    Ok(u64::from_le_bytes(b))
}

fn read_bytes(r: &mut impl Read, n: usize) -> Result<Vec<u8>, String> {
    let mut out = vec![0u8; n];
    r.read_exact(&mut out).map_err(|e| e.to_string())?;
    Ok(out)
}

fn type_code(t: ValueType) -> u32 {
    match t {
        ValueType::F64 => 0, ValueType::F32 => 1, ValueType::I64 => 2, ValueType::U64 => 3,
        ValueType::I32 => 4, ValueType::U32 => 5, ValueType::I16 => 6, ValueType::U16 => 7,
        ValueType::I8 => 8, ValueType::U8 => 9, ValueType::Bool => 10, ValueType::Text => 11,
    }
}

fn code_type(c: u32) -> Option<ValueType> {
    Some(match c {
        0 => ValueType::F64, 1 => ValueType::F32, 2 => ValueType::I64, 3 => ValueType::U64,
        4 => ValueType::I32, 5 => ValueType::U32, 6 => ValueType::I16, 7 => ValueType::U16,
        8 => ValueType::I8, 9 => ValueType::U8, 10 => ValueType::Bool, 11 => ValueType::Text,
        _ => return None,
    })
}

fn write_column(w: &mut impl Write, col: &Column) -> Result<(), String> {
    let name = col.name.as_bytes();
    let unit = col.unit.as_bytes();
    write_u32(w, name.len() as u32)?;
    write_u32(w, unit.len() as u32)?;
    write_u32(w, type_code(col.value_type()))?;
    write_u32(w, col.len() as u32)?;
    w.write_all(name).map_err(|e| e.to_string())?;
    w.write_all(unit).map_err(|e| e.to_string())?;
    match &col.data {
        ColumnData::F64(v) => for x in v { w.write_all(&x.to_le_bytes()).map_err(|e| e.to_string())?; },
        ColumnData::F32(v) => for x in v { w.write_all(&x.to_le_bytes()).map_err(|e| e.to_string())?; },
        ColumnData::I64(v) => for x in v { w.write_all(&x.to_le_bytes()).map_err(|e| e.to_string())?; },
        ColumnData::U64(v) => for x in v { w.write_all(&x.to_le_bytes()).map_err(|e| e.to_string())?; },
        ColumnData::I32(v) => for x in v { w.write_all(&x.to_le_bytes()).map_err(|e| e.to_string())?; },
        ColumnData::U32(v) => for x in v { w.write_all(&x.to_le_bytes()).map_err(|e| e.to_string())?; },
        ColumnData::I16(v) => for x in v { w.write_all(&x.to_le_bytes()).map_err(|e| e.to_string())?; },
        ColumnData::U16(v) => for x in v { w.write_all(&x.to_le_bytes()).map_err(|e| e.to_string())?; },
        ColumnData::I8(v) => for x in v { w.write_all(&x.to_le_bytes()).map_err(|e| e.to_string())?; },
        ColumnData::U8(v) => for x in v { w.write_all(&x.to_le_bytes()).map_err(|e| e.to_string())?; },
        ColumnData::Bool(v) => for x in v { w.write_all(if *x { &[1u8] } else { &[0u8] }).map_err(|e| e.to_string())?; },
        ColumnData::Text(v) => for x in v {
            let bytes = x.as_bytes();
            write_u32(w, bytes.len() as u32)?;
            w.write_all(bytes).map_err(|e| e.to_string())?;
        },
    }
    let bits = col.len().div_ceil(8);
    w.write_all(&col.nulls.raw()[..bits]).map_err(|e| e.to_string())
}

fn read_column(r: &mut impl Read) -> Result<Column, String> {
    let name_len = read_u32(r)? as usize;
    let unit_len = read_u32(r)? as usize;
    let vt = code_type(read_u32(r)?).ok_or("未知列类型")?;
    let n = read_u32(r)? as usize;
    let name = String::from_utf8(read_bytes(r, name_len)?).map_err(|e| e.to_string())?;
    let unit = String::from_utf8(read_bytes(r, unit_len)?).map_err(|e| e.to_string())?;
    let data = match vt {
        ValueType::F64 => ColumnData::F64(read_f64s(r, n)?),
        ValueType::F32 => { let mut v = Vec::with_capacity(n); for _ in 0..n { let mut b=[0u8;4]; r.read_exact(&mut b).map_err(|e| e.to_string())?; v.push(f32::from_le_bytes(b)); } ColumnData::F32(v) }
        ValueType::I64 => { let mut v = Vec::with_capacity(n); for _ in 0..n { v.push(read_i64(r)?); } ColumnData::I64(v) }
        ValueType::U64 => { let mut v = Vec::with_capacity(n); for _ in 0..n { v.push(read_u64(r)?); } ColumnData::U64(v) }
        ValueType::I32 => { let mut v = Vec::with_capacity(n); for _ in 0..n { let mut b=[0u8;4]; r.read_exact(&mut b).map_err(|e| e.to_string())?; v.push(i32::from_le_bytes(b)); } ColumnData::I32(v) }
        ValueType::U32 => { let mut v = Vec::with_capacity(n); for _ in 0..n { let mut b=[0u8;4]; r.read_exact(&mut b).map_err(|e| e.to_string())?; v.push(u32::from_le_bytes(b)); } ColumnData::U32(v) }
        ValueType::I16 => { let mut v = Vec::with_capacity(n); for _ in 0..n { let mut b=[0u8;2]; r.read_exact(&mut b).map_err(|e| e.to_string())?; v.push(i16::from_le_bytes(b)); } ColumnData::I16(v) }
        ValueType::U16 => { let mut v = Vec::with_capacity(n); for _ in 0..n { let mut b=[0u8;2]; r.read_exact(&mut b).map_err(|e| e.to_string())?; v.push(u16::from_le_bytes(b)); } ColumnData::U16(v) }
        ValueType::I8 => { let mut v = Vec::with_capacity(n); for _ in 0..n { let mut b=[0u8;1]; r.read_exact(&mut b).map_err(|e| e.to_string())?; v.push(i8::from_le_bytes(b)); } ColumnData::I8(v) }
        ValueType::U8 => ColumnData::U8(read_bytes(r, n)?),
        ValueType::Bool => { let b = read_bytes(r, n)?; ColumnData::Bool(b.into_iter().map(|x| x != 0).collect()) }
        ValueType::Text => { let mut v = Vec::with_capacity(n); for _ in 0..n { let len = read_u32(r)? as usize; v.push(String::from_utf8(read_bytes(r, len)?).map_err(|e| e.to_string())?); } ColumnData::Text(v) }
    };
    let bits = read_bytes(r, n.div_ceil(8))?;
    Column::with_nulls(name, unit, data, NullBits::from_raw(&bits, n))
}

pub fn save_typed_dataset(path: &Path, ds: &TypedDataset, source_size: i64, source_mtime: i64) -> Result<(), String> {
    if ds.tables.len() > MAX_CHANNELS as usize {
        return Err("表数超过缓存上限".into());
    }
    let tmp = temp_path(path);
    {
        let f = File::create(&tmp).map_err(|e| e.to_string())?;
        let mut w = BufWriter::new(f);
        write_u32(&mut w, TYPED_MAGIC)?;
        write_u32(&mut w, TYPED_VERSION)?;
        write_i64(&mut w, source_size)?;
        write_i64(&mut w, source_mtime)?;
        write_u32(&mut w, ds.tables.len() as u32)?;
        for table in &ds.tables {
            let name = table.name.as_bytes();
            write_u32(&mut w, name.len() as u32)?;
            write_u32(&mut w, table.row_count() as u32)?;
            write_u32(&mut w, table.columns.len() as u32)?;
            w.write_all(name).map_err(|e| e.to_string())?;
            for t in table.time_us.iter() { write_i64(&mut w, *t)?; }
            for s in &table.seq { write_u64(&mut w, *s)?; }
            for col in &table.columns { write_column(&mut w, col)?; }
        }
        w.flush().map_err(|e| e.to_string())?;
    }
    fs::rename(&tmp, path).map_err(|e| {
        let _ = fs::remove_file(&tmp);
        e.to_string()
    })
}

pub fn load_typed_dataset(path: &Path, expected_size: i64, expected_mtime: i64) -> Result<Option<TypedDataset>, String> {
    if !path.is_file() {
        return Ok(None);
    }
    let f = File::open(path).map_err(|e| e.to_string())?;
    let mut r = BufReader::new(f);
    if read_u32(&mut r)? != TYPED_MAGIC {
        return Ok(None);
    }
    if read_u32(&mut r)? != TYPED_VERSION {
        return Ok(None);
    }
    if read_i64(&mut r)? != expected_size || read_i64(&mut r)? != expected_mtime {
        return Ok(None);
    }
    let table_count = read_u32(&mut r)?;
    if table_count > MAX_CHANNELS {
        return Err("表数超过缓存上限".into());
    }
    let mut ds = TypedDataset::new();
    for _ in 0..table_count {
        let name_len = read_u32(&mut r)? as usize;
        let rows = read_u32(&mut r)? as usize;
        let cols = read_u32(&mut r)?;
        let name = String::from_utf8(read_bytes(&mut r, name_len)?).map_err(|e| e.to_string())?;
        if cols > MAX_COLUMNS {
            return Err("列数超过缓存上限".into());
        }
        let mut time_us = Vec::with_capacity(rows);
        for _ in 0..rows { time_us.push(read_i64(&mut r)?); }
        let mut seqs = Vec::with_capacity(rows);
        for _ in 0..rows { seqs.push(read_u64(&mut r)?); }
        let mut columns = Vec::with_capacity(cols as usize);
        for _ in 0..cols { columns.push(read_column(&mut r)?); }
        let idx = ds.add_table_with_columns(&name, time_us, columns).map_err(|e| e.to_string())?;
        if let Some(table) = ds.table_mut(idx) { table.seq = seqs; }
    }
    Ok(Some(ds))
}

#[cfg(test)]
mod typed_tests {
    use super::*;

    #[test]
    fn typed_save_and_load_roundtrip() {
        let dir = std::env::temp_dir().join(format!("ps_cache_typed_{}", std::process::id()));
        let _ = fs::create_dir_all(&dir);
        let path = dir.join("x.pscache");
        let mut ds = TypedDataset::new();
        ds.add_table("imu", vec![100, 200]).unwrap();
        let idx = ds.table_index("imu").unwrap();
        ds.add_column(idx, Column::new("roll", "deg", ColumnData::F32(vec![1.0, 2.0])).unwrap()).unwrap();
        let mut nulls = NullBits::new(2);
        nulls.set_null(1, true);
        ds.add_column(idx, Column::with_nulls("flag", "", ColumnData::I32(vec![7, 0]), nulls).unwrap()).unwrap();
        save_typed_dataset(&path, &ds, 12, 34).unwrap();
        let hit = load_typed_dataset(&path, 12, 34).unwrap().unwrap();
        assert_eq!(hit.tables.len(), 1);
        let t = &hit.tables[0];
        assert_eq!(t.time_us.as_slice(), &[100, 200]);
        assert_eq!(t.columns.len(), 2);
        assert_eq!(t.columns[0].value_as_f64(0), Some(1.0));
        assert!(t.columns[1].is_null(1));
        assert!(load_typed_dataset(&path, 13, 34).unwrap().is_none());
        let _ = fs::remove_dir_all(&dir);
    }
}
