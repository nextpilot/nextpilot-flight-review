use super::*;
use serde_json::json;
use std::collections::HashMap;

fn ch(name: &str, t: Vec<f64>, v: Vec<f64>) -> (String, Series) {
    (name.to_string(), Series { t, v })
}

fn channels_from(pairs: Vec<(String, Series)>) -> HashMap<String, Series> {
    pairs.into_iter().collect()
}

fn node(name: &str, filter: &str, params: Value, input: Source) -> Node {
    Node::new(name.to_string(), filter.to_string(), params, input)
}

// ---- 图结构：循环检测 + 拓扑序 ----

#[test]
fn cycle_is_detected_and_rejected() {
    let mut g = Graph::new();
    let a = g.add_node(node("a", "none", json!({}), Source::Channel("x".into())));
    let b = g.add_node(node("b", "absolute", json!({}), Source::Node(a)));
    let c = g.add_node(node("c", "scale", json!({}), Source::Node(b)));
    assert_eq!(g.add_edge(a, b), Ok(()));
    assert_eq!(g.add_edge(b, c), Ok(()));
    // c -> a 会成环，必须拒绝。
    assert_eq!(g.add_edge(c, a), Err(GraphError::Cycle));
    // 自环拒绝。
    assert_eq!(g.add_edge(a, a), Err(GraphError::Cycle));
    // 拓扑序仍有效且上游优先。
    let order = g.topological_order().unwrap();
    let pa = order.iter().position(|&x| x == a).unwrap();
    let pb = order.iter().position(|&x| x == b).unwrap();
    let pc = order.iter().position(|&x| x == c).unwrap();
    assert!(pa < pb && pb < pc, "order={:?}", order);
}

#[test]
fn duplicate_and_missing_edges() {
    let mut g = Graph::new();
    let a = g.add_node(node("a", "none", json!({}), Source::Channel("x".into())));
    let b = g.add_node(node("b", "none", json!({}), Source::Channel("x".into())));
    assert_eq!(g.add_edge(a, b), Ok(()));
    assert_eq!(g.add_edge(a, b), Err(GraphError::DuplicateEdge));
    assert_eq!(g.add_edge(0, 99), Err(GraphError::NodeNotFound(99)));
}

// ---- 全量求值：串联 + 并联与手工逐算子对拍 ----

#[test]
fn serial_and_parallel_matches_manual_ops() {
    // 输入通道 x。
    let t = vec![0.0, 1.0, 2.0, 3.0, 4.0];
    let v = vec![-2.0, -1.0, 3.0, 4.0, -5.0];
    let chans = channels_from(vec![ch("x", t.clone(), v.clone())]);

    let mut g = Graph::new();
    // 串联：x -> absolute -> scale
    let n1 = g.add_node(node("abs", "absolute", json!({}), Source::Channel("x".into())));
    let n2 = g.add_node(node("scale", "scale", json!({"value_scale": 2.0}), Source::Node(n1)));
    g.add_edge(n1, n2).unwrap();
    // 并联分发：x -> binary_filter（独立分支）
    let n3 = g.add_node(
        node("pos", "binary_filter", json!({"binary_op": 3, "binary_a": 0.0}), Source::Channel("x".into())),
    );

    let out = g.evaluate_full(&chans).unwrap();

    // 手工逐算子对拍。
    let (mt, mabs) = ps_ops::filters::apply("absolute", &t, &v, &json!({})).unwrap();
    let (_, mscale) = ps_ops::filters::apply("scale", &mt, &mabs, &json!({"value_scale": 2.0})).unwrap();
    let (_, mbin) = ps_ops::filters::apply(
        "binary_filter",
        &t,
        &v,
        &json!({"binary_op": 3, "binary_a": 0.0}),
    )
    .unwrap();

    assert_eq!(out[n1].v, mabs);
    assert_eq!(out[n2].v, mscale);
    assert_eq!(out[n3].v, mbin);
    assert_eq!(out[n2].t, mt);
}

#[test]
fn window_serial_matches_manual() {
    // 窗口算子串联：moving_average -> derivative，验证手工对拍。
    let t: Vec<f64> = (0..20).map(|i| i as f64 * 0.1).collect();
    let v: Vec<f64> = (0..20).map(|i| (i % 5) as f64).collect();
    let chans = channels_from(vec![ch("x", t.clone(), v.clone())]);

    let mut g = Graph::new();
    let n1 = g.add_node(node("ma", "moving_average", json!({"window": 3}), Source::Channel("x".into())));
    let n2 = g.add_node(node("der", "derivative", json!({}), Source::Node(n1)));
    g.add_edge(n1, n2).unwrap();
    let out = g.evaluate_full(&chans).unwrap();

    let (mt, mv) = ps_ops::filters::apply("moving_average", &t, &v, &json!({"window": 3})).unwrap();
    let (_, md) = ps_ops::filters::apply("derivative", &mt, &mv, &json!({})).unwrap();
    assert_eq!(out[n1].v, mv);
    assert_eq!(out[n2].v, md);
}

// ---- 增量续算：与全量逐位一致 ----

fn incremental_equals_full(filter: &str, params: Value, extra: Vec<f64>) {
    let t0: Vec<f64> = (0..40).map(|i| i as f64 * 0.01).collect();
    let v0: Vec<f64> = (0..40).map(|i| ((i * 7) % 11) as f64 - 5.0).collect();
    let t1: Vec<f64> = (40..40 + extra.len()).map(|i| i as f64 * 0.01).collect();

    let mut all_t = t0.clone();
    all_t.extend_from_slice(&t1);
    let mut all_v = v0.clone();
    all_v.extend_from_slice(&extra);
    let full = Graph::new_full_single(filter, &params, "x");
    let full_out = full.evaluate_full(&channels_from(vec![ch("x", all_t, all_v)])).unwrap();

    let mut eng = IncrementalEngine::new(Graph::new_full_single(filter, &params, "x"));
    eng.evaluate(1, &channels_from(vec![ch("x", t0.clone(), v0.clone())])).unwrap();
    let mut t_add = t0;
    let mut v_add = v0;
    t_add.extend_from_slice(&t1);
    v_add.extend_from_slice(&extra);
    eng.evaluate(2, &channels_from(vec![ch("x", t_add, v_add)])).unwrap();

    let inc = eng.output(0).unwrap();
    assert_eq!(inc.v.len(), full_out[0].v.len(), "{filter}: 长度不一致");
    for (i, (a, b)) in inc.v.iter().zip(full_out[0].v.iter()).enumerate() {
        assert!(
            (a - b).abs() <= 1e-9 || (a.is_nan() && b.is_nan()),
            "{filter}: 行 {i} 不一致 inc={a} full={b}"
        );
    }
    assert_eq!(inc.t.len(), full_out[0].t.len(), "{filter}: 时间轴长度不一致");
    for (i, (a, b)) in inc.t.iter().zip(full_out[0].t.iter()).enumerate() {
        assert!((a - b).abs() <= 1e-9, "{filter}: 时间行 {i} 不一致 inc={a} full={b}");
    }
}

impl Graph {
    fn new_full_single(filter: &str, params: &Value, channel: &str) -> Self {
        let mut g = Graph::new();
        g.add_node(node("n", filter, params.clone(), Source::Channel(channel.to_string())));
        g
    }
}

#[test]
fn incremental_stateless_matches_full() {
    incremental_equals_full("absolute", json!({}), vec![1.0, -2.0, 3.0, -4.0, 5.0]);
    incremental_equals_full("scale", json!({"value_scale": 3.0, "value_offset": 1.0}), vec![2.0, 4.0, 6.0]);
}

#[test]
fn incremental_window_matches_full() {
    incremental_equals_full("moving_average", json!({"window": 4}), vec![1.0, 2.0, 3.0, 4.0, 5.0]);
    incremental_equals_full("moving_rms", json!({"window": 5}), vec![2.0, -3.0, 4.0]);
}

#[test]
fn incremental_stateful_matches_full() {
    // 有状态算子走全量重算，仍须与全量逐位一致。
    incremental_equals_full("derivative", json!({}), vec![1.0, 2.0, 3.0]);
    incremental_equals_full("integral", json!({}), vec![1.0, 2.0, 3.0]);
    incremental_equals_full("normalize", json!({}), vec![10.0, -10.0, 3.0]);
}

#[test]
fn generation_unchanged_skips_recompute() {
    let t = vec![0.0, 1.0, 2.0];
    let v = vec![1.0, 2.0, 3.0];
    let chans = channels_from(vec![ch("x", t, v)]);
    let mut eng = IncrementalEngine::new(Graph::new_full_single("absolute", &json!({}), "x"));
    eng.evaluate(1, &chans).unwrap();
    let after_first = eng.recomputed_rows;
    assert_eq!(after_first, 3);
    // generation 不变，重复求值不重算。
    eng.evaluate(1, &chans).unwrap();
    assert_eq!(eng.recomputed_rows, after_first, "generation 未变不应重算");
}

#[test]
fn incremental_rows_scale_with_append() {
    // 追加 M 行，逐点算子 recomputed_rows 增量应为 M（线性），而非全量 N。
    let base: Vec<f64> = (0..1000).map(|i| i as f64).collect();
    let t0 = base.clone();
    let v0 = base.clone();
    let mut eng = IncrementalEngine::new(Graph::new_full_single("absolute", &json!({}), "x"));
    eng.evaluate(1, &channels_from(vec![ch("x", t0.clone(), v0.clone())])).unwrap();
    let before = eng.recomputed_rows;

    let m = 10usize;
    let mut t = t0;
    let mut v = v0;
    for i in 0..m {
        t.push((1000 + i) as f64);
        v.push((1000 + i) as f64);
    }
    eng.evaluate(2, &channels_from(vec![ch("x", t, v)])).unwrap();
    let delta = eng.recomputed_rows - before;
    assert_eq!(delta, m, "逐点算子应只重算新增 {m} 行，实际 {delta}");
    assert!(delta < 1000, "不应重算全量");
}

#[test]
fn missing_channel_reports_error() {
    let g = Graph::new_full_single("absolute", &json!({}), "nonexistent");
    let err = g.evaluate_full(&HashMap::new()).unwrap_err();
    assert_eq!(err, GraphError::ChannelMissing("nonexistent".into()));
}

// ---- BJ5：迟到输入检测 + 事务性回滚 ----

/// 断言两个 Series 逐位一致（NaN 视为相等）。
fn assert_series_bitwise(a: &Series, b: &Series, label: &str) {
    assert_eq!(a.t.len(), b.t.len(), "{label}: 时间轴长度 {} vs {}", a.t.len(), b.t.len());
    assert_eq!(a.v.len(), b.v.len(), "{label}: 值长度 {} vs {}", a.v.len(), b.v.len());
    for (i, (x, y)) in a.t.iter().zip(b.t.iter()).enumerate() {
        assert!((x - y).abs() <= 1e-9 || (x.is_nan() && y.is_nan()), "{label}: t[{i}] {x} vs {y}");
    }
    for (i, (x, y)) in a.v.iter().zip(b.v.iter()).enumerate() {
        assert!((x - y).abs() <= 1e-9 || (x.is_nan() && y.is_nan()), "{label}: v[{i}] {x} vs {y}");
    }
}

/// 中部 backfill 后（前缀被改写），增量输出必须与全量重算逐位一致。
/// 覆盖 Stateless / Window / Full 三种记忆跨度。
#[test]
fn late_backfill_matches_full_for_all_memory_kinds() {
    fn run(filter: &str, params: Value) {
        // 初次：时间 [0,10,20,30,40]，值随行号。
        let t0 = vec![0.0, 10.0, 20.0, 30.0, 40.0];
        let v0 = vec![1.0, 2.0, 3.0, 4.0, 5.0];
        let mut eng = IncrementalEngine::new(Graph::new_full_single(filter, &params, "x"));
        eng.evaluate(1, &channels_from(vec![ch("x", t0.clone(), v0.clone())])).unwrap();

        // 中部 backfill 两行（时间 15、25），前缀末行从 40 后移 → 迟到。
        let t1 = vec![0.0, 10.0, 15.0, 20.0, 25.0, 30.0, 40.0];
        let v1 = vec![1.0, 2.0, 99.0, 3.0, 88.0, 4.0, 5.0];
        eng.evaluate(2, &channels_from(vec![ch("x", t1.clone(), v1.clone())])).unwrap();

        let full = Graph::new_full_single(filter, &params, "x")
            .evaluate_full(&channels_from(vec![ch("x", t1, v1)]))
            .unwrap();
        assert_series_bitwise(eng.output(0).unwrap(), &full[0], filter);
    }
    run("absolute", json!({}));                       // Stateless
    run("moving_average", json!({"window": 3}));      // Window
    run("integral", json!({}));                        // Full
}

/// 迟到沿拓扑传播：上游全量重算后 out_rev 变化，下游必须跟着重算。
#[test]
fn late_input_propagates_to_downstream() {
    let params = json!({});
    let t0 = vec![0.0, 10.0, 20.0, 30.0];
    let v0 = vec![2.0, -3.0, 4.0, -5.0];

    let mut eng = IncrementalEngine::new(Graph::new());
    let a = eng.add_node(node("abs", "absolute", params.clone(), Source::Channel("x".into())));
    let b = eng.add_node(node("scale", "scale", json!({"value_scale": 2.0}), Source::Node(a)));
    eng.add_edge(a, b).unwrap();
    eng.evaluate(1, &channels_from(vec![ch("x", t0.clone(), v0.clone())])).unwrap();

    // 中部 backfill。
    let t1 = vec![0.0, 10.0, 15.0, 20.0, 30.0];
    let v1 = vec![2.0, -3.0, 7.0, 4.0, -5.0];
    let chans = channels_from(vec![ch("x", t1.clone(), v1.clone())]);
    eng.evaluate(2, &chans).unwrap();

    // 与全量串联对拍。
    let mut g = Graph::new();
    let ga = g.add_node(node("abs", "absolute", params.clone(), Source::Channel("x".into())));
    let gb = g.add_node(node("scale", "scale", json!({"value_scale": 2.0}), Source::Node(ga)));
    g.add_edge(ga, gb).unwrap();
    let full = g.evaluate_full(&chans).unwrap();
    assert_series_bitwise(eng.output(a).unwrap(), &full[a], "abs");
    assert_series_bitwise(eng.output(b).unwrap(), &full[b], "scale");
}

/// 上游纯追加（增量）不提升 out_rev，下游不应误退全量——守住 S8 增量契约。
#[test]
fn pure_append_does_not_force_downstream_full() {
    let t0 = vec![0.0, 10.0, 20.0];
    let v0 = vec![1.0, 2.0, 3.0];
    let mut eng = IncrementalEngine::new(Graph::new());
    let a = eng.add_node(node("abs", "absolute", json!({}), Source::Channel("x".into())));
    let b = eng.add_node(node("scale", "scale", json!({"value_scale": 2.0}), Source::Node(a)));
    eng.add_edge(a, b).unwrap();
    eng.evaluate(1, &channels_from(vec![ch("x", t0.clone(), v0.clone())])).unwrap();
    let base = eng.recomputed_rows;

    // 纯追加 3 行：上游 Stateless 增量 3 行，下游 Stateless 增量 3 行 = 6。
    let t1 = vec![0.0, 10.0, 20.0, 30.0, 40.0, 50.0];
    let v1 = vec![1.0, 2.0, 3.0, 4.0, 5.0, 6.0];
    eng.evaluate(2, &channels_from(vec![ch("x", t1, v1)])).unwrap();
    let delta = eng.recomputed_rows - base;
    assert_eq!(delta, 6, "纯追加下游应走增量 3+3=6，实际 {delta}（误退全量？）");
}

/// 算子失败时事务回滚：旧输出完好（不清空、不重复），下次全量恢复。
/// 用 expression 算子：首次合法表达式建立水位，之后注入非法表达式 + 迟到 → Full 失败。
#[test]
fn failed_recompute_rolls_back_and_recovers_full() {
    let t0 = vec![0.0, 10.0, 20.0, 30.0];
    let v0 = vec![1.0, 2.0, 3.0, 4.0];
    let good = json!({"expr": "x"});
    let mut eng = IncrementalEngine::new(Graph::new_full_single("expression", &good, "x"));
    eng.evaluate(1, &channels_from(vec![ch("x", t0.clone(), v0.clone())])).unwrap();
    let before = eng.output(0).unwrap().clone();

    // 注入非法表达式（缺 expr）+ 中部 backfill 触发 Full → apply 失败。
    eng.graph.nodes[0].params = json!({});
    let t1 = vec![0.0, 10.0, 15.0, 20.0, 30.0];
    let v1 = vec![1.0, 2.0, 9.0, 3.0, 4.0];
    let bad = channels_from(vec![ch("x", t1.clone(), v1.clone())]);
    assert!(eng.evaluate(2, &bad).is_err(), "非法表达式应导致算子错误");

    // 回滚：输出与失败前逐位一致，不清空、不重复。
    assert_series_bitwise(eng.output(0).unwrap(), &before, "rollback");

    // 修复表达式 + 同样输入重跑：force_full 强制全量，恢复成功且与全量一致。
    eng.graph.nodes[0].params = good.clone();
    eng.evaluate(3, &bad).unwrap();
    let full = Graph::new_full_single("expression", &good, "x")
        .evaluate_full(&bad).unwrap();
    assert_series_bitwise(eng.output(0).unwrap(), &full[0], "recover");
}

/// 多节点事务：上游成功、下游失败时，上游输出也必须回滚（全事务，不留半更新）。
#[test]
fn failed_downstream_rolls_back_upstream_too() {
    let t0 = vec![0.0, 10.0, 20.0];
    let v0 = vec![1.0, 2.0, 3.0];
    let mut eng = IncrementalEngine::new(Graph::new());
    let a = eng.add_node(node("abs", "absolute", json!({}), Source::Channel("x".into())));
    let b = eng.add_node(node("bad", "expression", json!({"expr": "x"}), Source::Node(a)));
    eng.add_edge(a, b).unwrap();
    eng.evaluate(1, &channels_from(vec![ch("x", t0.clone(), v0.clone())])).unwrap();
    let up_before = eng.output(a).unwrap().clone();
    let down_before = eng.output(b).unwrap().clone();

    // 下游注入非法表达式 + 中部 backfill → 上游成功全量、下游失败。
    eng.graph.nodes[b].params = json!({});
    let t1 = vec![0.0, 5.0, 10.0, 20.0];
    let v1 = vec![1.0, 8.0, 2.0, 3.0];
    assert!(eng.evaluate(2, &channels_from(vec![ch("x", t1, v1)])).is_err());

    assert_series_bitwise(eng.output(a).unwrap(), &up_before, "upstream rolled back");
    assert_series_bitwise(eng.output(b).unwrap(), &down_before, "downstream rolled back");
}

// ---- BJ6：gap 台账只做检测，不参与求值 ----

/// 上报缺口只影响台账，不改变求值输出（issue 范围约束：只加检测、不改求值）。
#[test]
fn gap_reporting_does_not_alter_evaluation() {
    let mut eng = IncrementalEngine::new(Graph::new());
    let a = eng.add_node(node("abs", "absolute", json!({}), Source::Channel("x".into())));
    let t = vec![0.0, 1.0, 2.0];
    let v = vec![-1.0, 2.0, -3.0];
    let input = channels_from(vec![ch("x", t, v)]);
    eng.evaluate(1, &input).unwrap();
    let baseline = eng.output(a).unwrap().clone();

    // 上游丢了段 5..=6：记缺口。
    let g = eng.report_gap("x", 5, 6, 1.0, 2.0).expect("应记录缺口");
    assert_eq!((g.from_id, g.to_id), (5, 6));
    assert_eq!(eng.gaps().len(), 1);

    // 同样输入再求值（generation 前进）：输出与基线逐位一致。
    eng.evaluate(2, &input).unwrap();
    assert_series_bitwise(eng.output(a).unwrap(), &baseline, "gap must not affect eval");
}

/// observe_segment 自动检测跳号，与显式上报共用同一台账。
#[test]
fn observe_segment_detects_jump_and_shares_ledger() {
    let mut eng = IncrementalEngine::new(Graph::new());
    assert!(eng.observe_segment("tf", 1, 0.0, 0.1).is_none());
    assert!(eng.observe_segment("tf", 2, 0.1, 0.2).is_none());
    let g = eng.observe_segment("tf", 5, 0.5, 0.6).expect("跳号应生成缺口");
    assert_eq!((g.from_id, g.to_id), (3, 4));
    assert_eq!(eng.gaps().len(), 1);
    assert_eq!(eng.gap_tracker().total(), 1);

    // 显式上报继续累积到同一台账。
    eng.report_gap("tf", 8, 9, 0.9, 1.0);
    assert_eq!(eng.gap_tracker().total(), 2);
    assert_eq!(eng.gaps().len(), 2);

    // 时间稀疏但段 id 连续：不产生新缺口（pj 的坑）。
    assert!(eng.observe_segment("tf", 10, 1e9, 1e9 + 1.0).is_none());
    assert_eq!(eng.gap_tracker().total(), 2);
}

// ---- BJ7 阶段一：MIMO 精确时间戳 join ----

fn mimo_channels() -> HashMap<String, Series> {
    channels_from(vec![
        ch("a", vec![0.0, 1.0, 2.0], vec![10.0, 11.0, 12.0]),
        ch("b", vec![1.0, 2.0, 4.0], vec![21.0, 22.0, 24.0]),
    ])
}

/// 只有两路时间戳都到齐才产出；与全量求值逐位一致。
#[test]
fn mimo_join_matches_full_and_requires_all_inputs() {
    let mut eng = IncrementalEngine::new(Graph::new());
    let m = eng.add_node(Node::new_multi(
        "m".into(),
        "join_equal".into(),
        json!({}),
        vec![Source::Channel("a".into()), Source::Channel("b".into())],
    ));
    let chs = mimo_channels();
    eng.evaluate(1, &chs).unwrap();
    let out = eng.output(m).unwrap();
    assert_eq!(out.t, vec![1.0, 2.0], "只有 1、2 两路同时到达");
    assert_eq!(out.v, vec![11.0, 12.0]);

    let full = eng.graph.evaluate_full(&chs).unwrap();
    assert_series_bitwise(eng.output(m).unwrap(), &full[m], "mimo vs full");
}

/// 输入未变（仅 generation 前进）时 MIMO 整节点跳过，不做无谓全量。
#[test]
fn mimo_skips_when_inputs_unchanged() {
    let mut eng = IncrementalEngine::new(Graph::new());
    eng.add_node(Node::new_multi(
        "m".into(),
        "join_equal".into(),
        json!({}),
        vec![Source::Channel("a".into()), Source::Channel("b".into())],
    ));
    let chs = mimo_channels();
    eng.evaluate(1, &chs).unwrap();
    let before = eng.recomputed_rows;
    eng.evaluate(2, &chs).unwrap();
    assert_eq!(eng.recomputed_rows, before, "输入未变应跳过 MIMO");
}

/// 任一路尾部追加 → MIMO 重算，且与全量一致（增量结果不残留旧形状）。
#[test]
fn mimo_recomputes_on_any_input_growth() {
    let mut eng = IncrementalEngine::new(Graph::new());
    let m = eng.add_node(Node::new_multi(
        "m".into(),
        "join_combine".into(),
        json!({"op": "add"}),
        vec![Source::Channel("a".into()), Source::Channel("b".into())],
    ));
    let chs = mimo_channels();
    eng.evaluate(1, &chs).unwrap();
    assert_eq!(eng.output(m).unwrap().v, vec![32.0, 34.0]);

    // b 追加 3.0：现在 2.0/3.0 都对齐（a 有 2.0，b 有 3.0？a 无 3.0）→ 仍只有 1、2。
    let chs2 = channels_from(vec![
        ch("a", vec![0.0, 1.0, 2.0, 3.0], vec![10.0, 11.0, 12.0, 13.0]),
        ch("b", vec![1.0, 2.0, 4.0], vec![21.0, 22.0, 24.0]),
    ]);
    eng.evaluate(2, &chs2).unwrap();
    assert_eq!(eng.output(m).unwrap().t, vec![1.0, 2.0]);
    assert_eq!(eng.output(m).unwrap().v, vec![32.0, 34.0]);

    // a、b 各自补上 3.0 → 三路对齐行出现。
    let chs3 = channels_from(vec![
        ch("a", vec![0.0, 1.0, 2.0, 3.0], vec![10.0, 11.0, 12.0, 13.0]),
        ch("b", vec![1.0, 2.0, 3.0, 4.0], vec![21.0, 22.0, 23.0, 24.0]),
    ]);
    eng.evaluate(3, &chs3).unwrap();
    assert_eq!(eng.output(m).unwrap().t, vec![1.0, 2.0, 3.0]);
    let full = eng.graph.evaluate_full(&chs3).unwrap();
    assert_series_bitwise(eng.output(m).unwrap(), &full[m], "mimo after growth");
}

/// MIMO 下游节点能跟随 join 输出的形状变化（走全量而不是残留旧尾部）。
#[test]
fn mimo_downstream_follows_shape_change() {
    let mut eng = IncrementalEngine::new(Graph::new());
    let m = eng.add_node(Node::new_multi(
        "m".into(),
        "join_equal".into(),
        json!({}),
        vec![Source::Channel("a".into()), Source::Channel("b".into())],
    ));
    let s = eng.add_node(node("s", "scale", json!({"value_scale": 10.0}), Source::Node(m)));
    eng.add_edge(m, s).unwrap();
    let chs = mimo_channels();
    eng.evaluate(1, &chs).unwrap();
    assert_eq!(eng.output(s).unwrap().v, vec![110.0, 120.0]);

    let chs2 = channels_from(vec![
        ch("a", vec![0.0, 1.0, 2.0], vec![10.0, 11.0, 12.0]),
        ch("b", vec![1.0, 2.0, 3.0], vec![21.0, 22.0, 23.0]),
    ]);
    eng.evaluate(2, &chs2).unwrap();
    assert_eq!(eng.output(s).unwrap().v, vec![110.0, 120.0]);
    let full = eng.graph.evaluate_full(&chs2).unwrap();
    assert_series_bitwise(eng.output(s).unwrap(), &full[s], "downstream of mimo");
}

/// 三路 MIMO 与显式加边写法等价。
#[test]
fn three_way_mimo_via_explicit_edges() {
    let mut eng = IncrementalEngine::new(Graph::new());
    let a = eng.add_node(node("a", "none", json!({}), Source::Channel("x".into())));
    let b = eng.add_node(node("b", "none", json!({}), Source::Channel("y".into())));
    let c = eng.add_node(node("c", "none", json!({}), Source::Channel("z".into())));
    let m = eng.add_node(Node::new_multi(
        "m".into(),
        "join_equal".into(),
        json!({}),
        vec![Source::Node(a), Source::Node(b), Source::Node(c)],
    ));
    eng.add_edge(a, m).unwrap();
    eng.add_edge(b, m).unwrap();
    eng.add_edge(c, m).unwrap();
    let chs = channels_from(vec![
        ch("x", vec![0.0, 1.0], vec![1.0, 1.0]),
        ch("y", vec![1.0, 2.0], vec![2.0, 2.0]),
        ch("z", vec![1.0, 3.0], vec![3.0, 3.0]),
    ]);
    eng.evaluate(1, &chs).unwrap();
    assert_eq!(eng.output(m).unwrap().t, vec![1.0]);
    let full = eng.graph.evaluate_full(&chs).unwrap();
    assert_series_bitwise(eng.output(m).unwrap(), &full[m], "3-way mimo");
}

/// MIMO 算子失败时走既有全事务回滚，旧输出完好。
#[test]
fn mimo_op_failure_rolls_back() {
    let mut eng = IncrementalEngine::new(Graph::new());
    let m = eng.add_node(Node::new_multi(
        "m".into(),
        "join_combine".into(),
        json!({"op": "add"}),
        vec![Source::Channel("a".into()), Source::Channel("b".into())],
    ));
    let chs = mimo_channels();
    eng.evaluate(1, &chs).unwrap();
    let before = eng.output(m).unwrap().clone();

    // 改成非法 op + 输入变化 → 重算失败（输入不变会被 skip，故必须动输入）。
    eng.graph.nodes[m].params = json!({"op": "bogus"});
    let chs2 = channels_from(vec![
        ch("a", vec![0.0, 1.0, 2.0, 5.0], vec![10.0, 11.0, 12.0, 15.0]),
        ch("b", vec![1.0, 2.0, 4.0], vec![21.0, 22.0, 24.0]),
    ]);
    assert!(eng.evaluate(2, &chs2).is_err());
    assert_series_bitwise(eng.output(m).unwrap(), &before, "mimo rolled back");
}

// ---- BJ7 阶段二：算子热替换 ----

/// 成功热替换：节点 id / 拓扑不变，输出与下游一起换新语义。
#[test]
fn hot_swap_success_replaces_output_in_place() {
    let mut eng = IncrementalEngine::new(Graph::new());
    let a = eng.add_node(node("a", "scale", json!({"value_scale": 2.0}), Source::Channel("x".into())));
    let b = eng.add_node(node("b", "none", json!({}), Source::Node(a)));
    eng.add_edge(a, b).unwrap();
    let chs = channels_from(vec![ch("x", vec![0.0, 1.0], vec![1.0, 2.0])]);
    eng.evaluate(1, &chs).unwrap();
    assert_eq!(eng.output(a).unwrap().v, vec![2.0, 4.0]);
    assert_eq!(eng.output(b).unwrap().v, vec![2.0, 4.0]);

    // 2x → 10x；force_full 保证即使输入未变也重算。
    eng.replace_node_op(a, "scale".into(), json!({"value_scale": 10.0})).unwrap();
    eng.evaluate(2, &chs).unwrap();
    assert_eq!(eng.output(a).unwrap().v, vec![10.0, 20.0]);
    assert_eq!(eng.output(b).unwrap().v, vec![10.0, 20.0], "下游跟随改写");
    assert_eq!(eng.graph.nodes.len(), 2, "节点数量与 id 不变");
}

/// 失败热替换：旧算子与旧输出都完好，重跑恢复旧语义。
#[test]
fn hot_swap_failure_restores_previous_op_and_output() {
    let mut eng = IncrementalEngine::new(Graph::new());
    let a = eng.add_node(node("a", "absolute", json!({}), Source::Channel("x".into())));
    let chs = channels_from(vec![ch("x", vec![0.0, 1.0], vec![-1.0, 2.0])]);
    eng.evaluate(1, &chs).unwrap();
    let before = eng.output(a).unwrap().clone();

    // 换成非法表达式（缺 expr）→ 求值失败。
    eng.replace_node_op(a, "expression".into(), json!({})).unwrap();
    assert!(eng.evaluate(2, &chs).is_err());

    assert_eq!(eng.graph.nodes[a].filter_id, "absolute", "旧算子已恢复");
    assert_series_bitwise(eng.output(a).unwrap(), &before, "旧输出完好");

    // 再跑一次：与全量逐位一致。
    eng.evaluate(3, &chs).unwrap();
    let full = eng.graph.evaluate_full(&chs).unwrap();
    assert_series_bitwise(eng.output(a).unwrap(), &full[a], "recovered");
}

/// 成功生效后暂存被清除：之后再换失败，退回的是上一个**生效**的算子。
#[test]
fn pending_swap_cleared_after_success() {
    let mut eng = IncrementalEngine::new(Graph::new());
    let a = eng.add_node(node("a", "absolute", json!({}), Source::Channel("x".into())));
    let chs = channels_from(vec![ch("x", vec![0.0], vec![-3.0])]);
    eng.evaluate(1, &chs).unwrap();

    // 第一次替换成功并提交。
    eng.replace_node_op(a, "scale".into(), json!({"value_scale": 10.0})).unwrap();
    eng.evaluate(2, &chs).unwrap();
    assert_eq!(eng.graph.nodes[a].filter_id, "scale");

    // 第二次替换失败 → 回到 scale(10)，不是最初的 absolute。
    eng.replace_node_op(a, "expression".into(), json!({})).unwrap();
    assert!(eng.evaluate(3, &chs).is_err());
    assert_eq!(eng.graph.nodes[a].filter_id, "scale");
    assert_eq!(eng.graph.nodes[a].params["value_scale"], json!(10.0));
    assert_eq!(eng.output(a).unwrap().v, vec![-30.0]);
}

/// MIMO 节点也能热替换（join_equal → join_count）。
#[test]
fn hot_swap_on_mimo_node() {
    let mut eng = IncrementalEngine::new(Graph::new());
    let m = eng.add_node(Node::new_multi(
        "m".into(),
        "join_equal".into(),
        json!({}),
        vec![Source::Channel("a".into()), Source::Channel("b".into())],
    ));
    let chs = mimo_channels();
    eng.evaluate(1, &chs).unwrap();
    assert_eq!(eng.output(m).unwrap().v, vec![11.0, 12.0]);

    eng.replace_node_op(m, "join_count".into(), json!({})).unwrap();
    eng.evaluate(2, &chs).unwrap();
    assert_eq!(eng.output(m).unwrap().v, vec![1.0, 1.0], "换成计数语义");
    assert_eq!(eng.output(m).unwrap().t, vec![1.0, 2.0]);
}

/// 越界节点 id 拒绝替换。
#[test]
fn hot_swap_rejects_unknown_node() {
    let mut eng = IncrementalEngine::new(Graph::new());
    let err = eng.replace_node_op(9, "none".into(), json!({})).unwrap_err();
    assert_eq!(err, GraphError::NodeNotFound(9));
}

// ---- BJ7 阶段三：时间分片回放 + 快照读屏障 ----

use std::time::Duration;

fn chain(n: usize) -> IncrementalEngine {
    let mut eng = IncrementalEngine::new(Graph::new());
    let root = eng.add_node(node(
        "n0",
        "scale",
        json!({"value_scale": 2.0}),
        Source::Channel("x".into()),
    ));
    let mut prev = root;
    for i in 1..n {
        let id = eng.add_node(node(
            &format!("n{i}"),
            "scale",
            json!({"value_scale": 2.0}),
            Source::Node(prev),
        ));
        eng.add_edge(prev, id).unwrap();
        prev = id;
    }
    eng
}

fn chain_input() -> HashMap<String, Series> {
    channels_from(vec![ch("x", vec![0.0, 1.0, 2.0], vec![1.0, 2.0, 3.0])])
}

/// 分片回放（每次 1 节点）与一次性 evaluate 逐位一致。
#[test]
fn staged_replay_matches_one_shot() {
    let chs = chain_input();
    let mut one = chain(5);
    one.evaluate(1, &chs).unwrap();

    let mut staged = chain(5);
    let mut guard = 0;
    loop {
        if staged.evaluate_step(1, &chs, Duration::ZERO).unwrap() {
            break;
        }
        guard += 1;
        assert!(guard < 50, "分片没有推进");
    }
    for i in 0..5 {
        assert_series_bitwise(
            staged.output(i).unwrap(),
            one.output(i).unwrap(),
            &format!("node{i}"),
        );
    }
}

/// budget=0 时每步只推进一个节点，进度可查询、可续跑。
#[test]
fn staged_replay_advances_one_node_per_zero_budget_step() {
    let chs = chain_input();
    let mut eng = chain(4);
    assert!(
        !eng.evaluate_step(1, &chs, Duration::ZERO).unwrap(),
        "4 个节点不可能一步完成"
    );
    assert!(eng.replay_pending(), "应留下未完成进度");

    eng.read_barrier(1, &chs).unwrap();
    assert!(!eng.replay_pending(), "barrier 后回放应完成");
}

/// 屏障：未完成时推完再返回，结果与一次性求值一致。
#[test]
fn read_barrier_completes_pending_replay() {
    let chs = chain_input();
    let mut one = chain(4);
    one.evaluate(1, &chs).unwrap();

    let mut staged = chain(4);
    assert!(!staged.evaluate_step(1, &chs, Duration::ZERO).unwrap());
    assert!(staged.replay_pending());
    staged.read_barrier(1, &chs).unwrap();
    assert!(!staged.replay_pending());
    for i in 0..4 {
        assert_series_bitwise(
            staged.output(i).unwrap(),
            one.output(i).unwrap(),
            &format!("barrier node{i}"),
        );
    }
}

/// 一次性 evaluate 会作废未完成的分片进度（两者不混用）。
#[test]
fn one_shot_evaluate_discards_pending_replay() {
    let chs = chain_input();
    let mut eng = chain(4);
    assert!(!eng.evaluate_step(1, &chs, Duration::ZERO).unwrap());
    assert!(eng.replay_pending());
    eng.evaluate(1, &chs).unwrap();
    assert!(!eng.replay_pending(), "一次性求值应清理分片进度");
    let full = eng.graph.evaluate_full(&chs).unwrap();
    for i in 0..4 {
        assert_series_bitwise(eng.output(i).unwrap(), &full[i], &format!("node{i}"));
    }
}

/// 分片模式下 MIMO 节点同样正确。
#[test]
fn staged_replay_handles_mimo() {
    let mut one = IncrementalEngine::new(Graph::new());
    let m1 = one.add_node(Node::new_multi(
        "m".into(),
        "join_combine".into(),
        json!({"op": "add"}),
        vec![Source::Channel("a".into()), Source::Channel("b".into())],
    ));
    one.evaluate(1, &mimo_channels()).unwrap();

    let mut staged = IncrementalEngine::new(Graph::new());
    let m2 = staged.add_node(Node::new_multi(
        "m".into(),
        "join_combine".into(),
        json!({"op": "add"}),
        vec![Source::Channel("a".into()), Source::Channel("b".into())],
    ));
    staged.read_barrier(1, &mimo_channels()).unwrap();
    assert_series_bitwise(
        staged.output(m2).unwrap(),
        one.output(m1).unwrap(),
        "staged mimo",
    );
}

/// 分片模式下算子失败：本节点输出不动，热替换算子被恢复。
#[test]
fn staged_replay_failure_restores_swap() {
    let mut eng = IncrementalEngine::new(Graph::new());
    let a = eng.add_node(node("a", "absolute", json!({}), Source::Channel("x".into())));
    let chs = chain_input();
    eng.evaluate(1, &chs).unwrap();
    let before = eng.output(a).unwrap().clone();

    eng.replace_node_op(a, "expression".into(), json!({})).unwrap();
    assert!(eng.evaluate_step(2, &chs, Duration::ZERO).is_err());
    assert_eq!(eng.graph.nodes[a].filter_id, "absolute", "算子已恢复");
    assert_series_bitwise(eng.output(a).unwrap(), &before, "输出未动");

    // 恢复后重跑成功。
    eng.evaluate(3, &chs).unwrap();
    let full = eng.graph.evaluate_full(&chs).unwrap();
    assert_series_bitwise(eng.output(a).unwrap(), &full[a], "recovered");
}

/// 分片第二轮回放走增量（Stateless 只算新增行），recomputed_rows 与一次性一致。
#[test]
fn staged_replay_is_incremental_on_second_run() {
    let mut one = chain(3);
    let mut staged = chain(3);
    let first = chain_input();
    one.evaluate(1, &first).unwrap();
    staged.read_barrier(1, &first).unwrap();

    let second = channels_from(vec![ch("x", vec![0.0, 1.0, 2.0, 3.0], vec![1.0, 2.0, 3.0, 4.0])]);
    let before = one.recomputed_rows;
    one.evaluate(2, &second).unwrap();
    let one_delta = one.recomputed_rows - before;

    let before = staged.recomputed_rows;
    staged.read_barrier(2, &second).unwrap();
    let staged_delta = staged.recomputed_rows - before;

    assert_eq!(one_delta, staged_delta, "分片与一次性重算行数应一致");
    for i in 0..3 {
        assert_series_bitwise(
            staged.output(i).unwrap(),
            one.output(i).unwrap(),
            &format!("incremental node{i}"),
        );
    }
}
