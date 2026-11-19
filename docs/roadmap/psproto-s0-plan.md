# psproto 施工方案：S0 起步

> 依据：`docs/roadmap/psproto-v2.md`（设计）+ 两条已定决策：
> **① 时间必须 i64；② `decodeSequential` 先保留。**

---

## 0. 这两条决定的连带影响（必须先说清）

### 0.1 "时间必须 i64" 撞上一个根问题

全链路现在这样走：

```
帧字节 → readElem() → double → evalExpr() → double → v *= 0.001 → v - base → double 秒 → ch.t.push_back(double)
```

`RtSeg.raw` 本身是 `std::vector<double>`（`SequentialCodec.cpp:708` 附近）。**也就是说时间戳在"读出来"那一步就已经降级了**——只把 `t` 改成 `int64_t` 而不管读取路径，等于把 double 里已经被抹掉精度的值套一个整数外壳，是自欺。

所以"时间必须 i64"不是改一个字段类型，而是要**让时间戳的取值路径绕开 double**。

### 0.2 "保留 `decodeSequential`" ≠ 零改动

保留的是**函数名、参数、返回类型名**；`ProtocolChannel` 内部字段语义变了（`t` 从 double 秒 → i64 ticks）。消费者必须跟着改——现有只有 **1 处**（`CustomProtocolParser.cpp:68`），代价很小，但要说清楚，避免误以为"调用处一行不用动"。

---

## 1. 时间 i64：三个方案的取舍

| 方案 | 做法 | 优点 | 缺点 | 结论 |
|---|---|---|---|---|
| **A. 全链路 typed** | `RtSeg.raw` 从 `vector<double>` 改成 `vector<Value>`（u8..u64/i/f） | 一步到位，无临时结构；S2 不用返工 | 牵动 codec 全部 1053 行读写路径 + 表达式引擎，一次性风险高；与"先堵崩溃"的优先级冲突 | 终态，**但不是第一步** |
| **B. 时间戳捷径**（推荐 S0） | 整型段读取时**额外**填一份 `rawI`（int64/uint64）；时间戳取值若命中整型段则走 i64，否则回退 double | 改动局限在 `readElem` 调用点 + timestamp 分支；S0 就能交付"时间精确" | 段内部出现临时双写（`raw` + `rawI`） | **S0 采用**，S2 收敛到 A |
| **C. 只换接口** | 只把 `t` 改 i64，读取仍走 double | 改动最小 | 精度照样丢，只是看起来对了 | **否掉**：这是自欺，会把问题藏进"看起来已修好" |

**B 的边界必须写进代码注释**：`rawI` 是**过渡结构**，只服务时间戳，S2 段表编译时随 `Value` 类型化一起删掉。不写这条注释，半年后就没人敢删它。

---

## 2. S0 改动清单（文件级）

| # | 文件 | 改动 | 为什么现在做 |
|---|---|---|---|
| 1 | `src/ProtocolDocument.cpp:426/428/430` | `std::stoul` → 新增 `parseU32Safe(text, out)`（前缀判进制 + 逐字符校验），失败写 `err` 返回 | **唯一会崩溃的点**：`"poly":"0xZZ"` 抛 `std::invalid_argument` 直接终止进程 |
| 2 | `include/protocol/SequentialCodec.h` | `ProtocolChannel::t` 由 `std::vector<double>` → `std::vector<std::int64_t> t;`，新增 `std::int64_t ticksPerSecond = 1000000;` | 兑现"时间必须 i64"，并显式暴露刻度换算 |
| 3 | `src/SequentialCodec.cpp` 段读取循环 | 整型段额外填 `RtSeg::rawI`（`std::vector<std::int64_t>`，`u64` 走 `uint64_t` 位模式） | 让时间戳取值不经过 double（方案 B） |
| 4 | `src/SequentialCodec.cpp` timestamp 分支（`:934-968`） | ① 命中整型段走 `rawI`；② `unit`（s/ms/us/**ns**）→ `ticksPerSecond`；③ `origin: first_frame` 用 **i64 相减**；④ `fallback: index` 用 `frameIndex * ticksPerSecond` | 顺序决定精度：先减后换算，不是先换算后减 |
| 5 | `extensions/common/CustomProtocolParser.cpp:68` | `times[i] = static_cast<int64_t>(first.t[i] * 1000000.0)` → `times[i] = first.t[i]`（当 `ticksPerSecond==1e6`） | 唯一消费者；改完反而更直接（`times` 本来就是 int64 微秒） |
| 6 | `tests/test_protocol.cpp` | `t` 相关断言改为 ticks；新增 1 条**纳秒精度**用例 | 现有断言是 double 语义，不改会假绿 |
| 7 | `tests/test_protocol_robust.cpp`（新增） | ① 畸形模板 20 份（含 `"poly":"0xZZ"`、负 size、超大 size）；② 随机字节 fuzz 500 轮；③ 断言：不抛异常、不崩、单轮 ≤ 50 ms | 把"强健"变成可验证事实；现有测试全走正确路径 |
| 8 | `tests/CMakeLists.txt` | 注册新测试 `ng_protocol_robust` | — |

**不做**（本轮）：`Diagnostics` 结构化（帧号+段名）改动面比上面 8 项加起来还大，单独一批；`maxsize` 强制、`count>256` 显式报错同理。

---

## 3. 顺序与依赖

```
① stoul 修复            ── 独立，先做（5 分钟，立刻消除崩溃）
      ↓
② 协议头改 t 为 i64      ── 独立改类型
      ↓
③ codec 填 rawI + ④ 时间戳走 i64   ── 必须一起改，否则编译过不了/语义错
      ↓
⑤ 消费者 + ⑥ 测试断言     ── 跟随 ②~④
      ↓
⑦⑧ fuzz 测试             ── 独立，最后加
```

每步**单独提交**，便于单步回滚。

---

## 4. 验收（可执行）

```bash
# 基线：改动前应为 10/10
QT_QPA_PLATFORM=offscreen ctest --test-dir build-ng

# 1) 崩溃点
#    模板里 "poly":"0xZZ" → 必须返回结构化错误，进程存活
# 2) 纳秒精度
#    构造 u64 时间戳 1700000000123456789 ns 的两帧，断言
#    (t[1]-t[0]) 与手算差值 ≤ 1（ticks=ns 时即 ≤1 ns）
#    反例检查：若实现是"先转 double 再减"，差值会是 256 的倍数 →
#    断言 256 的倍数出现即 FAIL（这条断言能把错实现钉死）
# 3) 零回归
#    f32/f64 通道的 v 值与改动前逐位相同；ULog/CSV 走全链路仍正常
# 4) fuzz
#    test_protocol_robust 500 轮随机字节：不崩、不超时
```

验收里第 2 条的"256 的倍数"断言是刻意设计的——它让"假装改成 i64 但内部还是 double"的实现**无法通过**。

---

## 5. 风险与回滚

| 风险 | 概率 | 处置 |
|---|---|---|
| `ticksPerSecond` 语义搞反（把 1e6 当成 1e-6）→ 时间轴差 1000 倍 | 中 | 验收断言用**具体数值**（`t[1]-t[0] == 1000000` 这类），不用"接近" |
| `rawI` 双写忘了在某条分支填 → 时间戳回退 double，静默降精 | 中 | 时间戳分支里若命中整型段但 `rawI` 为空，**直接报错**，不静默回退 |
| 消费者 `CustomProtocolParser` 改错导致 ULog 面板时间轴异常 | 低 | 该文件仅 1 行改动；用 `gui-shot` 出图对比 X 轴范围 |
| fuzz 暴露既有越界 | 中 | 这正是目的；暴露后按 P0 处理，不绕过 |

回滚：每步独立提交，`git revert <sha>` 单步生效，不影响其他步。

---

## 6. 明确不动

- `ng/core`、`ng/gui`、`ng/extensions` 的结构（除消费者那 1 行）；
- `decodeSequential` 的函数签名与 `decodeSequentialJson`、`previewSequentialFrame` 的签名；
- `Dataset` 的存储类型（`QVector<double>` 留着，S2 之后再说）；
- `src/`（老代码只读）。

---

## 7. S1–S5 顺序（便于排期，不在本轮）

| 批 | 内容 | 依赖 |
|---|---|---|
| S1 | `psproto.schema.json` + README + `size` 语义改字节（含 7 个 fixture 迁移） | 需先定 §11.2 的破坏性变更 |
| S2 | 段表编译 + `RtSeq.raw` 收敛为 typed（删掉 `rawI` 捷径）+ 校验按类型 + 诊断结构化 | 依赖 S0 的 i64 基础 |
| S3 | `StreamParser`（feedByte/feedChunk/finish），`decodeSequential` 内部改走它 | 依赖 S2 |
| S4a | 位域 / `until` / `text_number` / `derive` / 参数化 CRC 算法表 | 依赖 S2 |
| S4b | Lua 兜底（沙箱 + 配额 + 预编译） | 依赖 S4a 的挂点 |
| S5 | codegen（含脚本模板显式报错） | 依赖 S2 |
