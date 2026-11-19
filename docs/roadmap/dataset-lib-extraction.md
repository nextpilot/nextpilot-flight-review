# dataset 抽库设计：把数据地基从 `ng/core` 下沉为可复用 `ng/libs/dataset`

> 状态：**架构设计，待需求方拍板**（2026-11-19）。本轮只出边界与契约，**不写实现代码**。
> 需求方诉求原文："把 core 中的 dataset 抽象成一个 libs，这样其他地方可以复用。"
> 定位：与 [`modules-organ-completion.md`](modules-organ-completion.md) 配套的 core 层瘦身动作；不改动 source/dataset 的模块拆分裁决。
> 所有结论均逐行核对过 `ng/core`、`ng/CMakeLists.txt`、`ng/tests`。

---

## 0. 一句话结论

dataset 抽库**技术上可行且边界干净**——`Dataset` 只依赖 Rust ABI + Qt Core，不碰 core 任何业务类型。但有一个必须先解决的硬约束：**`Result.h` 的归属**（§3），不解决就会要么污染所有消费者、要么形成 `core ↔ dataset` 循环依赖。命名空间的统一（§4）建议与抽库**解耦**、另批做，避免风险叠加。

---

## 1. 现状与复用诉求（证据）

**现状**：`Dataset` 与 `DataRepository` 都编译进 `ng_core`（`ng/core/CMakeLists.txt`：两者 `.cpp` 在 `add_library(ng_core STATIC ...)` 里，`ng_core` 链接 `Qt6::Core` + `ng_kernel`）。任何想用数据模型的人必须链接整个 `ng_core`——连带 `AppContext`、`ExtensionRegistry`、`IngestPipeline`、全套 SPI 头。

**复用诉求是真实的**，最直接的受害者就在仓库里：

> `ng/tests/test_fastplot_dataset.cpp` 只想验证 "fastplot × Rust LOD" 这条链，却 `#include "DataRepository.h"` / `"IngestPipeline.h"` / `"ParserRegistry.h"`，链接整个 `ng_core`。`fastplot` 明明是独立库，却被 `ng_core` 拖住——这正是"抽库后可只链 dataset"的收益点。

此外 `cli_main.cpp`、各 `FormatDriver`、`bench_gui.cpp`、`shot_gui.cpp` 都直接依赖数据模型。抽成独立库后，这些消费者（以及未来的 toolbox extension、离线分析工具）都能**按需链接数据地基**，而不是被迫吞下整个骨架。

---

## 2. 抽库边界：进 / 不进

| 进 `ng/libs/dataset` | 不进（留在 core 或各自归属） | 理由 |
|---|---|---|
| `Dataset.h/.cpp` | `DataSource.h` / `SourceRegistry.h`（改名后） | 前者是数据模型，后者是源会话登记，跟 §1 的 `mod.source` 走 |
| `DataRepository.h/.cpp` | `IngestPipeline.h/.cpp`、`IngestOrchestrator.h/.cpp` | 摄取管道依赖源 + parser + 仓库，是**组装层**，不是数据模型；留 core/host |
| — | `DecodedFrame.h` | 头注释明确与 `Dataset` "刻意分开"：人类可读单帧 vs 数值批，互不转换 |
| — | `WorkspaceDataConfig.h` | 属 `SettingsStore` 的 Workspace 作用域，是"配方"不是数据本体 |

**依赖核对（抽库的干净度）**：`Dataset.h` 只 include `ps_kernel.h` + Qt（`QJsonObject/QSharedDataPointer/QString/QVector`）；`Dataset.cpp` 只用 `QFileInfo/QJsonDocument/QJsonObject` + 标准库。**`Dataset` 自身完全不依赖 core 的任何业务类型**——这正是可独立成库的技术依据。`DataRepository.h` 额外依赖 `Dataset.h` + `Result.h`（唯一瑕疵，见 §3）。

依赖图（目标态）：

```
ng_kernel (INTERFACE, Rust ps-abi 静态库)
    ▲
    │ PUBLIC
ng_dataset (STATIC: Dataset.cpp + DataRepository.cpp) ──PUBLIC──▶ ng_foundation (INTERFACE, Result.h)
    ▲                                                                   ▲
    │ PUBLIC                                                            │ PUBLIC
ng_core (去掉 Dataset/DataRepository 源文件) ────────────────────────────┘
    ▲
    └── host / modules / gui / app（消费者）
```

---

## 3. 硬约束：`Result.h` 必须下沉（否则循环或污染）

**问题**：`DataRepository::addWithId/addWithStableId` 返回 `Result<DatasetId>`，所以 `ng_dataset` 需要 `Result.h`。但 `Result.h` 现状在 `ng/core/include/`，被 **21 处**引用，其中 `ThemeManager.h`、`ModuleHost.h`、`PropertySchema.h` 等**完全不碰数据**。

| 方案 | 后果 | 裁决 |
|---|---|---|
| `Result.h` 随 dataset 进库 | 改主题也要依赖 dataset 库——污染所有消费者，且 dataset 库名实不符 | ❌ 否决 |
| `Result.h` 留在 core | `ng_dataset → ng_core`（取 Result）+ `ng_core → ng_dataset`（取 Dataset）= **CMake 循环依赖** | ❌ 否决 |
| **下沉为 `ng_foundation`（INTERFACE，header-only）** | dataset 与 core 都 PUBLIC 依赖 foundation；无循环、无污染 | ✅ **采纳** |

**`ng_foundation` 说明**：只放 `Result.h`（纯模板 + `<QString>`，无 `.cpp`、无链接产物），是 `INTERFACE` 库，仅提供 include 路径。**它不是"又多一个编译目标"，只是把公共头的物理归属归位**。

- `Result.h` 物理移到 `ng/libs/foundation/include/Result.h`，命名空间 `nextpilot::plotscope::core` **保持不变**（见 §4 同理）。
- 21 处 `#include "Result.h"` **无需改字面量**——只要 `ng_foundation` 的 include 目录进入编译路径，且 `core/include/` 下不再留有同名文件（物理移走），引号包含即可命中。若实现方发现某 target 未传递该路径，统一补 `target_link_libraries(... PUBLIC ng_foundation)`，不逐个改 include。

---

## 4. 命名空间：主推"保持 `core`，物理抽库先行"

`ng/libs/*` 的惯例是独立命名空间（`fastplot` → `nextpilot::plotscope::fastplot`；`protocol` → `nextpilot::plotscope::protocol`）。但 dataset 抽库**不建议本批改命名空间**：

- 保持 `nextpilot::plotscope::core`：30+ 引用文件里所有 `core::Dataset` / `DatasetId` / `ValueType` / `TimeUs` 字面量**零改动**，本批只需移文件 + 改 CMake + 改 include 路径。抽库收益（独立链接）立刻到手。
- 改 `nextpilot::plotscope::dataset`：更符合 libs 惯例，但要波及 30+ 文件、与 R0 的 `SourceRegistry`/`ToolboxDomain` 两个大改名叠加，实现方风险过高。

**裁决**：主推"物理抽库、命名空间保持 `core`"；命名空间统一作为**独立可选批次 R-D2**，待 R-D1 全绿后再评估。C++ 允许"库边界 ≠ 命名空间"（两者解耦），这不违反任何分层原则。

---

## 5. CMake 改动面（实现方）

| 文件 | 改动 |
|---|---|
| `ng/libs/foundation/CMakeLists.txt`（新建） | `add_library(ng_foundation INTERFACE)`，`target_include_directories(... INTERFACE include)` |
| `ng/libs/dataset/CMakeLists.txt`（新建） | `add_library(ng_dataset STATIC src/Dataset.cpp src/DataRepository.cpp)`，PUBLIC 链 `ng_kernel` + `Qt6::Core` + `ng_foundation`；AUTOMOC ON（仓库有 QObject 无关，但保持一致） |
| `ng/core/CMakeLists.txt` | 从 `ng_core` 源列表移除 `src/Dataset.cpp`、`src/DataRepository.cpp`（头是否保留转发视 §4 策略定）；`target_link_libraries(ng_core PUBLIC ng_dataset ng_foundation)` |
| `ng/CMakeLists.txt` | 在 `add_subdirectory(core)` **之前**加 `add_subdirectory(libs/foundation)` 与 `add_subdirectory(libs/dataset)`（依赖方向：先定义被依赖者） |
| 消费者 targets | 仅**按需**把 `ng_core` 换成 `ng_dataset`（如 `fastplot_dataset_test`），其余保持链 `ng_core` 即可自动获得 dataset（PUBLIC 传递） |

> `ng_kernel` 的 INTERFACE 定义当前在 `ng/CMakeLists.txt`（非独立子目录），抽库后维持原位即可——`ng_dataset` 只管链接它，不搬动其定义。

---

## 6. 复用契约与验收

1. **独立链接成立**：新增/改造一个最小 target，只链 `ng_dataset` + `ng_kernel` + `Qt6::Core`，能完成"建 Dataset → appendFrame → readWindow/lodMinMax"全流程，**不链接 `ng_core`**。`fastplot_dataset_test` 是天然的验收载体。
2. **零回归**：`ctest` 现有 11 项全绿（Debug/Release 双配置）。
3. **无循环**：`cmake` 配置成功、无 target 循环依赖告警。
4. **Result 不污染**：`ThemeManager` / `ModuleHost` 等仅链 `ng_core` 或其既有依赖即可拿到 `Result.h`，不因 dataset 抽库新增链接要求。
5. **领域纯净**：`ng/libs/dataset` 接口无 source/parser/intent/飞行/CAN 等领域词——它只提供 `Dataset/Table/Column/ChannelView/LodTriple/ColumnStats` 数据模型与 `DataRepository` 仓库。

---

## 7. 落地批次

| 批 | 内容 | 依赖 | 验收 |
|---|---|---|---|
| R-D1 | 下沉 `Result` → `ng_foundation`；新建 `ng_dataset` 并迁入 `Dataset`/`DataRepository`；`ng_core` 改 PUBLIC 依赖；消费者按需换链 | — | §6 第 1–4 条 |
| R-D2（可选） | 命名空间 `core` → `dataset` 全局统一（30+ 文件） | R-D1 | 与 libs 惯例一致；全绿 |

> 与 R0（`SourceRegistry` + `ToolboxDomain` 改名）**无依赖冲突**：R0 动的是 core/extension 的类型命名，R-D1 动的是数据模型的物理归属。可并行，但建议 R0 先行、R-D1 跟进，避免一次改动面过大。

---

## 8. 待需求方拍板项

1. **确认抽库边界**（§2）：`Dataset` + `DataRepository` 进库，`IngestPipeline`/`DecodedFrame`/`WorkspaceDataConfig` 留原位？
2. **确认 `Result` 下沉 `ng_foundation`**（§3）——这是抽库的硬性前置，不可跳过。
3. **确认命名空间保持 `core`、R-D2 另批**（§4）？若希望本批就统一到 `dataset`，请明确（将扩大改动面）。
4. `DataRepository` 是否借机改名 `DatasetRepository`（与 §1.4 可选项同源）？本设计**不**擅自采纳。
