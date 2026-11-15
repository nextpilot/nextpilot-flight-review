//! 实时流机制：固定容量环形缓冲、通道视图、视口 LOD。
//! 环形缓冲负责“只留最近 N 点”，视图负责给 C/UI 一段连续内存；两者都由 kernel 持有。

use ps_ndf::lod_minmax_into;
use std::collections::VecDeque;
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
    ring_t: VecDeque<f64>,
    ring_v: VecDeque<f64>,
    view_t: Vec<f64>,
    view_v: Vec<f64>,
    name_c: CString,
    unit_c: CString,
    view: StreamChannel,
}

impl Channel {
    fn new(name: &str, unit: &str, capacity: usize) -> Self {
        let name_c = CString::new(name).unwrap_or_default();
        let unit_c = CString::new(unit).unwrap_or_default();
        let mut ch = Self {
            ring_t: VecDeque::with_capacity(capacity),
            ring_v: VecDeque::with_capacity(capacity),
            view_t: Vec::with_capacity(capacity),
            view_v: Vec::with_capacity(capacity),
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
        self.view_t.clear();
        self.view_v.clear();
        self.view_t.extend(self.ring_t.iter().copied());
        self.view_v.extend(self.ring_v.iter().copied());
        self.view = StreamChannel {
            name: self.name_c.as_ptr(),
            unit: self.unit_c.as_ptr(),
            point_count: self.view_t.len() as u32,
            time: self.view_t.as_ptr(),
            value: self.view_v.as_ptr(),
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

    /// 追加一批样本；返回 0 表示全收，1 表示环形缓冲已丢弃最旧数据。
    pub fn push(&mut self, name: &str, unit: &str, t: &[f64], v: &[f64]) -> i32 {
        if name.is_empty() {
            return 2;
        }
        let n = t.len().min(v.len());
        if n == 0 {
            return 0;
        }
        let idx = match self.channels.iter().position(|c| c.name_c.to_bytes() == name.as_bytes()) {
            Some(i) => i,
            None => {
                self.channels.push(Channel::new(name, unit, self.capacity));
                self.channels.len() - 1
            }
        };
        let ch = &mut self.channels[idx];
        let mut dropped = n > self.capacity;
        let start = n.saturating_sub(self.capacity);
        for i in start..n {
            if ch.ring_t.len() == self.capacity {
                ch.ring_t.pop_front();
                ch.ring_v.pop_front();
                dropped = true;
            }
            ch.ring_t.push_back(t[i]);
            ch.ring_v.push_back(v[i]);
        }
        if dropped { 1 } else { 0 }
    }

    /// 刷新并返回连续视图；视图指针在下一次 refresh/释放后失效。
    pub fn channel(&mut self, index: usize) -> Option<&StreamChannel> {
        let ch = self.channels.get_mut(index)?;
        ch.refresh();
        Some(&ch.view)
    }

    fn refresh_by_name(&mut self, name: &str) -> Option<usize> {
        let idx = self.channels.iter().position(|c| c.name_c.to_bytes() == name.as_bytes())?;
        self.channels[idx].refresh();
        Some(idx)
    }

    pub fn lod_into(&mut self, name: &str, t0: f64, t1: f64, px: usize, out: &mut [f64]) -> usize {
        match self.refresh_by_name(name) {
            Some(i) => {
                let ch = &self.channels[i];
                lod_minmax_into(&ch.view_t, &ch.view_v, t0, t1, px, out)
            }
            None => 0,
        }
    }

    pub fn total_points(&self) -> usize {
        self.channels.iter().map(|c| c.ring_t.len()).sum()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn append_view_and_ring_wrap() {
        let mut s = Stream::new(3);
        assert_eq!(s.push("roll", "deg", &[0.0, 1.0], &[1.0, 3.0]), 0);
        assert_eq!(s.channel_count(), 1);
        let ch = s.channel(0).unwrap();
        assert_eq!(ch.point_count, 2);
        assert!(s.lod_into("roll", 0.0, 2.0, 4, &mut vec![0.0; 12]) > 0);

        assert_eq!(s.push("roll", "deg", &[2.0, 3.0, 4.0], &[5.0, 7.0, 9.0]), 1);
        let ch = s.channel(0).unwrap();
        assert_eq!(ch.point_count, 3);
        unsafe {
            assert_eq!(*ch.time.add(0), 2.0);
            assert_eq!(*ch.time.add(2), 4.0);
        }
    }
}
