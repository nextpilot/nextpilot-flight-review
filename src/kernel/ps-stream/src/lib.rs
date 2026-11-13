//! 实时流机制：固定容量、按通道追加、通道视图、视口 LOD。
//! app 只负责启停与 UI；缓冲和抽稀由这里持有，避免 Qt 侧再维护一份环形数组。

use ps_ndf::{lod_minmax_into, Series};
use std::ffi::CString;
use std::os::raw::c_char;

#[repr(C)]
pub struct StreamChannel {
    pub name: *const c_char,
    pub unit: *const c_char,
    pub point_count: u32,
    pub time: *const f64,
    pub value: *const f64,
}

struct Channel {
    series: Series,
    name_c: CString,
    unit_c: CString,
    view: StreamChannel,
}

impl Channel {
    fn new(name: &str, unit: &str, capacity: usize) -> Self {
        let name_c = CString::new(name).unwrap_or_default();
        let unit_c = CString::new(unit).unwrap_or_default();
        let mut series = Series::new(name);
        series.unit = unit.to_string();
        series.t = Vec::with_capacity(capacity);
        series.v = Vec::with_capacity(capacity);
        let mut ch = Self {
            series,
            name_c,
            unit_c,
            view: StreamChannel {
                name: std::ptr::null(),
                unit: std::ptr::null(),
                point_count: 0,
                time: std::ptr::null(),
                value: std::ptr::null(),
            },
        };
        ch.refresh();
        ch
    }

    fn refresh(&mut self) {
        self.view = StreamChannel {
            name: self.name_c.as_ptr(),
            unit: self.unit_c.as_ptr(),
            point_count: self.series.t.len() as u32,
            time: self.series.t.as_ptr(),
            value: self.series.v.as_ptr(),
        };
    }
}

pub struct Stream {
    capacity: usize,
    channels: Vec<Channel>,
}

impl Stream {
    pub fn new(capacity: usize) -> Self {
        Self { capacity: capacity.max(1), channels: Vec::new() }
    }

    pub fn capacity(&self) -> usize {
        self.capacity
    }

    pub fn channel_count(&self) -> usize {
        self.channels.len()
    }

    /// 追加一批样本；返回 0 表示全收，1 表示容量不足只收了前一段。
    pub fn push(&mut self, name: &str, unit: &str, t: &[f64], v: &[f64]) -> i32 {
        if name.is_empty() {
            return 2;
        }
        let n = t.len().min(v.len());
        if n == 0 {
            return 0;
        }
        let idx = match self.channels.iter().position(|c| c.series.name == name) {
            Some(i) => i,
            None => {
                self.channels.push(Channel::new(name, unit, self.capacity));
                self.channels.len() - 1
            }
        };
        let ch = &mut self.channels[idx];
        let room = self.capacity.saturating_sub(ch.series.t.len());
        let take = n.min(room);
        for i in 0..take {
            ch.series.t.push(t[i]);
            ch.series.v.push(v[i]);
        }
        ch.refresh();
        if take < n { 1 } else { 0 }
    }

    pub fn channel(&self, index: usize) -> Option<&StreamChannel> {
        self.channels.get(index).map(|c| &c.view)
    }

    pub fn channel_by_name(&self, name: &str) -> Option<&Channel> {
        self.channels.iter().find(|c| c.series.name == name)
    }

    pub fn lod_into(
        &self,
        name: &str,
        t0: f64,
        t1: f64,
        px: usize,
        out: &mut [f64],
    ) -> usize {
        match self.channel_by_name(name) {
            Some(c) => lod_minmax_into(&c.series.t, &c.series.v, t0, t1, px, out),
            None => 0,
        }
    }

    pub fn total_points(&self) -> usize {
        self.channels.iter().map(|c| c.series.t.len()).sum()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn append_view_and_lod() {
        let mut s = Stream::new(16);
        assert_eq!(s.push("roll", "deg", &[0.0, 1.0], &[1.0, 3.0]), 0);
        assert_eq!(s.channel_count(), 1);
        let ch = s.channel(0).unwrap();
        assert_eq!(ch.point_count, 2);
        let mut out = vec![0.0; 3 * 4];
        assert!(s.lod_into("roll", 0.0, 2.0, 4, &mut out) > 0);
        assert_eq!(s.push("roll", "deg", &[2.0; 20], &[0.0; 20]), 1);
        assert_eq!(s.channel(0).unwrap().point_count, 16);
    }
}
