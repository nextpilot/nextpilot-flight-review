//! 数据集二进制缓存：只接收不透明路径与源文件指纹，不认 loader / 选项。
//! 缓存 key 与失效策略仍在 app；这里只负责“把 Dataset 原子写盘、校验后读回”。

use ps_ndf::{Dataset, Series};
use std::fs::{self, File};
use std::io::{BufReader, BufWriter, Read, Write};
use std::path::{Path, PathBuf};

const MAGIC: u32 = 0x5053_4331; // PSC1
const VERSION: u32 = 1;
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
            for x in &ch.t {
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
            t,
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
        let _ = fs::remove_dir_all(&dir);
    }
}
