//! NDF（NextPilot Data Format）：按表组织的时间序列内存表示。
//! 逻辑上 Dataset 是库、Table 是 topic 表、Column 是字段列；物理上 t 与 v 分列存储。

pub mod object_index;
pub mod object_read;
pub mod segments;
pub mod typed;

use std::sync::Arc;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Precision {
    F32,
    F64,
}

/// 一条通道的原始样本。`t` 指向所属 topic 表的时间轴，同一表的所有列共享一份。
/// `v` 存 f64 便于统一算子。
#[derive(Debug, Clone)]
pub struct Series {
    pub name: String,
    pub unit: String,
    pub t: Arc<Vec<f64>>,
    pub v: Vec<f64>,
}

impl Default for Series {
    fn default() -> Self {
        Self { name: String::new(), unit: String::new(), t: Arc::new(Vec::new()), v: Vec::new() }
    }
}

impl Series {
    pub fn new(name: impl Into<String>) -> Self {
        Self { name: name.into(), ..Default::default() }
    }

    pub fn push(&mut self, t: f64, v: f64) {
        Arc::make_mut(&mut self.t).push(t);
        self.v.push(v);
    }

    pub fn time(&self) -> &[f64] {
        self.t.as_slice()
    }

    pub fn len(&self) -> usize {
        self.t.len()
    }

    pub fn is_empty(&self) -> bool {
        self.t.is_empty()
    }

    /// 返回 (min, max)；空序列返回 None。
    pub fn min_max(&self) -> Option<(f64, f64)> {
        let mut iter = self.v.iter().copied().filter(|x| x.is_finite());
        let first = iter.next()?;
        let mut lo = first;
        let mut hi = first;
        for x in iter {
            if x < lo {
                lo = x;
            }
            if x > hi {
                hi = x;
            }
        }
        Some((lo, hi))
    }

    /// 抽稀到每 `bucket` 个样本一个点（min/max 保形），M0 先做等距抽稀。
    pub fn downsample(&self, bucket: usize) -> Vec<(f64, f64)> {
        let bucket = bucket.max(1);
        if bucket == 1 {
            return self.t.iter().copied().zip(self.v.iter().copied()).collect();
        }
        self.t
            .chunks(bucket)
            .zip(self.v.chunks(bucket))
            .map(|(ts, vs)| {
                let t = ts[0];
                let mut lo = f64::INFINITY;
                let mut hi = f64::NEG_INFINITY;
                for v in vs.iter().copied().filter(|x| x.is_finite()) {
                    lo = lo.min(v);
                    hi = hi.max(v);
                }
                (t, if lo.is_finite() { (lo + hi) / 2.0 } else { f64::NAN })
            })
            .collect()
    }
}

/// 一张 topic 表：一份时间轴 + 多列。Dataset 相当于库/schema。
#[derive(Debug, Clone)]
pub struct Table {
    pub name: String,
    pub t: Arc<Vec<f64>>,
    pub channels: Vec<usize>,
}

/// 数据集：一组表；`channels` 是扁平视图，供现有 C ABI/绘图使用。
#[derive(Debug, Clone, Default)]
pub struct Dataset {
    pub channels: Vec<Series>,
    pub tables: Vec<Table>,
}

impl Dataset {
    pub fn new() -> Self {
        Self { channels: Vec::new(), tables: Vec::new() }
    }

    pub fn len(&self) -> usize {
        self.channels.len()
    }

    pub fn is_empty(&self) -> bool {
        self.channels.is_empty()
    }

    pub fn channel(&self, index: usize) -> Option<&Series> {
        self.channels.get(index)
    }

    /// 按名字找通道；返回索引与通道引用。
    pub fn find(&self, name: &str) -> Option<(usize, &Series)> {
        self.channels.iter().enumerate().find(|(_, c)| c.name == name)
    }

    /// 新建一张 topic 表；时间轴只复制一次。
    pub fn add_table(&mut self, name: impl Into<String>, t: Vec<f64>) -> Result<usize, String> {
        let name = name.into();
        if name.is_empty() {
            return Err("表名为空".into());
        }
        if self.tables.iter().any(|x| x.name == name) {
            return Err(format!("表已存在：{name}"));
        }
        self.tables.push(Table { name, t: Arc::new(t), channels: Vec::new() });
        Ok(self.tables.len() - 1)
    }

    /// 往指定表追加一列；同表所有列共享 table.t。
    pub fn add_channel_to_table(
        &mut self,
        table: usize,
        name: impl Into<String>,
        unit: impl Into<String>,
        v: Vec<f64>,
    ) -> Result<(), String> {
        let name = name.into();
        if name.is_empty() {
            return Err("输出通道名为空".into());
        }
        if table >= self.tables.len() {
            return Err("表不存在".into());
        }
        if self.channels.iter().any(|c| c.name == name) {
            return Err(format!("通道已存在：{name}"));
        }
        let t = self.tables[table].t.clone();
        if v.len() != t.len() {
            return Err(format!("通道点数与表时间轴不一致：{name}"));
        }
        let idx = self.channels.len();
        self.channels.push(Series { name, unit: unit.into(), t, v });
        self.tables[table].channels.push(idx);
        Ok(())
    }

    /// 追加独立通道；名字重复或为空返回错误。
    pub fn add_channel(&mut self, series: Series) -> Result<(), String> {
        if series.name.is_empty() {
            return Err("输出通道名为空".into());
        }
        if self.channels.iter().any(|c| c.name == series.name) {
            return Err(format!("通道已存在：{}", series.name));
        }
        self.channels.push(series);
        Ok(())
    }

    /// 删除通道；返回是否删除成功。
    pub fn remove_channel(&mut self, name: &str) -> bool {
        let Some(idx) = self.channels.iter().position(|c| c.name == name) else { return false; };
        self.channels.remove(idx);
        for table in &mut self.tables {
            table.channels.retain(|&i| i != idx);
            for i in &mut table.channels {
                if *i > idx {
                    *i -= 1;
                }
            }
        }
        true
    }
}

/// 视口 LOD：把 [t0,t1] 内样本按像素分桶，每桶输出 (中点时间, min, max)。
/// 降采样必须在 Rust 侧完成（§11 铁律 3），UI 只拿 ≤px 个三元组去画。
pub fn lod_minmax(t: &[f64], v: &[f64], t0: f64, t1: f64, px: usize) -> Vec<(f64, f64, f64)> {
    let mut out = vec![0.0f64; px.max(1) * 3];
    let n = lod_minmax_into(t, v, t0, t1, px, &mut out);
    out[..n * 3]
        .chunks_exact(3)
        .map(|c| (c[0], c[1], c[2]))
        .collect()
}

/// 视口 LOD 的零分配版本：把 `(x,min,max)` 写进调用方缓冲区，返回写入的三元组数。
/// 一次批量调用可以并行处理多个通道，避免每通道一次 FFI + 一次 Vec 分配。
pub fn lod_minmax_into(
    t: &[f64],
    v: &[f64],
    t0: f64,
    t1: f64,
    px: usize,
    out: &mut [f64],
) -> usize {
    let cap = out.len() / 3;
    if cap == 0 || t.is_empty() || v.is_empty() || !(t1 > t0) {
        return 0;
    }
    let px = px.max(1).min(cap);
    let n = t.len().min(v.len());
    let start = t.partition_point(|x| *x < t0);
    let span = t1 - t0;

    let mut count = 0usize;
    let mut cur_bucket = usize::MAX;
    for i in start..n {
        let ti = t[i];
        if ti > t1 {
            break;
        }
        let b = (((ti - t0) / span) * px as f64).floor();
        let b = if b < 0.0 { 0 } else { (b as usize).min(px - 1) };
        let val = v[i];
        if b != cur_bucket {
            // 只有“新桶且已经写满”才停；已写满的最后一个桶还要继续收 min/max。
            if count >= cap {
                break;
            }
            cur_bucket = b;
            let base = count * 3;
            out[base] = t0 + span * (b as f64 + 0.5) / px as f64;
            out[base + 1] = val;
            out[base + 2] = val;
            count += 1;
        } else if count > 0 {
            let base = (count - 1) * 3;
            if val.is_finite() {
                out[base + 1] = out[base + 1].min(val);
                out[base + 2] = out[base + 2].max(val);
            }
        }
    }
    count
}

/// 通道精度推断：用于 D11（f32/f64/kEnum/kString）的最小实现。
pub fn precision_of(v: &[f64]) -> Precision {
    if v.iter().all(|x| (*x as f32).is_finite() && (*x as f32) as f64 == *x) {
        Precision::F32
    } else {
        Precision::F64
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn push_and_range() {
        let mut s = Series::new("att.roll");
        s.push(0.0, 1.5);
        s.push(0.1, -2.0);
        assert_eq!(s.len(), 2);
        assert_eq!(s.min_max(), Some((-2.0, 1.5)));
    }

    #[test]
    fn empty_range_is_none() {
        assert!(Series::new("x").min_max().is_none());
    }

    #[test]
    fn lod_buckets_keep_extremes() {
        let mut s = Series::new("x");
        for i in 0..100 {
            let t = i as f64 * 0.1;
            s.push(t, if i % 2 == 0 { -1.0 } else { 1.0 });
        }
        let lod = lod_minmax(&s.t, &s.v, 0.0, 10.0, 10);
        assert_eq!(lod.len(), 10);
        for (_, lo, hi) in &lod {
            assert_eq!(*lo, -1.0);
            assert_eq!(*hi, 1.0);
        }
    }

    #[test]
    fn lod_into_reuses_caller_buffer() {
        let t = [0.0, 1.0, 2.0];
        let v = [0.0, 3.0, -1.0];
        let mut out = [0.0; 9];
        let n = lod_minmax_into(&t, &v, 0.0, 2.0, 3, &mut out);
        assert_eq!(n, 3);
        assert_eq!(out[1], 0.0);
        assert_eq!(out[7], -1.0);
    }

    #[test]
    fn lod_empty_when_range_invalid() {
        assert!(lod_minmax(&[0.0], &[1.0], 1.0, 0.0, 10).is_empty());
    }

    #[test]
    fn downsample_keeps_bucket_midpoint() {
        let mut s = Series::new("x");
        for i in 0..8 {
            s.push(i as f64, if i % 2 == 0 { 0.0 } else { 10.0 });
        }
        let p = s.downsample(4);
        assert_eq!(p.len(), 2);
        assert!((p[0].1 - 5.0).abs() < 1e-9);
    }

    #[test]
    fn dataset_adds_and_finds_channels() {
        let mut ds = Dataset::new();
        let mut s = Series::new("att.roll");
        s.push(0.0, 1.0);
        ds.add_channel(s).unwrap();
        assert_eq!(ds.len(), 1);
        assert_eq!(ds.find("att.roll").unwrap().0, 0);
        assert!(ds.add_channel(Series::new("att.roll")).is_err());
        assert!(ds.remove_channel("att.roll"));
        assert!(ds.is_empty());
    }

    #[test]
    fn table_channels_share_axis() {
        let mut ds = Dataset::new();
        let table = ds.add_table("att", vec![0.0, 1.0, 2.0]).unwrap();
        ds.add_channel_to_table(table, "att.roll", "deg", vec![1.0, 2.0, 3.0]).unwrap();
        ds.add_channel_to_table(table, "att.pitch", "deg", vec![4.0, 5.0, 6.0]).unwrap();
        assert!(ds.add_channel_to_table(table, "att.yaw", "deg", vec![1.0]).is_err());
        assert!(Arc::ptr_eq(&ds.channels[0].t, &ds.channels[1].t));
        assert_eq!(ds.tables[0].channels, vec![0, 1]);
        assert!(ds.remove_channel("att.roll"));
        assert_eq!(ds.channels[0].name, "att.pitch");
        assert_eq!(ds.tables[0].channels, vec![0]);
    }
}
