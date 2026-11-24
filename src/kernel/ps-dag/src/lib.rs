//! ps-dag：派生序列依赖图编排（S7 / SCENE-08）。
//!
//! 节点 = 一次算子调用，边 = 数据依赖。建立在 S3 的 generation 变更通知之上：
//! append 只 bump generation，incremental 求值按各算子的「记忆跨度」回看尾部重算，
//! 保证与全量求值逐位一致，且重算量随增量行数线性增长（逐点 O(新增)、窗口 O(W+新增)）。
//!
//! 不修改 ps-ops 任何算子实现；本 crate 只做编排与增量调度。

use serde_json::Value;
use std::collections::{HashMap, HashSet, VecDeque};
use std::time::{Duration, Instant};

mod gap;
mod mimo;
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
        // 多输入 join：输出行数取决于各路时间戳交集，任一路尾部追加都可能改变结果
        // 形状，增量需要多路水位；宁可正确性优先，统一全量。
        "join" | "join_equal" | "join_combine" | "join_count" => Memory::Full,
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
///
/// `inputs.len() == 1` 是普通单输入节点；`> 1` 是 MIMO 节点，按时间戳精确 join
/// （见 `mimo` 模块）。单输入节点的行为与 BJ7 之前完全一致。
#[derive(Debug, Clone)]
pub struct Node {
    pub name: String,
    pub filter_id: String,
    pub params: Value,
    pub inputs: Vec<Source>,
    pub memory: Memory,
}

impl Node {
    /// 单输入节点（保持 BJ7 之前的签名）。
    pub fn new(name: String, filter_id: String, params: Value, input: Source) -> Self {
        let memory = memory_of(&filter_id, &params);
        Self { name, filter_id, params, inputs: vec![input], memory }
    }

    /// 多输入（MIMO）节点：`inputs` 至少 2 路，按时间戳精确对齐。
    pub fn new_multi(name: String, filter_id: String, params: Value, inputs: Vec<Source>) -> Self {
        let memory = memory_of(&filter_id, &params);
        Self { name, filter_id, params, inputs, memory }
    }

    /// 首路输入（MIMO 时是「主路」）。
    pub fn primary_input(&self) -> Option<&Source> {
        self.inputs.first()
    }

    pub fn is_mimo(&self) -> bool {
        self.inputs.len() > 1
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

    /// 有效依赖边 = 显式 add_edge + 节点 `Source::Node` 的隐式声明（去重）。
    /// 调用方可能只声明 inputs 而不显式加边，拓扑序必须认这些依赖。
    fn effective_edges(&self) -> Vec<(usize, usize)> {
        let mut e = self.edges.clone();
        for (id, node) in self.nodes.iter().enumerate() {
            for up in node.inputs.iter().filter_map(|s| match s {
                Source::Node(u) => Some(*u),
                Source::Channel(_) => None,
            }) {
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

    /// 节点的实际输入来源列表。
    ///
    /// 若有入边指向它，用入边上游**全部**覆盖声明的 inputs（单输入节点等价于原来的
    /// 「第一条入边覆盖」）；否则用节点自己声明的 inputs。MIMO 节点请用
    /// `Node::new_multi` 声明，或为每一路都加边。
    fn resolve_inputs(&self, id: usize) -> Vec<Source> {
        let froms: Vec<Source> = self
            .edges
            .iter()
            .filter(|&&(_, to)| to == id)
            .map(|&(from, _)| Source::Node(from))
            .collect();
        if froms.is_empty() {
            self.nodes[id].inputs.clone()
        } else {
            froms
        }
    }

    /// 收集求值所需通道名（未被边覆盖的 Channel 输入）。
    fn required_channels(&self) -> HashSet<String> {
        let mut s = HashSet::new();
        for id in 0..self.nodes.len() {
            for src in self.resolve_inputs(id) {
                if let Source::Channel(name) = src {
                    s.insert(name);
                }
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
            let sources = self.resolve_inputs(id);
            let inputs: Vec<&Series> = sources
                .iter()
                .map(|s| match s {
                    Source::Channel(name) => Ok(channels.get(name).unwrap()),
                    Source::Node(up) => outputs[*up].as_ref().ok_or(GraphError::NodeNotFound(*up)),
                })
                .collect::<Result<_, _>>()?;
            let series = if node.is_mimo() {
                mimo::apply_multi(&node.filter_id, &inputs, &node.params)
                    .map_err(GraphError::OpFailed)?
            } else {
                let input = inputs.first().ok_or(GraphError::NodeNotFound(id))?;
                let (t, v) =
                    ps_ops::filters::apply(&node.filter_id, &input.t, &input.v, &node.params)
                        .map_err(GraphError::OpFailed)?;
                Series { t, v }
            };
            outputs[id] = Some(series);
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
    /// 已消费输入水位（BJ5 迟到检测）。单输入节点长度为 1；MIMO 节点逐路一个。
    /// None 表示尚未消费。
    watermark: Option<Vec<Watermark>>,
    /// 上次失败回滚后置位：watermark 已失效，下次强制全量。
    force_full: bool,
    /// 输出修订号：每次成功重算 +1，下游据此检测上游输出改写。
    out_rev: u64,
    /// 热替换暂存的旧算子（filter_id, params）。本次求值整体成功才丢弃；
    /// 失败则用它把节点恢复原样，避免「换错算子把图弄坏」。
    pending_swap: Option<(String, Value)>,
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

/// 分片回放游标：记录本次 generation 的拓扑序与已推进位置。
#[derive(Debug, Clone)]
struct ReplayCursor {
    generation: u64,
    order: Vec<usize>,
    cursor: usize,
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
    /// 未完成的分片回放进度。
    replay: Option<ReplayCursor>,
}

impl IncrementalEngine {
    pub fn new(graph: Graph) -> Self {
        let states = graph.nodes.iter().map(|_| NodeState::default_state()).collect();
        Self { graph, states, recomputed_rows: 0, gaps: GapTracker::new(), replay: None }
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

    /// 算子热替换：保持节点 id、拓扑与下游引用不变，只换算子与参数。
    ///
    /// 旧算子暂存到 `pending_swap` 并把节点置 `force_full`；下次 `evaluate` **整体
    /// 成功**才丢弃暂存（算子正式生效），失败则自动恢复旧算子，输出由 Undo 日志
    /// 一并回滚。这样调参时「改错公式」不会把整张图弄坏。
    /// 连续替换多次时保留最初那份旧算子，失败即退回替换前状态。
    pub fn replace_node_op(
        &mut self,
        id: usize,
        filter_id: String,
        params: Value,
    ) -> Result<(), GraphError> {
        if id >= self.graph.nodes.len() {
            return Err(GraphError::NodeNotFound(id));
        }
        let node = &mut self.graph.nodes[id];
        let old_filter = std::mem::replace(&mut node.filter_id, filter_id);
        let old_params = std::mem::replace(&mut node.params, params);
        node.memory = memory_of(&node.filter_id, &node.params);
        let st = &mut self.states[id];
        if st.pending_swap.is_none() {
            st.pending_swap = Some((old_filter, old_params));
        }
        st.force_full = true;
        Ok(())
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
        // 一次性求值覆盖任何未完成的分片进度：两者不能混用同一个 generation。
        self.replay = None;
        for name in self.graph.required_channels() {
            if !channels.contains_key(&name) {
                return Err(GraphError::ChannelMissing(name));
            }
        }
        let order = self.graph.topological_order()?;
        let n_nodes = self.states.len();
        // 标量快照：回滚时恢复 generation / computed_len / watermark / out_rev。
        // 单输入节点水位长度为 1，MIMO 节点逐路一个。
        let snap: Vec<(u64, usize, Option<Vec<Watermark>>, u64)> = self
            .states
            .iter()
            .map(|s| (s.generation, s.computed_len, s.watermark.clone(), s.out_rev))
            .collect();
        let mut undo: Vec<Undo> = (0..n_nodes).map(|_| Undo::None).collect();
        let mut failed: Option<GraphError> = None;
        let recomputed_before = self.recomputed_rows;

        for id in order {
            if failed.is_some() {
                break;
            }
            let node = &self.graph.nodes[id];

            // ---- MIMO 节点：多路精确时间戳 join，记忆跨度恒为全量 ----
            // 全量而非增量：输出行数取决于各路时间戳交集，任一路尾部追加都可能改变
            // 结果形状，增量需要多路水位对齐，复杂度与收益不成比。正确性优先。
            if node.is_mimo() {
                let sources = self.graph.resolve_inputs(id);
                let computed: Result<(Option<Series>, Vec<Watermark>), String> = {
                    let mut ins: Vec<&Series> = Vec::with_capacity(sources.len());
                    for s in &sources {
                        match s {
                            Source::Channel(name) => ins.push(channels.get(name).unwrap()),
                            Source::Node(up) => ins.push(&self.states[*up].out),
                        }
                    }
                    let wms: Vec<Watermark> = sources
                        .iter()
                        .zip(ins.iter())
                        .map(|(src, s)| {
                            let up_rev = match src {
                                Source::Channel(_) => 0,
                                Source::Node(up) => self.states[*up].out_rev,
                            };
                            let n = s.t.len().min(s.v.len());
                            Watermark::from_input(&s.t, n, up_rev)
                        })
                        .collect();
                    let st = &self.states[id];
                    // 多路水位全等且未被要求强制全量：整节点跳过。
                    let skip = !st.force_full
                        && st
                            .watermark
                            .as_ref()
                            .is_some_and(|pw| pw.as_slice() == wms.as_slice());
                    if skip {
                        Ok((None, wms))
                    } else {
                        mimo::apply_multi(&node.filter_id, &ins, &node.params)
                            .map(|s| (Some(s), wms))
                    }
                };
                match computed {
                    Err(e) => {
                        failed = Some(GraphError::OpFailed(e));
                        break;
                    }
                    Ok((maybe_out, wms)) => {
                        let first_len = wms.first().map_or(0, |w| w.len);
                        let st = &mut self.states[id];
                        if let Some(new_out) = maybe_out {
                            self.recomputed_rows = self.recomputed_rows.saturating_add(first_len);
                            undo[id] = Undo::Original(Some(std::mem::take(&mut st.out)));
                            st.out = new_out;
                            // join 输出形状可能整体变化，下游必须跟随全量。
                            st.out_rev = st.out_rev.wrapping_add(1);
                        }
                        st.generation = generation;
                        st.computed_len = first_len;
                        st.watermark = Some(wms);
                        st.force_full = false;
                        st.pending_swap = None;
                        continue;
                    }
                }
            }

            let Some(source) = self.graph.resolve_inputs(id).into_iter().next() else {
                // 无任何输入：无从求值，跳过（正常图不会走到）。
                continue;
            };
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
            let verdict = classify(
                gen_changed,
                st.watermark.as_ref().and_then(|v| v.first().copied()),
                st.force_full,
                n,
                in_t_probe,
                up_rev,
            );
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
            st.watermark = Some(vec![Watermark::from_input(in_t, n, up_rev)]);
            st.force_full = false;
            st.pending_swap = None;
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
                // 换过算子的节点：本次求值没有整体成功 → 恢复旧算子（含 memory 派生）。
                if let Some((old_f, old_p)) = self.states[id].pending_swap.take() {
                    let node = &mut self.graph.nodes[id];
                    node.filter_id = old_f;
                    node.params = old_p;
                    node.memory = memory_of(&node.filter_id, &node.params);
                    self.states[id].force_full = true;
                }
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
                let (g, len, wm, rev) = snap[id].clone();
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

    /// 是否有未完成的分片回放。
    pub fn replay_pending(&self) -> bool {
        self.replay.is_some()
    }

    /// 分片回放：按耗时预算推进本 generation 的求值，返回是否已完成。
    ///
    /// 与 `evaluate` 的差别：**逐节点提交**——某节点失败只把该节点自身恢复原样
    /// （输出本就没写入），不撤销已提交的上游节点，从而可中断、可续跑。需要全局
    /// 原子性时用 `evaluate`。同一 generation 重复调用会从上次位置续跑；
    /// generation 变化则重新开始。至少推进一个节点后才检查预算。
    pub fn evaluate_step(
        &mut self,
        generation: u64,
        channels: &HashMap<String, Series>,
        budget: Duration,
    ) -> Result<bool, GraphError> {
        for name in self.graph.required_channels() {
            if !channels.contains_key(&name) {
                return Err(GraphError::ChannelMissing(name));
            }
        }
        let restart = self.replay.as_ref().map_or(true, |r| r.generation != generation);
        if restart {
            let order = self.graph.topological_order()?;
            self.replay = Some(ReplayCursor { generation, order, cursor: 0 });
        }
        let start = Instant::now();
        loop {
            let (done, id) = {
                let Some(r) = &self.replay else { return Ok(true) };
                (r.cursor >= r.order.len(), r.order.get(r.cursor).copied())
            };
            let Some(id) = id.filter(|_| !done) else {
                self.replay = None;
                return Ok(true);
            };
            self.step_one(id, generation, channels)?;
            if let Some(r) = &mut self.replay {
                r.cursor += 1;
            }
            if start.elapsed() >= budget {
                return Ok(false);
            }
        }
    }

    /// 快照读屏障：确保 generation 的回放已完成后再让调用方读取。
    ///
    /// 单线程库没有「别的线程在推进」可等，所以这里的「阻塞」= 自己把剩余节点推完
    /// 再返回（内部循环 step、不限预算）。若无待办回放则立即返回。
    pub fn read_barrier(
        &mut self,
        generation: u64,
        channels: &HashMap<String, Series>,
    ) -> Result<(), GraphError> {
        while !self.evaluate_step(generation, channels, Duration::from_secs(86_400))? {}
        Ok(())
    }

    /// 求值并立即提交单个节点（分片模式用；失败只把本节点恢复原样）。
    fn step_one(
        &mut self,
        id: usize,
        generation: u64,
        channels: &HashMap<String, Series>,
    ) -> Result<(), GraphError> {
        if id >= self.states.len() {
            return Ok(());
        }
        let sources = self.graph.resolve_inputs(id);
        // 先拷出算子信息：后面要可变借 states / graph，避免与 node 的不可变借冲突。
        let (filter_id, params, memory) = {
            let node = &self.graph.nodes[id];
            (node.filter_id.clone(), node.params.clone(), node.memory)
        };

        if self.graph.nodes[id].is_mimo() {
            let computed: Result<(Option<Series>, Vec<Watermark>, usize), String> = {
                let mut ins: Vec<&Series> = Vec::with_capacity(sources.len());
                for s in &sources {
                    match s {
                        Source::Channel(name) => ins.push(channels.get(name).unwrap()),
                        Source::Node(up) => ins.push(&self.states[*up].out),
                    }
                }
                let wms: Vec<Watermark> = sources
                    .iter()
                    .zip(ins.iter())
                    .map(|(src, s)| {
                        let up_rev = match src {
                            Source::Channel(_) => 0,
                            Source::Node(up) => self.states[*up].out_rev,
                        };
                        let n = s.t.len().min(s.v.len());
                        Watermark::from_input(&s.t, n, up_rev)
                    })
                    .collect();
                let st = &self.states[id];
                let first_len = wms.first().map_or(0, |w| w.len);
                let skip = !st.force_full
                    && st.watermark.as_ref().is_some_and(|pw| pw.as_slice() == wms.as_slice());
                if skip {
                    Ok((None, wms, first_len))
                } else {
                    mimo::apply_multi(&filter_id, &ins, &params).map(|s| (Some(s), wms, first_len))
                }
            };
            match computed {
                Err(e) => {
                    self.restore_pending_swap(id);
                    return Err(GraphError::OpFailed(e));
                }
                Ok((maybe_out, wms, first_len)) => {
                    let st = &mut self.states[id];
                    if let Some(new_out) = maybe_out {
                        self.recomputed_rows = self.recomputed_rows.saturating_add(first_len);
                        st.out = new_out;
                        st.out_rev = st.out_rev.wrapping_add(1);
                    }
                    st.generation = generation;
                    st.computed_len = first_len;
                    st.watermark = Some(wms);
                    st.force_full = false;
                    st.pending_swap = None;
                }
            }
            return Ok(());
        }

        let Some(source) = sources.into_iter().next() else { return Ok(()) };
        // 借用只在本块内存活：算出 owned 的新输出与水位后即释放对上游的引用。
        let (n, computed, full, wm, applied) = {
            let (before, at) = self.states.split_at_mut(id);
            let st = &at[0];
            let (n, up_rev, in_t, in_v): (usize, u64, &[f64], &[f64]) = match &source {
                Source::Channel(name) => {
                    let s = channels.get(name).unwrap();
                    let n = s.t.len().min(s.v.len());
                    (n, 0, &s.t[..n], &s.v[..n])
                }
                Source::Node(up) => {
                    let s = &before[*up].out;
                    let n = s.t.len().min(s.v.len());
                    (n, before[*up].out_rev, &s.t[..n], &s.v[..n])
                }
            };
            let verdict = classify(
                generation > st.generation,
                st.watermark.as_ref().and_then(|v| v.first().copied()),
                st.force_full,
                n,
                in_t,
                up_rev,
            );
            if verdict == Verdict::Skip {
                return Ok(());
            }
            let computed = st.computed_len;
            let full = verdict == Verdict::Full || memory == Memory::Full;
            let applied = if full {
                ps_ops::filters::apply(&filter_id, in_t, in_v, &params)
            } else {
                match memory {
                    Memory::Stateless => ps_ops::filters::apply(
                        &filter_id,
                        &in_t[computed..n],
                        &in_v[computed..n],
                        &params,
                    ),
                    Memory::Window(w) => {
                        let pad = w.saturating_sub(1);
                        let r = computed.saturating_sub(pad);
                        let s = r.saturating_sub(pad);
                        ps_ops::filters::apply(&filter_id, &in_t[s..n], &in_v[s..n], &params)
                    }
                    Memory::Full => unreachable!("full 分支已覆盖"),
                }
            };
            (
                n,
                computed,
                full,
                Watermark::from_input(in_t, n, up_rev),
                applied,
            )
        };

        let applied = match applied {
            Ok((t, v)) => Series { t, v },
            Err(e) => {
                // 逐节点提交：本节点输出尚未写入，只需恢复未提交的热替换算子。
                self.restore_pending_swap(id);
                self.states[id].force_full = true;
                return Err(GraphError::OpFailed(e));
            }
        };

        self.recomputed_rows = self.recomputed_rows.saturating_add(match memory {
            Memory::Stateless => n.saturating_sub(computed),
            Memory::Window(w) => (w.saturating_sub(1)).min(computed) + n.saturating_sub(computed),
            Memory::Full => n,
        });

        let st = &mut self.states[id];
        if full {
            st.out = applied;
        } else {
            match memory {
                Memory::Stateless => {
                    let mut o = std::mem::take(&mut st.out);
                    o.append(&applied);
                    st.out = o;
                }
                Memory::Window(w) => {
                    let pad = w.saturating_sub(1);
                    let r = computed.saturating_sub(pad);
                    let s = r.saturating_sub(pad);
                    let mut o = std::mem::take(&mut st.out);
                    o.splice_tail_from(r, &applied, r - s);
                    st.out = o;
                }
                Memory::Full => unreachable!("full 分支已覆盖"),
            }
        }
        st.generation = generation;
        st.computed_len = n;
        st.watermark = Some(vec![wm]);
        st.force_full = false;
        st.pending_swap = None;
        if full {
            st.out_rev = st.out_rev.wrapping_add(1);
        }
        Ok(())
    }

    /// 把未提交的热替换算子恢复成旧值（含 memory 派生）。
    fn restore_pending_swap(&mut self, id: usize) {
        if let Some((old_f, old_p)) = self.states[id].pending_swap.take() {
            let node = &mut self.graph.nodes[id];
            node.filter_id = old_f;
            node.params = old_p;
            node.memory = memory_of(&node.filter_id, &node.params);
        }
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
            pending_swap: None,
        }
    }
}

#[cfg(test)]
mod tests;
