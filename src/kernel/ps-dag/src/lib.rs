//! ps-dag：派生序列依赖图编排（S7 / SCENE-08）。
//!
//! 节点 = 一次算子调用，边 = 数据依赖。建立在 S3 的 generation 变更通知之上：
//! append 只 bump generation，incremental 求值按各算子的「记忆跨度」回看尾部重算，
//! 保证与全量求值逐位一致，且重算量随增量行数线性增长（逐点 O(新增)、窗口 O(W+新增)）。
//!
//! 不修改 ps-ops 任何算子实现；本 crate 只做编排与增量调度。

use serde_json::Value;
use std::collections::{HashMap, HashSet, VecDeque};

mod gap;
mod watermark;
pub use gap::{DEFAULT_GAP_CAPACITY, Gap, GapTracker};
pub use watermark::{Verdict, Watermark, classify};

/// 一段派生序列：时间轴（秒）+ 值。与 ps-ops filter 的 (t, v) 输出同构。
#[derive(Debug, Clone, Default, PartialEq)]
pub struct Series {
    pub t: Vec<f64>,
    pub v: Vec<f64>,
}

impl Series {
    fn append(&mut self, other: &Series) {
        self.t.extend_from_slice(&other.t);
        self.v.extend_from_slice(&other.v);
    }
    /// 用 other 从 keep 行起的部分替换 self 尾部（[0,keep) 保留）。
    fn splice_tail_from(&mut self, keep: usize, other: &Series, offset: usize) {
        self.t.truncate(keep);
        self.v.truncate(keep);
        self.t.extend_from_slice(&other.t[offset..]);
        self.v.extend_from_slice(&other.v[offset..]);
    }
}

/// 算子的记忆跨度：决定增量重算的回看行数。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Memory {
    /// 逐点无状态：输出 i 只依赖输入 i。回看 0 行。
    Stateless,
    /// 有限窗口 W：输出 i 依赖输入 [max(0,i-W+1), i]。回看 W-1 行。
    Window(usize),
    /// 有状态 / 输出长度可变 / 依赖全局：从头全量重算。
    Full,
}

/// 按 filter id + 参数求算子记忆跨度。事实来源：ps-ops/filters.rs 各分支。
/// 不改算子实现；这是编排层对算子的元数据标注。
pub fn memory_of(filter_id: &str, params: &Value) -> Memory {
    let window = || {
        params
            .get("window")
            .and_then(|v| v.as_u64())
            .unwrap_or(100)
            .max(1) as usize
    };
    match filter_id {
        // 逐点：输出 i 只依赖输入 i（scale 的时间偏移也是逐点）。
        "expression" | "none" | "absolute" | "scale" | "binary_filter" => Memory::Stateless,
        // 有限窗口：左 padding 用当前值，窗口不足 W 时用 v[i] 补齐。
        "moving_average" | "moving_rms" | "moving_variance" => Memory::Window(window()),
        // derivative/time_since_previous 输出比输入少 1（首样本抑制）；
        // integral 前缀累积；normalize 依赖全局 min/max；outlier_removal 丢弃样本；
        // samples_counter 依赖时间窗口队列——全部走全量，保证逐位一致。
        "derivative" | "integral" | "normalize" | "outlier_removal" | "samples_counter"
        | "time_since_previous" => Memory::Full,
        // 未知算子也交回 ps-ops 报错，这里保守用全量。
        _ => Memory::Full,
    }
}

/// 节点输入来源：外部通道名，或上游节点 id。
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Source {
    Channel(String),
    Node(usize),
}

/// 图节点：一次算子调用 + 输入绑定。
#[derive(Debug, Clone)]
pub struct Node {
    pub name: String,
    pub filter_id: String,
    pub params: Value,
    pub input: Source,
    pub memory: Memory,
}

impl Node {
    pub fn new(name: String, filter_id: String, params: Value, input: Source) -> Self {
        let memory = memory_of(&filter_id, &params);
        Self { name, filter_id, params, input, memory }
    }
}

/// 图错误。
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum GraphError {
    NodeNotFound(usize),
    ChannelMissing(String),
    Cycle,
    OpFailed(String),
    DuplicateEdge,
}

/// 派生依赖图。
#[derive(Debug, Clone, Default)]
pub struct Graph {
    pub nodes: Vec<Node>,
    /// 依赖边 (from, to)：to 的输入可读 from 的输出。
    pub edges: Vec<(usize, usize)>,
}

impl Graph {
    pub fn new() -> Self {
        Self::default()
    }

    /// 加节点，返回 id。
    pub fn add_node(&mut self, node: Node) -> usize {
        let id = self.nodes.len();
        self.nodes.push(node);
        id
    }

    /// 加依赖边。若形成环拒绝（Cycle）；重复边拒绝（DuplicateEdge）。
    pub fn add_edge(&mut self, from: usize, to: usize) -> Result<(), GraphError> {
        if from >= self.nodes.len() || to >= self.nodes.len() {
            return Err(GraphError::NodeNotFound(if from >= self.nodes.len() { from } else { to }));
        }
        if from == to || self.reachable(to, from) {
            return Err(GraphError::Cycle);
        }
        if self.edges.contains(&(from, to)) {
            return Err(GraphError::DuplicateEdge);
        }
        self.edges.push((from, to));
        Ok(())
    }

    /// from 是否可达 target（沿 from->to 方向 DFS）。
    fn reachable(&self, from: usize, target: usize) -> bool {
        let mut seen = vec![false; self.nodes.len()];
        let mut stack = vec![from];
        while let Some(u) = stack.pop() {
            if u == target {
                return true;
            }
            if seen[u] {
                continue;
            }
            seen[u] = true;
            for &(a, b) in &self.edges {
                if a == u {
                    stack.push(b);
                }
            }
        }
        false
    }

    /// 有效依赖边 = 显式 add_edge + 节点 Source::Node 的隐式声明（去重）。
    /// 调用方可能只声明 input=Node(up) 而不显式加边，拓扑序必须认这条依赖。
    fn effective_edges(&self) -> Vec<(usize, usize)> {
        let mut e = self.edges.clone();
        for (id, node) in self.nodes.iter().enumerate() {
            if let Source::Node(up) = node.input {
                if up < self.nodes.len() && !e.contains(&(up, id)) {
                    e.push((up, id));
                }
            }
        }
        e
    }

    /// 拓扑序（上游优先）。入度 0 入队，Kahn。
    pub fn topological_order(&self) -> Result<Vec<usize>, GraphError> {
        let n = self.nodes.len();
        let edges = self.effective_edges();
        let mut indeg = vec![0usize; n];
        for &(_, to) in &edges {
            indeg[to] += 1;
        }
        let mut q: VecDeque<usize> = (0..n).filter(|&i| indeg[i] == 0).collect();
        let mut order = Vec::with_capacity(n);
        while let Some(u) = q.pop_front() {
            order.push(u);
            for &(a, b) in &edges {
                if a == u {
                    indeg[b] -= 1;
                    if indeg[b] == 0 {
                        q.push_back(b);
                    }
                }
            }
        }
        if order.len() != n {
            Err(GraphError::Cycle)
        } else {
            Ok(order)
        }
    }

    /// 节点的实际输入来源：若有一条入边指向它，用该边上游覆盖声明的 input。
    fn resolve_input(&self, id: usize) -> Source {
        self.edges
            .iter()
            .find(|&(_, to)| *to == id)
            .map(|&(from, _)| Source::Node(from))
            .unwrap_or_else(|| self.nodes[id].input.clone())
    }

    /// 收集求值所需通道名（未被边覆盖的 Channel 输入）。
    fn required_channels(&self) -> HashSet<String> {
        let mut s = HashSet::new();
        for id in 0..self.nodes.len() {
            if let Source::Channel(name) = self.resolve_input(id) {
                s.insert(name);
            }
        }
        s
    }

    /// 全量求值：按拓扑序逐节点 apply，返回每节点的输出 Series。
    pub fn evaluate_full(&self, channels: &HashMap<String, Series>) -> Result<Vec<Series>, GraphError> {
        for name in self.required_channels() {
            if !channels.contains_key(&name) {
                return Err(GraphError::ChannelMissing(name));
            }
        }
        let order = self.topological_order()?;
        let mut outputs: Vec<Option<Series>> = vec![None; self.nodes.len()];
        for id in order {
            let node = &self.nodes[id];
            let input = match self.resolve_input(id) {
                Source::Channel(name) => channels.get(&name).unwrap(),
                Source::Node(up) => outputs[up].as_ref().ok_or(GraphError::NodeNotFound(up))?,
            };
            let (t, v) = ps_ops::filters::apply(&node.filter_id, &input.t, &input.v, &node.params)
                .map_err(GraphError::OpFailed)?;
            outputs[id] = Some(Series { t, v });
        }
        Ok(outputs.into_iter().map(|o| o.unwrap()).collect())
    }
}

/// 单节点增量状态。
#[derive(Debug, Clone)]
struct NodeState {
    generation: u64,
    computed_len: usize,
    out: Series,
    /// 已消费输入水位（BJ5 迟到检测）。None 表示尚未消费。
    watermark: Option<Watermark>,
    /// 上次失败回滚后置位：watermark 已失效，下次强制全量。
    force_full: bool,
    /// 输出修订号：每次成功重算 +1，下游据此检测上游输出改写。
    out_rev: u64,
}

/// 重算撤销日志：失败时按逆序精确还原输出，保证事务性（BJ5）。
#[derive(Debug)]
enum Undo {
    /// 该节点本次未触碰，无需还原。
    None,
    /// 全量重算：持有被替换的旧输出。
    Original(Option<Series>),
    /// Stateless 追加：记追加前的行数，回滚时截断。
    Append(usize),
    /// Window 拼接：记起点与被覆盖的原尾部，回滚时截断再补回。
    Splice(usize, Vec<f64>, Vec<f64>),
}

/// 增量引擎：持有图 + 每节点的生成快照与已算结果。
#[derive(Debug, Clone)]
pub struct IncrementalEngine {
    pub graph: Graph,
    states: Vec<NodeState>,
    /// 累计重算行数（用于验证「耗时与增量行数成正比」）。
    pub recomputed_rows: usize,
    /// 输入段缺口台账（不参与求值）。
    gaps: GapTracker,
}

impl IncrementalEngine {
    pub fn new(graph: Graph) -> Self {
        let states = graph.nodes.iter().map(|_| NodeState::default_state()).collect();
        Self { graph, states, recomputed_rows: 0, gaps: GapTracker::new() }
    }

    /// 上报「收到了一个段」，段 id 跳号时自动记缺口。返回新记录的缺口。
    pub fn observe_segment(
        &mut self,
        channel: &str,
        seg_id: u64,
        t_lo: f64,
        t_hi: f64,
    ) -> Option<Gap> {
        self.gaps.observe_segment(channel, seg_id, t_lo, t_hi)
    }

    /// 显式上报一处已知缺口（段 id 区间 + 时间提示）。
    pub fn report_gap(
        &mut self,
        channel: &str,
        from_id: u64,
        to_id: u64,
        t_lo: f64,
        t_hi: f64,
    ) -> Option<Gap> {
        self.gaps.report_gap(channel, from_id, to_id, t_lo, t_hi)
    }

    /// 最近缺口（最旧在前，长度 ≤ 容量上限）。
    pub fn gaps(&self) -> &[Gap] {
        self.gaps.gaps()
    }

    /// 缺口台账只读访问（累计数、被挤出条数、容量）。
    pub fn gap_tracker(&self) -> &GapTracker {
        &self.gaps
    }

    /// 设置缺口保留上限。
    pub fn set_gap_capacity(&mut self, cap: usize) {
        self.gaps.set_capacity(cap);
    }

    pub fn output(&self, id: usize) -> Option<&Series> {
        self.states.get(id).map(|s| &s.out)
    }

    /// 加节点并返回 id；graph 与 states 同步扩展。
    pub fn add_node(&mut self, node: Node) -> usize {
        let id = self.graph.add_node(node);
        self.states.push(NodeState::default_state());
        id
    }

    /// 加依赖边（转发图检查，含循环检测）。
    pub fn add_edge(&mut self, from: usize, to: usize) -> Result<(), GraphError> {
        self.graph.add_edge(from, to)
    }

    /// 增量求值（事务化）。
    ///
    /// 每个节点先按 watermark 判定 verdict：Skip 跳过、Incremental 尾部增量、
    /// Full 全量重算。迟到输入（中部插入）或上游改写会判为 Full 并沿拓扑传播。
    /// 重算用 Undo 日志记录每一步的输出变形；任一算子失败则按日志精确回滚，
    /// 旧输出完好（不清空、不重复），并把失败节点置 force_full 供下次全量恢复。
    pub fn evaluate(
        &mut self,
        generation: u64,
        channels: &HashMap<String, Series>,
    ) -> Result<(), GraphError> {
        for name in self.graph.required_channels() {
            if !channels.contains_key(&name) {
                return Err(GraphError::ChannelMissing(name));
            }
        }
        let order = self.graph.topological_order()?;
        let n_nodes = self.states.len();
        // 标量快照：回滚时恢复 generation / computed_len / watermark / out_rev。
        let snap: Vec<(u64, usize, Option<Watermark>, u64)> = self
            .states
            .iter()
            .map(|s| (s.generation, s.computed_len, s.watermark, s.out_rev))
            .collect();
        let mut undo: Vec<Undo> = (0..n_nodes).map(|_| Undo::None).collect();
        let mut failed: Option<GraphError> = None;
        let recomputed_before = self.recomputed_rows;

        for id in order {
            if failed.is_some() {
                break;
            }
            let source = self.graph.resolve_input(id);
            let node = &self.graph.nodes[id];
            // 拓扑序保证上游先于本节点，split_at_mut 同时借上游（不可变）与本节点（可变）。
            let (before, at) = self.states.split_at_mut(id);
            let st = &mut at[0];
            let (n, up_rev) = match &source {
                Source::Channel(name) => {
                    let s = channels.get(name).unwrap();
                    (s.t.len().min(s.v.len()), 0u64)
                }
                Source::Node(up) => {
                    let s = &before[*up].out;
                    (s.t.len().min(s.v.len()), before[*up].out_rev)
                }
            };

            // 迟到检测：本次输入 time 轴用于比对水位末行。
            let in_t_probe: &[f64] = match &source {
                Source::Channel(name) => &channels.get(name).unwrap().t,
                Source::Node(up) => &before[*up].out.t,
            };
            let gen_changed = generation > st.generation;
            let verdict = classify(gen_changed, st.watermark, st.force_full, n, in_t_probe, up_rev);
            if verdict == Verdict::Skip {
                continue;
            }

            let computed = st.computed_len;
            self.recomputed_rows = self.recomputed_rows.saturating_add(match node.memory {
                Memory::Stateless => n.saturating_sub(computed),
                Memory::Window(w) => (w.saturating_sub(1)).min(computed) + n.saturating_sub(computed),
                Memory::Full => n,
            });

            // 只借用需要的输入切片；增量路径绝不整条 clone。
            let (in_t, in_v): (&[f64], &[f64]) = match &source {
                Source::Channel(name) => {
                    let s = channels.get(name).unwrap();
                    (&s.t[..n], &s.v[..n])
                }
                Source::Node(up) => {
                    let s = &before[*up].out;
                    (&s.t[..n], &s.v[..n])
                }
            };

            // verdict==Full（迟到/收缩/上游改写）或记忆跨度要求全量：从头算，旧输出整体搁置。
            let full = verdict == Verdict::Full || node.memory == Memory::Full;
            let out = if full {
                let (t, v) = match ps_ops::filters::apply(&node.filter_id, in_t, in_v, &node.params) {
                    Ok(r) => r,
                    Err(e) => { failed = Some(GraphError::OpFailed(e)); break; }
                };
                undo[id] = Undo::Original(Some(std::mem::take(&mut st.out)));
                Series { t, v }
            } else {
                match node.memory {
                    Memory::Full => unreachable!("Full 记忆跨度的 verdict 不可能为 Incremental"),
                    Memory::Stateless => {
                        let (t, v) = match ps_ops::filters::apply(
                            &node.filter_id,
                            &in_t[computed..n],
                            &in_v[computed..n],
                            &node.params,
                        ) {
                            Ok(r) => r,
                            Err(e) => { failed = Some(GraphError::OpFailed(e)); break; }
                        };
                        let mut out = std::mem::take(&mut st.out);
                        let keep = out.t.len();
                        undo[id] = Undo::Append(keep);
                        out.append(&Series { t, v });
                        out
                    }
                    Memory::Window(w) => {
                        let pad = w.saturating_sub(1);
                        let r = computed.saturating_sub(pad);
                        let s = r.saturating_sub(pad);
                        let (t, v) = match ps_ops::filters::apply(
                            &node.filter_id,
                            &in_t[s..n],
                            &in_v[s..n],
                            &node.params,
                        ) {
                            Ok(r) => r,
                            Err(e) => { failed = Some(GraphError::OpFailed(e)); break; }
                        };
                        let offset = r - s;
                        let mut out = std::mem::take(&mut st.out);
                        // 记录被覆盖的原尾部 [r, len)，失败时精确还原。
                        let tail_t = out.t[r.min(out.t.len())..].to_vec();
                        let tail_v = out.v[r.min(out.v.len())..].to_vec();
                        undo[id] = Undo::Splice(r, tail_t, tail_v);
                        out.splice_tail_from(r, &Series { t, v }, offset);
                        out
                    }
                }
            };

            st.generation = generation;
            st.computed_len = n;
            st.watermark = Some(Watermark::from_input(in_t, n, up_rev));
            st.force_full = false;
            // 仅全量重算才提升修订号：增量只追加/改尾部，不改写已消费前缀，
            // 下游无需因普通 append 而退回全量。迟到(verdict=Full)必须提升，
            // 让下游 detect 到上游改写并跟随全量重算。
            if full {
                st.out_rev = st.out_rev.wrapping_add(1);
            }
            st.out = out;
        }

        if let Some(e) = failed {
            // 全事务回滚：所有已触碰节点按 Undo 逆序还原，标量回快照。
            self.recomputed_rows = recomputed_before;
            for (id, u) in undo.into_iter().enumerate() {
                let st = &mut self.states[id];
                match u {
                    Undo::None => continue,
                    Undo::Original(old) => {
                        if let Some(prev) = old { st.out = prev; }
                    }
                    Undo::Append(keep) => {
                        st.out.t.truncate(keep);
                        st.out.v.truncate(keep);
                    }
                    Undo::Splice(r, tail_t, tail_v) => {
                        st.out.t.truncate(r);
                        st.out.v.truncate(r);
                        st.out.t.extend_from_slice(&tail_t);
                        st.out.v.extend_from_slice(&tail_v);
                    }
                }
                let (g, len, wm, rev) = snap[id];
                st.generation = g;
                st.computed_len = len;
                st.watermark = wm;
                st.out_rev = rev;
                st.force_full = true; // watermark 已失效，下次强制全量恢复。
            }
            return Err(e);
        }
        Ok(())
    }
}

impl NodeState {
    fn default_state() -> Self {
        Self {
            generation: 0,
            computed_len: 0,
            out: Series::default(),
            watermark: None,
            force_full: false,
            out_rev: 0,
        }
    }
}

#[cfg(test)]
mod tests;
