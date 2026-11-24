# 数据层性能基线与回归守护

> SCENE-02 / S8 交付。S1（时间轴 us→ns）落地后建立的"纳秒版"基线，作为后续
> live 追加（S3）、列压缩（S5）等改动的回归对照。与 M2.7 验收基准
> `bench_m27.rs` 并存，互不干扰。

## 跑基准

在 `src/kernel/` 下执行：

```bash
# 只看数字（不判定）
cargo bench -p ps-abi --bench bench_dataset --

# 采集基线（机器相关，不入库，存到你自己的位置）
cargo bench -p ps-abi --bench bench_dataset -- --save-baseline /path/to/baseline.json

# 与基线对比判定（默认 p50 + 10% + 5us 容差）
cargo bench -p ps-abi --bench bench_dataset -- --baseline /path/to/baseline.json
```

参数：

| 参数 | 默认 | 说明 |
|---|---|---|
| `--save-baseline <path>` | — | 把本次结果存为基线 JSON |
| `--baseline <path>` | — | 读取基线并判定，任一指标回归则退出码 1 |
| `--threshold T` | `0.10` | 相对劣化阈值（10%） |
| `--min-abs US` | `5.0` | 绝对劣化容差（微秒），与阈值同时满足才判失败 |
| `--metric p50\|p99` | `p50` | 用哪个指标做判定；表格两个指标的变化都会打印 |

注意：cargo bench 的工作目录是 crate 根目录 `ps-abi/`，路径参数据此解析。

## 指标

全部在 **100 万行 F64 列、纳秒时间轴、100Hz 递增** 上测，每类多轮取 p50/p99。

| 指标 | 含义 | 复杂度/口径 |
|---|---|---|
| `lod_build` | LOD 金字塔首帧建塔 | O(n)，一次性；通过测试钩子失效旧塔后测 |
| `lod_hit` | LOD 金字塔命中查询 | O(px·log₄n)，稳态最高频路径 |
| `window_read` | `readWindow` 有界读 | cap=4096，等步长抽稀 |
| `column_raw` | `columnRaw` 整列借出 + memcpy | 零拷贝借出内部缓冲 |
| `value_at` | 随机行号访问 | 逐元素 FFI，每轮 1000 次取均值 |
| `append_batch` | 批追加吞吐（批内有序） | 每批 5 万行 × 20 批，含增量建塔；走零开销直通 |
| `append_batch_unordered` | 乱序批追加吞吐（S2 reorder） | 同上但批内时间逆序，度量入口 stable_sort 成本 |

单位统一为微秒（us）。`append_batch` 与 `append_batch_unordered` 对拍可量化 reorder
开销：批内有序时检测为 None、零开销直通；乱序时才付 O(k log k) 稳定排序。

## 判据设计

判定同时要求「相对劣化 > threshold」**且**「绝对劣化 > min-abs」，两个原因都来自实测：

1. **默认用 p50 而非 p99**：p50 在共享/虚拟环境稳定（同代码连跑波动 <1%）；
   p99 尾部延迟对 CPU 调度极敏感（实测波动可达 ±25%），只适合专用稳定机器，
   用 `--metric p99` 启用。
2. **加绝对容差**：`window_read`/`value_at` 这类微秒级小指标，4us 的抖动就是
   +16%，纯百分比会误报。`--min-abs 5us` 让它们不被噪声放大。

**使用约束**：基线必须在专用、负载稳定的机器采集，对比也在同环境跑。共享 CI
沙箱的 CPU 调度抖动会让时延波动 10%+，导致误报；这类环境应放宽阈值/容差，
或仅用本基准观察趋势，不作硬门禁。基线文件机器相关，**不入库**，由使用方在
自己的 CI 环境采集维护。

## CI 接入

本仓库当前没有自有的 CI 流水线配置。接入方式：在目标 CI 里跑同一条命令即可，

```bash
cd src/kernel
cargo bench -p ps-abi --bench bench_dataset -- --baseline <受管基线路径>
```

退出码非 0 即回归。基线文件由 CI 侧在稳定环境采集后作为受管产物保存，不提交到
仓库。S3/S5 等改动合入前后各跑一次，对比报告数字。

## 参考基线（沙箱环境，仅供趋势）

以下数字采集于云沙箱（2 vCPU，共享，软件环境），**绝对值不可跨机器比**，仅记录
本次 S8 落地的量级供参考：

| 指标 | p50(us) | p99(us) |
|---|---:|---:|
| lod_build | ~5,500 | ~7,000 |
| lod_hit | ~950 | ~1,500 |
| window_read | ~13 | ~60 |
| column_raw | ~300 | ~1,700 |
| value_at | ~0.12 | ~0.17 |
| append_batch | ~275 | ~3,500 |
| append_batch_unordered | ~264 | ~3,300 |

`append_batch_unordered` 与有序路径 p50 差异在噪声范围内（<10%），符合 S2 验收：
乱序 reorder 的相对追加吞吐下降 <10%；且有序追加完全不受影响（走零开销直通）。

注：本环境 `column_raw` p50 在 265~308us 间波动、`lod_build` 在 5.3~6.0ms 间波动，
是共享 CPU 噪声，不是代码退化。

## S5 压缩列 LOD 对拍

同环境、同 100 万行低基数 F64 值（周期 1009）下，原生列与 Dictionary 编码列各跑
200 轮 LOD 命中查询、10 轮首帧建塔，取 p50：

| 指标 | 原生(us) | 编码(us) | 变化 |
|---|---:|---:|---:|
| LOD 命中查询 | 1498.80 | 1498.72 | -0.0% |
| LOD 首帧建塔 | 6661.72 | 10014.42 | +50.3% |

命中查询几乎不变：LOD 塔是预计算 min/max，稳态查询不触碰原始列。建塔一次性逐行
解码，低基数数值列经 Dictionary 索引 + 字典取值，成本高于原生直读；该开销只在首帧
或塔失效时发生。主通道 F64 不调用编码时走原生路径，不受影响。

## S7 增量派生 vs 全量重算对拍

同环境、逐点算子（absolute），基准 100 万行、每轮追加 1000 行、30 轮，black_box
消费结果防优化，分别测「增量求值（只算新增尾部）」与「全量求值（重算整条）」p50：

| 指标 | p50(us) | 复杂度 |
|---|---:|---|
| 增量求值 | 12.73 | O(新增行数) |
| 全量求值 | 3792.35 | O(总行数) |
| 加速比 | 297.9x | — |

增量侧按算子记忆跨度续算：逐点算子只算新增行、窗口算子回看 W 行、有状态算子
全量重算；与全量结果逐位一致（含时间轴）由单测覆盖。耗时随增量行数线性增长，
而不是随数据集总长增长。generation 未变时整节点跳过；窗口/有状态算子的成本
见 ps-dag 单测，不在本基准口径内。

## BJ1 对象时间索引 vs 朴素线性插入（借鉴 pj ObjectStore）

对象索引（`ps_ndf::object_index`，BJ1）用 `BTreeMap<(time_ns, uid), handle>` 定位，
对比"Vec 按时间有序 + 乱序时 `Vec::insert` 挪动尾部"的朴素写法（pj 踩坑做法）。
同环境、release、随机时间戳、结果按时间序逐位一致：

| 场景 | N | BTreeMap 索引 | 朴素线性挪动 | 比值 |
|---|---:|---:|---:|---:|
| mostly-unsorted（随机时间，pj 坑场景） | 50,000 | 8.83ms | 81.12ms | 索引快 9.18x |
| 纯递增（有序追加） | 200,000 | 26.51ms | 5.30ms（Vec push） | 索引慢 5.0x |

**口径纠正**：issue 初稿写"1% 微回退"场景，实测证伪——回退幅度 50 远小于步长 1000，
属近似有序，朴素线性每次仅挪 1 元素，不构成 O(n²)。故改用真随机时间戳复现 pj 的坑。

**权衡说明**：BTreeMap 在乱序场景避免 O(n²) 退化，但在纯有序追加场景比 Vec push 慢
（节点分配开销）。对象索引服务于图像/点云/TF 等低频对象（每秒几十~几百条），MCAP
场景 chunk 解压本身毫秒级，索引微秒级差异非瓶颈；标量主数据仍走单调 `typed::Table`
完全不受影响。基准脚本为临时产物（`/tmp`，未入库），数字供趋势参考。

## BJ2 对象保留预算跳跃驱逐 vs 逐条（借鉴 pj RetentionJump）

`ps_ndf::object_index` 按 per-topic 预算（时间窗 / 条目数，0=off）驱逐过期前缀，
用 `BTreeMap::split_off` 一次切开 + 同步移除 `by_uid`（保持到达序游标一致）。
对比"逐条 pop_first"（pj 逐条驱逐 1.3s 的等价写法）。release、100k 条目逐 99990 条：

| 指标 | 耗时 | 说明 |
|---|---:|---|
| 跳跃驱逐（本单元） | 7.76ms | split_off 一次切 + 同步 by_uid |
| 逐条 pop_first | 2.92ms | 等价 BTreeMap 单结构 |
| 达标线 | <20ms | 对齐 pj RetentionJump 的 8ms |

跳跃驱逐绝对值达标（<20ms，与 pj 实测 8ms 同量级）。逐条对照只操作单一 BTreeMap，
而本单元还需同步维护 `by_uid` 到达序索引（乱序下被逐条目 UID 不连续，无法整段切），
故比值 0.4x 系结构差异而非实现缺陷；绝对耗时是验收口径。基准脚本为临时产物（`/tmp`，未入库）。

## BJ3 对象读取语义 + 分池命中率（借鉴 pj ResidentPayloadPool / 读语义）

`ps_ndf::object_read` 提供 warm latestAt（UID 键控缓存）、时间窗高水位
（`max_uid_at_or_before`，取 uid 最大而非时间最大）、迟到检测、内容身份
（content/rewrite/removal 三计数器）；`ResidentPool` 按 payload 大小分 small/large
双 FIFO + small 保留份额。分池命中率复刻 pj 场景（等比缩小：5000B 大点云 vs
200B 小 TF，260k 预算，1000 个小 TF 先入、600 个大点云涌入）：

| 策略 | 小 TF 存活率 |
|---|---:|
| 单 FIFO 无分池（pj 改造前） | 0.0%（0/1000） |
| 分池 + 210k small 保留份额 | 100.0%（1000/1000） |

对应 pj TECHNICAL_NOTES:50-54 的真实事故：单一 FIFO 池被点云挤爆致 32% TF 读取 miss。
本实现的分池使小对象在大对象涌入时 100% 存活。

**测试口径纠正（如实）**：首次 benchmark 误把"大对象"设为 600B（< 4096 small 阈值），
导致大对象也进 small 池、对照两组均 0%（假阴性）；改用 5000B（> 阈值）后得到上表正确结果。

**边界**：pj 的 `pushLazy(fetch: LazyCallback)` 惰性解压回调（C++ std::function）不跨
C ABI 传闭包；MCAP chunk 解压由上层 parser 在读取时驱动，本层提供零拷贝视图 +
warm cache + 读语义原语。基准脚本为临时产物（`/tmp`，未入库）。

## BJ4 乱序补传段式缓冲 vs 逐批整表重建（借鉴 pj 重叠 chunk + 读时归并）

`ps_ndf::segments` + `Table.pending` 让乱序补传写入只付批大小成本：批内排序后
push 一个 `PendingSegment`（段内按 `time_ns` 单调），到达阈值或读取前由
`Table::materialize` 用 k 路小顶堆按 `(time_ns, seq)` 归并回物理主表。主表始终
代表完整有序数据，故按行号直连 ABI、裸缓冲借出、LOD 全局行锚定全部不变。

批量补传基准（`bench_backfill_batched_vs_per_batch_merge`）：主表 5 万行，分
20 批各 1000 行补到表中部，多轮复测：

| 策略 | 20 批总耗时 | 加速比 |
|---|---:|---:|
| 逐批立即整表归并（旧 O(N)/批） | ~195ms | 1.0x |
| 攒段最后归并一次（O(m)/批 + O(N)） | ~19.8ms | ~9.5x |

两条路径输出逐位一致（时间轴、列值），加速比稳定在 9.5–9.9x。收益来自
"批量补传 → 单次渲染"场景：O(N) 重建只在读取前付一次，而非每批都付。
段数上限 64，达到即强制归并，避免段数无界拖慢单次归并。

物化时机（BJ4-fix / BJ4a）：除读取入口外，`Table::add_column`（schema 变更）、`ps_dataset_table_rows`、
派生输入（`apply_filter` / `apply_quat_rpy`）、`ps_dataset_column_encode` 也会先归并 pending。
这几条都是低频路径，各付一次 O(N)，换来不变量「pending 非空 ⇒ 段内列数与主表列数一致」——
否则加列后按新列数遍历旧段数组会越界（跨 C ABI panic 不可 unwind，直接 abort 宿主进程）。

**BJ4a 收口**：上述「每个入口记得调」的纪律已下沉为编译期强制——ps-ndf 表数据字段私有化，
`MatRef` 读守卫是数据视图唯一来源（构造即先归并），ABI 层直访表数据编译失败。
性能不受影响：守卫只是借用的类型包装，物化动作与 BJ4-fix 时完全一致（实测首读 15.9ms）。

**边界**：写入进段后若立刻按行号/裸缓冲读取，读取入口会先归并，此时仍付
一次 O(N)（与旧行为一致，换来了对外语义完全不变）。真正的收益在连续多批
补传中间不读的场景——MCAP/rosbag 批量导入后渲染正是此形态。逐条 backfill
且每条后紧跟读取时，段式不优于逐批归并。基准为 `ps-ndf` 单元测试内可复现
计时，非独立 criterion bench。
