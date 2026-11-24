//! ps-dag 迟到输入检测（BJ5）。
//!
//! 增量求值原先只看「长度 + generation」，BJ4 中部 backfill 物化后新增行落在
//! 已消费前缀中间，尾部增量会漏掉它们。本模块为每节点记输入水位，判定本次
//! 输入相对上次消费到底是无变化、纯追加，还是发生了迟到/收缩/上游改写。

/// 节点已消费输入的水位。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Watermark {
    /// 已消费输入行数。
    pub len: usize,
    /// 已消费输入最后一行时间的位模式（NaN-safe 比较）。len==0 时为 None。
    pub last_t_bits: Option<u64>,
    /// 已消费的上游输出修订号；Channel 输入恒为 0。
    pub upstream_rev: u64,
}

impl Watermark {
    /// 从当前输入构造水位。n 为实际消费行数（取 t/v 长度较小者）。
    pub fn from_input(in_t: &[f64], n: usize, upstream_rev: u64) -> Self {
        Self {
            len: n,
            last_t_bits: if n == 0 { None } else { Some(in_t[n - 1].to_bits()) },
            upstream_rev,
        }
    }
}

/// 增量判定结果。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Verdict {
    /// 输入无变化，整节点跳过。
    Skip,
    /// 纯追加，走尾部增量（Stateless append / Window 回看）。
    Incremental,
    /// 迟到、收缩、上游改写或强制全量，从头重算。
    Full,
}

/// 判定本次输入相对上次水位的处理方式。
///
/// - `gen_changed`：本次 generation 是否大于上次消费值
/// - `wm`：上次消费的水位（首次为 None）
/// - `force_full`：上次失败回滚后置位，强制本次全量
/// - `n`：本次输入行数
/// - `in_t`：本次输入时间轴（至少 n 行可读）
/// - `up_rev`：本次上游输出修订号；Channel 输入传 0
pub fn classify(
    gen_changed: bool,
    wm: Option<Watermark>,
    force_full: bool,
    n: usize,
    in_t: &[f64],
    up_rev: u64,
) -> Verdict {
    if force_full {
        return Verdict::Full;
    }
    let Some(w) = wm else {
        return Verdict::Full; // 首次消费。
    };
    if w.len == 0 || n < w.len {
        return Verdict::Full; // 上次空输入，或输入收缩。
    }
    if up_rev != w.upstream_rev {
        return Verdict::Full; // 上游本轮重算，输出内容可能已变。
    }

    // 中部插入会把原第 len-1 行推后，使其时间不再等于水位记录的末行时间。
    let prefix_intact = in_t.get(w.len - 1).map(|t| t.to_bits()) == w.last_t_bits;
    if !prefix_intact {
        return Verdict::Full; // 迟到输入落在已消费前缀内。
    }

    if n == w.len {
        // 长度未变、前缀末行未变、上游未变：本通道无新行。
        // generation 变化可能来自其它通道 append，对本节点无需重算。
        let _ = gen_changed;
        return Verdict::Skip;
    }

    // 新增区间出现时间倒退：迟到行落在物理尾部。
    if let Some(bits) = w.last_t_bits {
        let wm_t = f64::from_bits(bits);
        if in_t[w.len..n].iter().any(|&t| t < wm_t) {
            return Verdict::Full;
        }
    }
    Verdict::Incremental
}

#[cfg(test)]
mod tests {
    use super::*;

    fn wm(len: usize, last: f64, rev: u64) -> Watermark {
        Watermark::from_input(&vec![0.0; len.saturating_sub(1)].into_iter().chain([last]).collect::<Vec<_>>(), len, rev)
    }

    #[test]
    fn first_consumption_is_full() {
        assert_eq!(classify(true, None, false, 3, &[1.0, 2.0, 3.0], 0), Verdict::Full);
    }

    #[test]
    fn pure_append_is_incremental() {
        let w = wm(3, 3.0, 0);
        let t = [1.0, 2.0, 3.0, 4.0, 5.0];
        assert_eq!(classify(true, Some(w), false, 5, &t, 0), Verdict::Incremental);
    }

    #[test]
    fn unchanged_skips_even_if_generation_moved() {
        let w = wm(3, 3.0, 0);
        let t = [1.0, 2.0, 3.0];
        assert_eq!(classify(false, Some(w), false, 3, &t, 0), Verdict::Skip);
        assert_eq!(classify(true, Some(w), false, 3, &t, 0), Verdict::Skip);
    }

    #[test]
    fn middle_backfill_rewrites_prefix_and_is_full() {
        // 已消费 [10,20,30]；中部插入 25 后前缀末行变成 20（原 30 被推后）。
        let w = wm(3, 30.0, 0);
        let t = [10.0, 20.0, 25.0, 30.0];
        assert_eq!(classify(true, Some(w), false, 4, &t, 0), Verdict::Full);
    }

    #[test]
    fn late_row_in_appended_tail_is_full() {
        // 前缀未变，但新增行时间 2.0 < 水位 3.0。
        let w = wm(3, 3.0, 0);
        let t = [1.0, 2.0, 3.0, 2.0];
        assert_eq!(classify(true, Some(w), false, 4, &t, 0), Verdict::Full);
    }

    #[test]
    fn shrink_is_full() {
        let w = wm(5, 5.0, 0);
        let t = [1.0, 2.0, 3.0];
        assert_eq!(classify(true, Some(w), false, 3, &t, 0), Verdict::Full);
    }

    #[test]
    fn upstream_revision_change_is_full() {
        // 长度与前缀都没变，但上游重算了，下游必须跟着重算。
        let w = wm(3, 3.0, 4);
        let t = [1.0, 2.0, 3.0];
        assert_eq!(classify(true, Some(w), false, 3, &t, 5), Verdict::Full);
    }

    #[test]
    fn force_full_overrides_all() {
        let w = wm(3, 3.0, 0);
        let t = [1.0, 2.0, 3.0];
        assert_eq!(classify(false, Some(w), true, 3, &t, 0), Verdict::Full);
    }

    #[test]
    fn nan_prefix_time_compares_by_bits() {
        // 时间轴含 NaN 时按位比较，同一数据重复读到相同位模式仍认前缀未变。
        let nan = f64::NAN;
        let w = Watermark::from_input(&[1.0, nan], 2, 0);
        let t = [1.0, nan, 3.0];
        assert_eq!(classify(true, Some(w), false, 3, &t, 0), Verdict::Incremental);
    }
}
