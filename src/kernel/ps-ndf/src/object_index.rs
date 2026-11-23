//! 时间索引对象集合（BJ1 / 借鉴 pj_datastore ObjectStore 踩坑经验）。
//!
//! 与 `ObjectStore`（全局 payload 池 + 稳定句柄）分工：
//! - `ObjectStore` 只管字节与句柄生命周期，不认识时间、topic；
//! - `ObjectIndex` 只管"某个 topic 在某个时间有哪些对象句柄"，按时间有序、按到达序可追。
//!
//! ## 为什么这样设计（对应 pj 的真实事故）
//! - **乱序保留**：ROS `/tf` 约 1% 消息乱序，拒绝时间回退会丢行（TECHNICAL_NOTES:32-34）。
//!   本模块对乱序插入按 `(time_ns, uid)` 定位，不拒绝、不丢。
//! - **避免 O(n²)**：pj ObjectStore 乱序插入线性挪动条目，200k 条目约 1.1ms/次、
//!   mostly-unsorted 退化 O(n²)（TECHNICAL_NOTES:37-39）。本模块用 `BTreeMap` 让
//!   定位/插入为 O(log n)，遍历为有序 O(n)，不逐条挪动数组。
//!
//! **性能权衡（实测，须如实知悉）**：`BTreeMap` 在乱序场景避免 O(n²) 退化，
//! 但在纯有序追加场景比 `Vec::push` 慢（节点分配开销）。基准（release）：
//! 50k 随机时间戳 BTreeMap 8.8ms vs Vec 线性挪动 81ms（约 9x 优势）；
//! 200k 纯递增 BTreeMap 26.5ms vs Vec push 5.3ms（约 5x 劣势）。
//! 对象索引服务于图像/点云/TF 等低频对象（每秒几十~几百条），且 MCAP 场景
//! 的 chunk 解压本身是毫秒级，索引插入的微秒级差异在实践中不是瓶颈；
//! 标量主数据仍走单调 `typed::Table`，完全不受此权衡影响。
//! - **到达序 ≠ 时间序**：pj 用进程级 `SequentialUID` 把两者分开，供 `drainNewSince`
//!   等消费者按到达序读取，且 UID 不因驱逐/替换复用（ARCHITECTURE:228-233）。本模块的
//!   `uid` 来自全局单调原子计数器，稀疏、不复用、与时间无关。
//! - **相等时间保持到达序**：`BTreeMap<(time_ns, uid), _>` 的 key 含 uid，天然稳定。

use std::collections::BTreeMap;
use std::sync::atomic::{AtomicU64, Ordering};

/// 进程级到达序计数器：所有 topic 共享、严格递增、稀疏、永不复用。
/// 与 pj 的 `SequentialUID` 同语义：UID 序 = 到达序，独立于时间戳序。
static NEXT_UID: AtomicU64 = AtomicU64::new(1);

/// 单个 topic 的时间索引。
///
/// `by_time` 以 `(time_ns, uid)` 为 key：
/// - 时间升序 → 支持时间点查找与时间窗遍历；
/// - 同时间内 uid 升序 → 保持到达顺序（稳定）。
///
/// `by_uid` 提供到达序遍历（`drain_new_since` 用），key 为 uid。
#[derive(Debug, Clone, Default)]
pub struct TopicIndex {
    by_time: BTreeMap<(u64, u64), u64>, // (time_ns, uid) -> object handle
    by_uid: BTreeMap<u64, (u64, u64)>,  // uid -> (time_ns, handle)
}

impl TopicIndex {
    pub fn new() -> Self {
        Self::default()
    }

    /// 在 `time_ns` 处登记一个已存在的对象句柄，返回本次插入的到达序 UID。
    ///
    /// 乱序插入只走 BTreeMap 定位，不挪动已有条目；`uid` 由全局计数器给出，
    /// 因此即使两条记录时间相同，也有不同的 uid 保持到达序。
    pub fn insert(&mut self, time_ns: u64, handle: u64) -> u64 {
        let uid = NEXT_UID.fetch_add(1, Ordering::Relaxed);
        self.by_time.insert((time_ns, uid), handle);
        self.by_uid.insert(uid, (time_ns, handle));
        uid
    }

    /// 时间点查找：`<= time_ns` 的最新一条（含端点），返回 `(handle, uid)`。
    /// 对应 pj 的 `latestAt`，但只给索引定位，不解析 payload。
    pub fn latest_at(&self, time_ns: u64) -> Option<(u64, u64)> {
        let ((t, uid), handle) = self.by_time.range(..=(time_ns, u64::MAX)).next_back()?;
        debug_assert!(*t <= time_ns);
        Some((*handle, *uid))
    }

    /// 到达序游标：取所有 `uid > since` 的条目，按 uid 升序，返回 `(uid, time_ns, handle)`。
    /// 对应 pj 的 `drainNewSince`：晚到的条目即使时间更早，只要到达序靠后就会被取到。
    pub fn drain_new_since(&self, since: u64) -> Vec<(u64, u64, u64)> {
        self.by_uid
            .range((since + 1)..)
            .map(|(uid, (t, h))| (*uid, *t, *h))
            .collect()
    }

    /// 时间窗遍历：`[t0, t1]` 闭区间内的 `(time_ns, uid, handle)`，按时间升序。
    pub fn range_by_time(&self, t0: u64, t1: u64) -> Vec<(u64, u64, u64)> {
        if t0 > t1 {
            return Vec::new();
        }
        self.by_time
            .range((t0, 0)..=(t1, u64::MAX))
            .map(|((t, uid), h)| (*t, *uid, *h))
            .collect()
    }

    /// 当前最小到达序 UID；空 topic 返回 0。游标低于此值意味着错过了被驱逐的条目。
    pub fn first_uid(&self) -> u64 {
        self.by_uid.keys().next().copied().unwrap_or(0)
    }

    pub fn len(&self) -> usize {
        self.by_time.len()
    }

    pub fn is_empty(&self) -> bool {
        self.by_time.is_empty()
    }
}

/// Dataset 级对象索引：按 topic 名组织多个 `TopicIndex`。
/// topic 名由调用方约定（如 `/camera/image`、`/tf`），内核不解释。
#[derive(Debug, Clone, Default)]
pub struct ObjectIndex {
    topics: BTreeMap<String, TopicIndex>,
}

impl ObjectIndex {
    pub fn new() -> Self {
        Self::default()
    }

    /// 取得（或按需创建）某 topic 的可变索引。
    pub fn topic_mut(&mut self, name: &str) -> &mut TopicIndex {
        self.topics.entry(name.to_string()).or_insert_with(TopicIndex::new)
    }

    pub fn topic(&self, name: &str) -> Option<&TopicIndex> {
        self.topics.get(name)
    }

    pub fn topic_count(&self) -> usize {
        self.topics.len()
    }

    pub fn total_entries(&self) -> usize {
        self.topics.values().map(|t| t.len()).sum()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn in_order_insert_preserves_time_and_arrival() {
        let mut t = TopicIndex::new();
        let u1 = t.insert(100, 11);
        let u2 = t.insert(200, 22);
        let u3 = t.insert(300, 33);
        assert!(u1 < u2 && u2 < u3, "UID 按到达序递增");
        assert_eq!(t.latest_at(250), Some((22, u2)));
        assert_eq!(t.latest_at(999), Some((33, u3)));
        assert_eq!(t.latest_at(99), None);
        assert_eq!(t.range_by_time(100, 200), vec![(100, u1, 11), (200, u2, 22)]);
    }

    #[test]
    fn out_of_order_insert_is_kept_and_sorted_by_time() {
        // 复刻 pj 的 /tf 乱序场景：时间戳回退的行不能丢。
        let mut t = TopicIndex::new();
        let u1 = t.insert(300, 33); // 先到（时间较晚）
        let u2 = t.insert(100, 11); // 晚到（时间更早）——不能丢
        let u3 = t.insert(200, 22);
        // 时间序遍历按 time 升序，与到达序不同。
        let by_time = t.range_by_time(0, 400);
        assert_eq!(by_time, vec![(100, u2, 11), (200, u3, 22), (300, u1, 33)]);
        assert_eq!(t.len(), 3, "乱序行全部保留");
        // latestAt 按时间而非到达序。
        assert_eq!(t.latest_at(250), Some((22, u3)));
    }

    #[test]
    fn equal_time_keeps_arrival_order() {
        let mut t = TopicIndex::new();
        let u1 = t.insert(100, 11);
        let u2 = t.insert(100, 22);
        let u3 = t.insert(100, 33);
        assert!(u1 < u2 && u2 < u3);
        // 同时间内按 uid（到达序）升序。
        assert_eq!(t.range_by_time(100, 100), vec![(100, u1, 11), (100, u2, 22), (100, u3, 33)]);
        // latestAt 取同时间内最后到达的。
        assert_eq!(t.latest_at(100), Some((33, u3)));
    }

    #[test]
    fn drain_new_since_follows_arrival_not_time() {
        let mut t = TopicIndex::new();
        let u1 = t.insert(300, 33);
        let u2 = t.insert(100, 11); // 迟到但到达序靠后
        let cursor = t.drain_new_since(u1);
        assert_eq!(cursor, vec![(u2, 100, 11)], "迟到条目按到达序被取到");
        // 从 0 取全部，按 uid 升序而非时间序。
        let all = t.drain_new_since(0);
        assert_eq!(all, vec![(u1, 300, 33), (u2, 100, 11)]);
    }

    #[test]
    fn uid_is_process_wide_monotonic_and_not_reused() {
        let mut a = TopicIndex::new();
        let mut b = TopicIndex::new();
        let a1 = a.insert(1, 1);
        let b1 = b.insert(1, 1);
        let a2 = a.insert(2, 2);
        assert!(a1 < b1 && b1 < a2, "UID 跨 topic 进程级递增且稀疏");
        // 清空后新插入UID仍更大，不复用。
        let last = a2;
        let mut c = TopicIndex::new();
        let c1 = c.insert(0, 9);
        assert!(c1 > last, "UID 永不复用");
    }

    #[test]
    fn empty_topic_and_range_edge() {
        let t = TopicIndex::new();
        assert!(t.is_empty());
        assert_eq!(t.latest_at(0), None);
        assert_eq!(t.first_uid(), 0);
        assert!(t.range_by_time(10, 5).is_empty(), "倒置区间返回空");
    }

    #[test]
    fn dataset_index_separates_topics() {
        let mut idx = ObjectIndex::new();
        idx.topic_mut("/tf").insert(10, 1);
        idx.topic_mut("/tf").insert(20, 2);
        idx.topic_mut("/image").insert(10, 3);
        assert_eq!(idx.topic_count(), 2);
        assert_eq!(idx.total_entries(), 3);
        assert_eq!(idx.topic("/tf").unwrap().len(), 2);
        assert_eq!(idx.topic("/image").unwrap().len(), 1);
        assert!(idx.topic("/missing").is_none());
    }
}
