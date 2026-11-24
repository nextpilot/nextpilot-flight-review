//! 输入 retention gap 检测（BJ6 / 借鉴 pj_datastore 的 chunk id 水位）。
//!
//! pj 的经验（ARCHITECTURE:204-208）：feed 的时间戳只在非空行推进，若用时间戳
//! 判断「落后」，全 null / sparse 输入会被误判成永久 gap。正确做法是给每段一个
//! 单调 id，**只比较 id 是否跳号**；时间只用来标注缺口的大致位置。
//!
//! 本模块只做「记水位 + 判跳号 + 有界缓存」，不参与求值，也不改 ring 语义。
//! 缺口事实由真正知道段/驱逐的一侧上报（`observe_segment` 自动检测，
//! 或 `report_gap` 显式声明），因此不存在「凭空推断」造成的误判。

use std::collections::HashMap;

/// 一处输入缺口：某通道上段 id `[from_id, to_id]` 未到达即被丢弃。
///
/// `t_lo` / `t_hi` 是缺口两侧已知时间的边界（上一个已见段的上界、下一个已见段
/// 的下界），**仅作定位提示**。判定缺口存在与否只看段 id 跳号，不看这两个值——
/// 所以时间不推进（sparse / 全 null）也不会被误判成缺口。
#[derive(Debug, Clone, PartialEq)]
pub struct Gap {
    pub channel: String,
    /// 缺失区间起点（含）。
    pub from_id: u64,
    /// 缺失区间终点（含）。
    pub to_id: u64,
    pub t_lo: f64,
    pub t_hi: f64,
}

impl Gap {
    /// 缺失的段数。
    pub fn missing(&self) -> u64 {
        self.to_id - self.from_id + 1
    }
}

/// 默认保留条数。缺口是低频事件，64 条足够 UI 展示，内存可忽略。
pub const DEFAULT_GAP_CAPACITY: usize = 64;

/// 每通道的段 id 水位 + 有界缺口列表。
#[derive(Debug, Clone)]
pub struct GapTracker {
    /// 各通道已见的最大段 id（到达序，不是时间序）。
    last_seen: HashMap<String, u64>,
    /// 各通道最近一个已见段的时间上界，用于标注下一处缺口的时间起点。
    prev_t_hi: HashMap<String, f64>,
    /// 最近缺口，按检测顺序；长度不超过 `cap`。
    gaps: Vec<Gap>,
    /// 保留上限；0 表示不保留记录（仍累计 `total`）。
    cap: usize,
    /// 累计检测到的缺口数，不受 `cap` 影响。
    total: u64,
    /// 因 `cap` 被挤掉的条数。
    dropped: u64,
}

impl Default for GapTracker {
    fn default() -> Self {
        Self::new()
    }
}

impl GapTracker {
    pub fn new() -> Self {
        Self::with_capacity(DEFAULT_GAP_CAPACITY)
    }

    pub fn with_capacity(cap: usize) -> Self {
        Self {
            last_seen: HashMap::new(),
            prev_t_hi: HashMap::new(),
            gaps: Vec::new(),
            cap,
            total: 0,
            dropped: 0,
        }
    }

    /// 上报「收到了一个段」。段 id 跳号即判定缺口，返回新记录的 `Gap`。
    ///
    /// - 段 id 连续（`prev + 1`）：无缺口。
    /// - 段 id 跳号（`> prev + 1`）：记录 `[prev+1, seg_id-1]`。
    /// - 段 id 不大于水位（迟到 / 重复上报）：忽略，不推进水位、不产生缺口。
    pub fn observe_segment(
        &mut self,
        channel: &str,
        seg_id: u64,
        t_lo: f64,
        t_hi: f64,
    ) -> Option<Gap> {
        let Some(prev_id) = self.last_seen.get(channel).copied() else {
            // 首次见到该通道：建立水位，不存在缺口。
            self.last_seen.insert(channel.to_string(), seg_id);
            self.prev_t_hi.insert(channel.to_string(), t_hi);
            return None;
        };
        if seg_id <= prev_id {
            // 迟到或重复的旧段。推进水位会掩盖真实缺口，故原样返回。
            return None;
        }
        let produced = if seg_id > prev_id.saturating_add(1) {
            let lo = self.prev_t_hi.get(channel).copied().unwrap_or(t_lo);
            Some(self.push(Gap {
                channel: channel.to_string(),
                from_id: prev_id + 1,
                to_id: seg_id - 1,
                t_lo: lo,
                t_hi: t_lo,
            }))
        } else {
            None
        };
        self.last_seen.insert(channel.to_string(), seg_id);
        self.prev_t_hi.insert(channel.to_string(), t_hi);
        produced
    }

    /// 显式上报一处已知缺口：`channel` 上段 id `[from_id, to_id]` 被丢弃。
    ///
    /// 由知道丢弃事实的一侧调用（如数据层驱逐、导入跳段）。若该区间已在水位
    /// 之内（`from_id <= last_seen`）视作重复上报并忽略，返回 `None`。
    /// 否则记录并推进水位到 `to_id`。
    pub fn report_gap(
        &mut self,
        channel: &str,
        from_id: u64,
        to_id: u64,
        t_lo: f64,
        t_hi: f64,
    ) -> Option<Gap> {
        if to_id < from_id {
            return None;
        }
        let cur = self.last_seen.get(channel).copied().unwrap_or(0);
        if from_id <= cur {
            return None;
        }
        self.last_seen.insert(channel.to_string(), to_id);
        if t_hi.is_finite() {
            self.prev_t_hi.insert(channel.to_string(), t_hi);
        }
        Some(self.push(Gap {
            channel: channel.to_string(),
            from_id,
            to_id,
            t_lo,
            t_hi,
        }))
    }

    fn push(&mut self, gap: Gap) -> Gap {
        self.total = self.total.saturating_add(1);
        if self.cap == 0 {
            self.dropped = self.dropped.saturating_add(1);
            return gap;
        }
        self.gaps.push(gap.clone());
        // 超出即从头部裁掉最旧的，只保留最近 cap 条。
        if self.gaps.len() > self.cap {
            let excess = self.gaps.len() - self.cap;
            self.gaps.drain(0..excess);
            self.dropped = self.dropped.saturating_add(excess as u64);
        }
        gap
    }

    /// 最近缺口（按检测顺序，最旧在前），长度 ≤ `cap`。
    pub fn gaps(&self) -> &[Gap] {
        &self.gaps
    }

    /// 当前保留条数。
    pub fn len(&self) -> usize {
        self.gaps.len()
    }

    pub fn is_empty(&self) -> bool {
        self.gaps.is_empty()
    }

    /// 累计检测到的缺口数（含已被挤出保留窗口的）。
    pub fn total(&self) -> u64 {
        self.total
    }

    /// 因容量上限被挤掉的条数。
    pub fn dropped(&self) -> u64 {
        self.dropped
    }

    pub fn capacity(&self) -> usize {
        self.cap
    }

    /// 调整保留上限；缩小时立刻裁掉最旧的。
    pub fn set_capacity(&mut self, cap: usize) {
        self.cap = cap;
        if self.gaps.len() > cap {
            let excess = self.gaps.len() - cap;
            self.gaps.drain(0..excess);
            self.dropped = self.dropped.saturating_add(excess as u64);
        }
    }

    /// 某通道当前水位（已见最大段 id）。
    pub fn last_seen(&self, channel: &str) -> Option<u64> {
        self.last_seen.get(channel).copied()
    }

    /// 清空水位与记录（累计计数一并归零）。
    pub fn clear(&mut self) {
        self.last_seen.clear();
        self.prev_t_hi.clear();
        self.gaps.clear();
        self.total = 0;
        self.dropped = 0;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn contiguous_segments_produce_no_gap() {
        let mut t = GapTracker::new();
        for id in 1..=5 {
            assert!(t.observe_segment("a", id, id as f64, id as f64 + 0.1).is_none());
        }
        assert!(t.gaps().is_empty());
        assert_eq!(t.total(), 0);
        assert_eq!(t.last_seen("a"), Some(5));
    }

    #[test]
    fn sparse_time_without_id_jump_is_not_gap() {
        // pj 的核心坑：时间大幅跳跃但段 id 连续，不得判为缺口。
        let mut t = GapTracker::new();
        t.observe_segment("a", 1, 0.0, 1.0);
        t.observe_segment("a", 2, 1_000_000.0, 1_000_001.0);
        t.observe_segment("a", 3, 9e9, 9e9 + 0.5);
        assert!(t.gaps().is_empty(), "时间稀疏不应产生缺口");
        assert_eq!(t.total(), 0);
    }

    #[test]
    fn all_null_style_flat_time_is_not_gap() {
        // 全 null 输入的极端形态：时间完全不推进（恒为 0），段 id 连续。
        let mut t = GapTracker::new();
        for id in 1..=100 {
            assert!(t.observe_segment("a", id, 0.0, 0.0).is_none());
        }
        assert!(t.gaps().is_empty());
        assert_eq!(t.total(), 0);
    }

    #[test]
    fn id_jump_creates_bounded_gap_with_time_hints() {
        let mut t = GapTracker::new();
        t.observe_segment("a", 1, 0.0, 1.0);
        let g = t.observe_segment("a", 5, 5.0, 6.0).expect("应检测到缺口");
        assert_eq!((g.from_id, g.to_id), (2, 4));
        assert_eq!(g.missing(), 3);
        // 时间提示取上一个已见段的上界与当前段的下界。
        assert_eq!((g.t_lo, g.t_hi), (1.0, 5.0));
        assert_eq!(t.last_seen("a"), Some(5));
    }

    #[test]
    fn late_or_duplicate_segment_does_not_mask_gap() {
        let mut t = GapTracker::new();
        t.observe_segment("a", 1, 0.0, 1.0);
        t.observe_segment("a", 2, 1.0, 2.0);
        // 迟到上报旧段：不推进水位。
        assert!(t.observe_segment("a", 1, 0.0, 1.0).is_none());
        assert_eq!(t.last_seen("a"), Some(2));
        // 之后真跳号仍应检测到。
        let g = t.observe_segment("a", 4, 4.0, 5.0).expect("仍应检测到缺口");
        assert_eq!((g.from_id, g.to_id), (3, 3));
    }

    #[test]
    fn gaps_are_bounded_but_total_keeps_counting() {
        let mut t = GapTracker::with_capacity(4);
        t.observe_segment("a", 1, 0.0, 1.0);
        let mut prev = 1u64;
        for _ in 0..10u64 {
            // 每轮跳过 1 个 id，制造缺口。
            let id = prev + 2;
            t.observe_segment("a", id, id as f64, id as f64);
            prev = id;
        }
        assert_eq!(t.total(), 10);
        assert_eq!(t.len(), 4, "只保留最近 4 条");
        assert_eq!(t.dropped(), 6);
        // 保留的是最近 4 个缺口，id 递增。
        let ids: Vec<u64> = t.gaps().iter().map(|g| g.from_id).collect();
        assert!(ids.windows(2).all(|w| w[0] < w[1]));
        assert_eq!(*ids.last().unwrap(), prev - 1);
    }

    #[test]
    fn capacity_zero_disables_retention_but_keeps_total() {
        let mut t = GapTracker::with_capacity(0);
        t.observe_segment("a", 1, 0.0, 1.0);
        t.observe_segment("a", 3, 3.0, 4.0);
        assert_eq!(t.total(), 1);
        assert!(t.gaps().is_empty());
        assert_eq!(t.dropped(), 1);
    }

    #[test]
    fn shrinking_capacity_trims_immediately() {
        let mut t = GapTracker::with_capacity(8);
        t.observe_segment("a", 1, 0.0, 1.0);
        for id in [3u64, 5, 7, 9, 11] {
            t.observe_segment("a", id, id as f64, id as f64);
        }
        assert_eq!(t.len(), 5);
        t.set_capacity(2);
        assert_eq!(t.len(), 2);
        assert_eq!(t.dropped(), 3);
        assert_eq!(t.total(), 5);
    }

    #[test]
    fn channels_are_independent() {
        let mut t = GapTracker::new();
        t.observe_segment("a", 1, 0.0, 1.0);
        t.observe_segment("b", 1, 0.0, 1.0);
        // a 跳号，b 连续。
        assert!(t.observe_segment("a", 3, 3.0, 4.0).is_some());
        assert!(t.observe_segment("b", 2, 1.0, 2.0).is_none());
        assert_eq!(t.len(), 1);
        assert_eq!(t.gaps()[0].channel, "a");
        assert_eq!(t.last_seen("b"), Some(2));
    }

    #[test]
    fn explicit_report_advances_watermark_and_dedupes() {
        let mut t = GapTracker::new();
        // 首次显式上报：水位从 0 起，[1,3] 有效。
        let g = t.report_gap("a", 1, 3, 0.0, 3.0).expect("应记录");
        assert_eq!((g.from_id, g.to_id), (1, 3));
        assert_eq!(t.last_seen("a"), Some(3));
        // 重复上报同一区间：已在水位内，忽略。
        assert!(t.report_gap("a", 1, 3, 0.0, 3.0).is_none());
        assert_eq!(t.total(), 1);
        // 之后连续段无缺口。
        assert!(t.observe_segment("a", 4, 3.0, 4.0).is_none());
        // 区间反向非法：忽略。
        assert!(t.report_gap("a", 9, 5, 0.0, 0.0).is_none());
    }

    #[test]
    fn clear_resets_everything() {
        let mut t = GapTracker::new();
        t.observe_segment("a", 1, 0.0, 1.0);
        t.observe_segment("a", 3, 3.0, 4.0);
        t.clear();
        assert!(t.gaps().is_empty());
        assert_eq!(t.total(), 0);
        assert_eq!(t.dropped(), 0);
        assert_eq!(t.last_seen("a"), None);
    }
}
