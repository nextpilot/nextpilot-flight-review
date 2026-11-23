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
