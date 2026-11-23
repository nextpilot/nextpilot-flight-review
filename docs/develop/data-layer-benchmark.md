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
