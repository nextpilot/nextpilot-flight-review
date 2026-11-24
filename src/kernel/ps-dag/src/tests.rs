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
