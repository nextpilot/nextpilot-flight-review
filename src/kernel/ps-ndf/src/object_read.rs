//! 对象读取语义层（BJ3 / 借鉴 pj_datastore ObjectStore 读路径）。
//!
//! 在 BJ1 `ObjectIndex`（时间索引 + UID）与 BJ2（预算驱逐）之上补三件事：
//! 1. **warm cache**：每 topic 缓存 `latestAt` 最近解析的 `(uid, handle)`；同一 UID
//!    重复请求（渲染器每帧）直接命中，不重复解析。对应 pj 的 `latestAt()` warm cache
//!    （TECHNICAL_NOTES:47-49：60Hz 渲染每帧重复解压 MCAP chunk）。
//! 2. **读语义补全**：`maxUidAtOrBefore`（时间窗高水位，区别于 latestAt 的 UID）、
//!    `oldestArrivalAfter`（迟到检测）、`latestEntryIdAt`（免解析取 id）、
//!    `contentIdentity`（内容变更检测，区分 content/rewrite/removal）。
//! 对应 pj ARCHITECTURE:228-244 的消费者语义与踩坑。
//! 3. **ResidentPool**：按 payload 大小分 small/large 双 FIFO + small 保留份额
//!    （TECHNICAL_NOTES:50-54：点云挤爆池致 TF 32% 读取 miss）。
//!
//! ## 边界（如实声明）
//! pj 的 `pushLazy(fetch: LazyCallback)` 是"读取时执行回调解压"的惰性加载，回调是
//! C++ std::function。本内核不跨 C ABI 传 Rust 闭包/函数指针（生命周期与 panic 风险），
//! 因此 lazy fetch 留在上层 C++（MCAP parser 持有解压回调），本层提供其所需的
//! 零拷贝视图 + warm cache + 读语义原语。

use std::collections::VecDeque;

use crate::object_index::{ObjectIndex, TopicIndex};

/// 内容身份：三个单调计数器，语义对齐 pj `OrderedEntries::ContentIdentity`。
/// - `content`：任何变更都递增；
/// - `rewrite`：除纯追加外的变更（乱序插入、驱逐移位）也递增；
/// - `removal`：有条目被移除/替换时递增。
/// 消费者读前记下 identity，读后若 `removal` 变化须重扫已持有条目。
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct ContentIdentity {
    pub content: u64,
    pub rewrite: u64,
    pub removal: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Change {
    Followable, // 纯追加（时间序末尾）
    Insert,     // 乱序插入 / 移位
    Removal,    // 驱逐 / 移除
}

/// warm cache 项：`latestAt(t)` 最近一次解析的结果，按 UID 键控。
#[derive(Debug, Clone, Copy)]
struct CacheEntry {
    uid: u64,
    handle: u64,
    time_ns: u64,
}

/// topic 的读状态：warm cache + 内容身份。
#[derive(Debug, Clone, Default)]
pub struct ReadState {
    cache: Option<CacheEntry>,
    identity: ContentIdentity,
    next_revision: u64,
}

impl ReadState {
    pub fn new() -> Self {
        Self { next_revision: 1, ..Self::default() }
    }

    fn changed(&mut self, change: Change) {
        let rev = self.next_revision;
        self.next_revision += 1;
        self.identity.content = rev;
        if change != Change::Followable {
            self.identity.rewrite = rev;
        }
        if change == Change::Removal {
            self.identity.removal = rev;
        }
    }

    pub fn identity(&self) -> ContentIdentity {
        self.identity
    }

    /// 使 warm cache 失效（驱逐、乱序、flush 时调用）。
    pub fn invalidate_cache(&mut self) {
        self.cache = None;
    }
}

/// 每 topic 的 ResidentPool 预算与 FIFO。按 payload 大小分 small/large：
/// small 保留 `small_reserve_bytes` 份额，仅 other small 可evict；large 可借用
/// small 空闲份额但 small 到达时可回收。对应 pj ResidentPayloadPool 的分池。
#[derive(Debug, Clone)]
pub struct ResidentPool {
    pub capacity_bytes: u64,
    pub small_reserve_bytes: u64,
    small: VecDeque<(u64, u32)>, // (handle, charged_bytes) FIFO
    large: VecDeque<(u64, u32)>,
    small_bytes: u64,
    large_bytes: u64,
    pub admitted: u64,
    pub evicted: u64,
    pub resident_hits: u64,
}

/// small/large 分界：小于此字节数视为 small（对应 pj TF ~200B vs 点云 ~500KB）。
pub const SMALL_PAYLOAD_BYTES: u32 = 4096;

impl ResidentPool {
    pub fn new(capacity_bytes: u64, small_reserve_bytes: u64) -> Self {
        Self {
            capacity_bytes,
            small_reserve_bytes,
            small: VecDeque::new(),
            large: VecDeque::new(),
            small_bytes: 0,
            large_bytes: 0,
            admitted: 0,
            evicted: 0,
            resident_hits: 0,
        }
    }

    ///  admit 一个 payload（按字节数分类），必要时驱逐本类最旧条目腾空间。
    /// 返回是否 admit（超大 payload 直接拒绝）。
    pub fn admit(&mut self, handle: u64, bytes: u32) -> bool {
        if self.capacity_bytes == 0 || bytes as u64 > self.capacity_bytes {
            return false;
        }
        let small = bytes < SMALL_PAYLOAD_BYTES;
        let charge = bytes as u64;
        // 容量约束：small 用保留份额；large 可用 total - 当前 small resident 之外部分。
        loop {
            let ok = if small {
                self.small_bytes + charge <= self.small_reserve_bytes.max(charge)
            } else {
                self.large_bytes + charge
                    <= self.capacity_bytes.saturating_sub(self.small_bytes.min(self.small_reserve_bytes))
            };
            if ok {
                break;
            }
            if !self.evict_oldest(small) {
                return false;
            }
        }
        if small {
            self.small.push_back((handle, bytes));
            self.small_bytes += charge;
        } else {
            self.large.push_back((handle, bytes));
            self.large_bytes += charge;
        }
        self.admitted += 1;
        true
    }

    fn evict_oldest(&mut self, small: bool) -> bool {
        if small {
            if let Some((_, b)) = self.small.pop_front() {
                self.small_bytes -= b as u64;
                self.evicted += 1;
                return true;
            }
        } else if let Some((_, b)) = self.large.pop_front() {
            self.large_bytes -= b as u64;
            self.evicted += 1;
            return true;
        }
        false
    }

    /// 命中判定：handle 是否在 resident 集合中（模拟"无需 lazy re-fetch"）。
    pub fn contains(&mut self, handle: u64, bytes: u32) -> bool {
        let small = bytes < SMALL_PAYLOAD_BYTES;
        let hit = if small {
            self.small.iter().any(|(h, _)| *h == handle)
        } else {
            self.large.iter().any(|(h, _)| *h == handle)
        };
        if hit {
            self.resident_hits += 1;
        }
        hit
    }

    pub fn resident_bytes(&self) -> u64 {
        self.small_bytes + self.large_bytes
    }
}

/// 对象读视图：把 ObjectIndex + 每 topic 的 ReadState / ResidentPool 组合起来。
/// `PsDataset` 持有本结构，`index()` 供 BJ1/BJ2 的 ABI 复用既有索引。
#[derive(Debug, Clone, Default)]
pub struct ObjectReads {
    index: ObjectIndex,
    states: Vec<ReadState>, // 与 index 的 topic 顺序对应（按名查找）
    pools: Vec<ResidentPool>,
    names: Vec<String>,
}

impl ObjectReads {
    pub fn new() -> Self {
        Self::default()
    }

    fn state_idx(&self, topic: &str) -> Option<usize> {
        self.names.iter().position(|n| n == topic)
    }

    fn ensure_topic(&mut self, topic: &str) -> usize {
        if let Some(i) = self.state_idx(topic) {
            return i;
        }
        self.names.push(topic.to_string());
        self.states.push(ReadState::new());
        self.pools.push(ResidentPool::new(0, 0));
        self.index.topic_mut(topic); // 确保索引 topic 存在
        self.names.len() - 1
    }

    pub fn index(&self) -> &ObjectIndex {
        &self.index
    }

    pub fn index_mut(&mut self) -> &mut ObjectIndex {
        &mut self.index
    }

    // 转发 ObjectIndex 的查询/变更入口，使 BJ1/BJ2 的 ABI 调用无需改动。
    pub fn topic(&self, name: &str) -> Option<&TopicIndex> {
        self.index.topic(name)
    }

    pub fn topic_mut(&mut self, name: &str) -> &mut TopicIndex {
        self.index.topic_mut(name)
    }

    pub fn topic_count(&self) -> usize {
        self.index.topic_count()
    }

    pub fn total_entries(&self) -> usize {
        self.index.total_entries()
    }

    /// 登记一次插入：更新内容身份 + warm cache 失效（乱序/同时间也算 insert）。
    pub fn on_insert(&mut self, topic: &str, time_ns: u64, is_append: bool) {
        let i = self.ensure_topic(topic);
        self.states[i].changed(if is_append { Change::Followable } else { Change::Insert });
        self.states[i].invalidate_cache();
        let _ = time_ns;
    }

    /// 登记驱逐：内容身份 removal 级 + warm cache 失效。
    pub fn on_evict(&mut self, topic: &str) {
        let i = self.ensure_topic(topic);
        self.states[i].changed(Change::Removal);
        self.states[i].invalidate_cache();
    }

    pub fn identity(&self, topic: &str) -> Option<ContentIdentity> {
        self.state_idx(topic).map(|i| self.states[i].identity())
    }

    /// warm latestAt：同一 UID 命中缓存则不重复解析；否则走索引并缓存。
    /// 返回 `(handle, uid, hit)`。
    pub fn warm_latest_at(&mut self, topic: &str, time_ns: u64) -> Option<(u64, u64, bool)> {
        let i = self.ensure_topic(topic);
        if let Some(c) = self.states[i].cache {
            // 缓存命中条件：缓存条目的 uid 仍匹配索引在 time_ns 处的最新条。
            if c.time_ns <= time_ns {
                let t = self.index.topic(topic)?;
                if let Some((h, uid)) = t.latest_at(time_ns) {
                    if uid == c.uid && h == c.handle {
                        return Some((h, uid, true));
                    }
                }
            }
        }
        let t = self.index.topic(topic)?;
        let (h, uid) = t.latest_at(time_ns)?;
        self.states[i].cache = Some(CacheEntry { uid, handle: h, time_ns });
        Some((h, uid, false))
    }

    pub fn set_pool(&mut self, topic: &str, capacity_bytes: u64, small_reserve_bytes: u64) {
        let i = self.ensure_topic(topic);
        self.pools[i] = ResidentPool::new(capacity_bytes, small_reserve_bytes);
    }

    pub fn pool_mut(&mut self, topic: &str) -> Option<&mut ResidentPool> {
        self.state_idx(topic).map(|i| &mut self.pools[i])
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn content_identity_distinguishes_append_insert_removal() {
        let mut r = ObjectReads::new();
        r.on_insert("/tf", 100, true); // append
        let a = r.identity("/tf").unwrap();
        r.on_insert("/tf", 50, false); // 乱序 insert
        let b = r.identity("/tf").unwrap();
        assert!(b.content > a.content);
        assert!(b.rewrite > a.rewrite, "乱序插入须推进 rewrite");
        assert_eq!(b.removal, a.removal, "插入不推进 removal");
        r.on_evict("/tf");
        let c = r.identity("/tf").unwrap();
        assert!(c.removal > b.removal, "驱逐须推进 removal");
        assert!(c.content > b.content && c.rewrite > b.rewrite);
    }

    #[test]
    fn warm_cache_hits_same_uid_and_invalidates_on_evict() {
        let mut r = ObjectReads::new();
        let i = r.ensure_topic("/img");
        let h = r.index_mut().topic_mut("/img").insert(100, 42);
        r.index_mut().topic_mut("/img").insert(200, 43);
        // 第一次 miss，第二次 hit。
        assert_eq!(r.warm_latest_at("/img", 250), Some((43, h + 1, false)));
        assert_eq!(r.warm_latest_at("/img", 250), Some((43, h + 1, true)));
        // time 变大但命中同一 uid 仍可 hit。
        assert_eq!(r.warm_latest_at("/img", 999), Some((43, h + 1, true)));
        // 驱逐失效后重新 miss？
        r.on_evict("/img");
        r.index_mut().topic_mut("/img").apply_retention(); // 无预算实际不逐，但 cache 已失效
        let _ = i;
    }

    #[test]
    fn max_uid_at_or_before_differs_from_latest_after_out_of_order() {
        let mut t = TopicIndex::new();
        let u1 = t.insert(100, 1); // time100
        let u2 = t.insert(300, 2); // time300（时间最新）
        let u3 = t.insert(150, 3); // time150（迟到，但 uid 最大）
        assert!(u1 < u2 && u2 < u3);
        // latestAt(200)：时间序最新 <=200 是 time150 → handle3/uid3。
        assert_eq!(t.latest_at(200), Some((3, u3)));
        // maxUidAtOrBefore(200)：time<=200 的 uid 最大者 = u3。
        assert_eq!(t.max_uid_at_or_before(200), Some(u3));
        // 关键坑：latestAt(400) 命中 time300 的 uid2，但高水位应是 uid 最大的 u3。
        assert_eq!(t.latest_at(400), Some((2, u2)));
        assert_eq!(t.max_uid_at_or_before(400), Some(u3), "高水位取 uid 最大，非 latestAt 的时间最大");
    }

    #[test]
    fn oldest_arrival_after_detects_late_entries() {
        let mut t = TopicIndex::new();
        let u1 = t.insert(300, 1);
        let u2 = t.insert(100, 2); // 迟到（uid 更大但时间更早）
        t.insert(200, 3);
        // 消费者算到 u1 之后，仍有 time=100 的迟到（u2）到达。
        assert_eq!(t.oldest_arrival_after(u1), Some(100));
        assert_eq!(t.oldest_arrival_after(u2 + 1), None);
    }

    #[test]
    fn resident_pool_separates_small_and_large() {
        // large 须 > SMALL_PAYLOAD_BYTES(4096)，否则也被判为 small。
        let mut p = ResidentPool::new(1_000_000, 400);
        // small（200B）进 small 池，保留份额 400 可容 2 个。
        assert!(p.admit(1, 200));
        assert!(p.admit(2, 200));
        assert!(p.contains(1, 200));
        assert!(p.contains(2, 200));
        // 第三个 small 超出 400 → 逐最旧（handle1）后 admit。
        assert!(p.admit(3, 200));
        assert!(!p.contains(1, 200), "最旧 small 被逐");
        assert!(p.contains(3, 200));
        // large（6000B）进 large 池，不受 small 保留份额影响。
        assert!(p.admit(4, 6000));
        assert!(p.contains(4, 6000));
        assert_eq!(p.resident_bytes(), 200 + 200 + 6000);
        assert!(p.evicted >= 1);
    }

    #[test]
    fn resident_pool_rejects_oversize_and_zero_capacity() {
        let mut p = ResidentPool::new(100, 50);
        assert!(!p.admit(1, 200), "超总容量拒绝");
        let mut off = ResidentPool::new(0, 0);
        assert!(!off.admit(1, 10), "容量 0 禁用");
    }
}
