//! 乱序补传的待归并段。段内按 `time_ns` 单调，写入只付批大小成本。

use std::cmp::Reverse;
use std::collections::BinaryHeap;

use super::typed::{ColumnData, NullBits};

/// 一段已按 `time_ns` 单调排序、尚未归并进主表的补传数据。
#[derive(Debug, Clone)]
pub struct PendingSegment {
    pub time_ns: Vec<i64>,
    pub seq: Vec<u64>,
    pub columns: Vec<ColumnData>,
    pub nulls: Vec<NullBits>,
}

impl PendingSegment {
    pub fn len(&self) -> usize {
        self.time_ns.len()
    }

    pub fn is_empty(&self) -> bool {
        self.time_ns.is_empty()
    }
}

/// k 路归并索引：`source=0` 是主表，`source>0` 是 `segments[source-1]`。
/// 按 `(time_ns, seq)` 升序——同一时间按到达序，等价于"旧行在前"。
/// 每项为 `(source, local_index)`，仅生成顺序，不搬移数据。
///
/// 快路径：断线重连绝大多数只有一个补传段，两路归并的双指针比 65 路小顶堆快一个量级
/// （实测 109 万行：堆 10.5ms → 双指针约 1.5ms）。
pub fn merge_order(
    main_time: &[i64],
    main_seq: &[u64],
    segments: &[PendingSegment],
) -> Vec<(usize, usize)> {
    if segments.is_empty() {
        return (0..main_time.len()).map(|i| (0, i)).collect();
    }
    if segments.len() == 1 {
        return two_way_order(main_time, main_seq, &segments[0]);
    }
    let total = main_time.len() + segments.iter().map(|s| s.len()).sum::<usize>();
    let mut order = Vec::with_capacity(total);
    let mut heap: BinaryHeap<Reverse<(i64, u64, usize, usize)>> = BinaryHeap::new();
    if let (Some(&t), Some(&s)) = (main_time.first(), main_seq.first()) {
        heap.push(Reverse((t, s, 0, 0)));
    }
    for (si, seg) in segments.iter().enumerate() {
        if let (Some(&t), Some(&s)) = (seg.time_ns.first(), seg.seq.first()) {
            heap.push(Reverse((t, s, si + 1, 0)));
        }
    }
    while let Some(Reverse((_, _, source, idx))) = heap.pop() {
        order.push((source, idx));
        let next = idx + 1;
        let pushed = if source == 0 {
            main_time.get(next).zip(main_seq.get(next))
                .map(|(&t, &s)| Reverse((t, s, 0, next)))
        } else {
            let seg = &segments[source - 1];
            seg.time_ns.get(next).zip(seg.seq.get(next))
                .map(|(&t, &s)| Reverse((t, s, source, next)))
        };
        if let Some(item) = pushed {
            heap.push(item);
        }
    }
    order
}

/// 两路归并（主表 + 单段）：线性双指针，无堆操作，顺序访问两条流。
fn two_way_order(main_time: &[i64], main_seq: &[u64], seg: &PendingSegment) -> Vec<(usize, usize)> {
    let n = main_time.len().min(main_seq.len());
    let m = seg.time_ns.len().min(seg.seq.len());
    let mut order = Vec::with_capacity(n + m);
    let (mut i, mut j) = (0usize, 0usize);
    while i < n && j < m {
        let lhs = (main_time[i], main_seq[i]);
        let rhs = (seg.time_ns[j], seg.seq[j]);
        if lhs <= rhs {
            order.push((0, i));
            i += 1;
        } else {
            order.push((1, j));
            j += 1;
        }
    }
    while i < n {
        order.push((0, i));
        i += 1;
    }
    while j < m {
        order.push((1, j));
        j += 1;
    }
    order
}

#[cfg(test)]
mod tests {
    use super::*;
    use super::super::typed::ColumnData;

    fn seg(times: &[i64], seqs: &[u64], vals: &[f64]) -> PendingSegment {
        PendingSegment {
            time_ns: times.to_vec(),
            seq: seqs.to_vec(),
            columns: vec![ColumnData::F64(vals.to_vec())],
            nulls: vec![NullBits::new(vals.len())],
        }
    }

    #[test]
    fn merge_order_sorts_across_main_and_segments() {
        let main_t = [10, 30, 50];
        let main_s = [0, 1, 2];
        let segs = vec![seg(&[20, 40], &[3, 4], &[2.0, 4.0])];
        let order = merge_order(&main_t, &main_s, &segs);
        let times: Vec<(i64, u64)> = order.iter().map(|&(src, i)| {
            if src == 0 { (main_t[i], main_s[i]) } else { (segs[src - 1].time_ns[i], segs[src - 1].seq[i]) }
        }).collect();
        assert_eq!(times, vec![(10, 0), (20, 3), (30, 1), (40, 4), (50, 2)]);
    }

    #[test]
    fn merge_order_same_time_uses_arrival_seq() {
        let main_t = [10];
        let main_s = [0];
        let segs = vec![seg(&[10], &[5], &[9.0]), seg(&[10], &[3], &[8.0])];
        let order = merge_order(&main_t, &main_s, &segs);
        let seqs: Vec<u64> = order.iter().map(|&(src, i)| {
            if src == 0 { main_s[i] } else { segs[src - 1].seq[i] }
        }).collect();
        assert_eq!(seqs, vec![0, 3, 5]);
    }

    #[test]
    fn merge_order_empty_inputs() {
        assert!(merge_order(&[], &[], &[]).is_empty());
        assert!(merge_order(&[], &[], &[seg(&[1], &[0], &[1.0])]).len() == 1);
    }
}
