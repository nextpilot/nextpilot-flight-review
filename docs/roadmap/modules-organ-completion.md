# 器官补齐架构设计：mod.report 与模块边界裁决

> 状态：**架构设计，待需求方拍板**（2026-11-19）。拍板后交实现方分批落地，本轮**不写实现代码**。
> 定位：对应 `ng/ARCHITECTURE.md` §11 落地顺序 #11「器官补齐」。地基（#1–#10、#12、#15、#16）已立，这是工具箱开工前的最后一块底座。
> 机制事实以代码为准，本文所有结论均逐行核对过 `ng/host`、`ng/modules/*`。

---

## 0. 一句话结论

**器官补齐不是"从零建六个模块"，而是"文档目标与代码实际脱节 + 唯一真欠 `mod.report`"**。
六个模块里五个已在位且机制完整，只剩 `report` 全仓零实现；`source`/`dataset` 是文档没跟上代码的合并，不该拆；`protocol`/`settings` 是代码有、文档器官表漏登记。

---

## 1. 现状核对：文档目标 vs 代码实际

`ARCHITECTURE.md` §4.1 定义的目标器官：`source / dataset / plot / analysis / report / script`（6 个）。
代码实际模块（`ng/modules/`）：`data / plot / protocol / script / settings / analysis`（6 个）。

| 文档 §4.1 目标 | 代码实际 | 差异性质 | 裁决 |
|---|---|---|---|
| `mod.source` + `mod.dataset` | 合并为 `mod.data` | **文档脱节**（非欠账） | **不拆**，更新文档追认合并 |
| `mod.plot` | `mod.plot` ✅ | 对齐 | 保持 |
| `mod.analysis` | `mod.analysis` ✅ | 对齐 | 保持（已实现调度+gallery） |
| `mod.script` | `mod.script` ✅ | 对齐 | 保持 |
| `mod.report` | **无**（全仓零实现） | **真欠账** | **补建**（本文核心） |
| 文档未列 | `mod.protocol` | 文档漏登记 | 补进 §4.1 器官表 |
| 文档未列 | `mod.settings` | 文档漏登记 | 补进 §4.1 器官表 |

### 1.1 为什么不拆 `source` / `dataset`

- §3.1 已把 DataSource 定为**三段聚合的运行时实例**（来源+解析绑定+数据集），用户操作上是"打开一个源就可用"的统一概念；拆成两个可装卸模块会把一个聚合根割裂。
- 代码 `mod.data` 已承载：打开/关闭/录制/回放（源生命周期）+ 数据源树/表/列浏览（数据集目录）+ 派生/导出/reload/recent。这正是 §4.1 给 source 与 dataset 两项机制之和。
- 拆分的唯一收益是"单独装卸"，但 dataset 离开 source 没有数据来源，二者必然成对出现——**假独立**，违反 §4.2「装卸互不牵连」的本意。
- 结论：`mod.data` 内部保持 `DataManagerPanel` 单树双视图（源卡片→表→列），对外 `provides: data.session`。**文档改，代码不改。**

### 1.2 为什么 `report` 是真欠账

- §3.4 明确定义 Report 是三类消费者之一（View/Analysis/Report），产出 HTML/PDF/CSV 文档；老代码无 Report，是新需求，架构上"给了位置但没人住"。
- 代码里已有**接线点但无实现**：`AnalysisModule::onAttach` 注册了 `analysis.app.report` → `autopilot.review.report`（点击给诚实提示）。报告器官落地后，这个入口才有真实落点。
- 交付标准 §13 第 6 项要求"示例集"、toolbox 架构要求"飞控 review 与报告边界"——报告是分析价值外泄的出口，缺它，分析结果只能留在内存。

---

## 2. `mod.report` 设计（核心）

### 2.1 职责边界：机制归器官，内容归扩展

严格套用 §4.1 的"器官不懂业务"硬规则：

| 归 `mod.report`（机制） | 归扩展（知识） |
|---|---|
| 报告**模板注册槽**：扩展注册"我提供某类报告模板" | 模板本体：章节结构、措辞、图表选择（如 `autopilot.review.report`） |
| **渲染调度**：把模板 + 输入数据 → 文档 | 报告内容：算哪些指标、怎么解读 |
| **导出**：HTML / PDF / CSV 三后端 | 领域排版约定 |
| 结果**缓存与预览** | — |

**器官里不出现领域词汇**（飞行/姿态/CAN/电池）；出现即违规。

### 2.2 插槽与意图面

- 插槽：`report.preview` 面板（§4.1 已预留此名）。
- 意图（与 §9 统一意图管道一致，GUI/CLI/脚本同一路径）：

| 意图 op | 输入 | 产出 | 说明 |
|---|---|---|---|
| `report.render` | `templateId` + 输入（`datasetId` / `findingId` 列表） | 渲染后的文档模型 | 器官调度，模板来自扩展 |
| `report.export` | 文档模型 + 格式（`html`/`pdf`/`csv`） | 文件 | 导出三后端 |
| `report.preview` | 文档模型 | 预览部件 | GUI 用；headless 无操作 |

### 2.3 依赖方向：**软依赖，禁用 `requires`**（关键裁决）

`ModuleHost::resolveOrder` 的语义（逐行核对）：某个模块 `requires` 的能力若**无人提供或提供者被禁用**，该模块直接被标记错误、**拒绝加载**。

因此 `mod.report` **不得写 `requires: analysis.runner`**——否则需求方禁用分析模块时，报告模块会连带加载失败，违反 §4.2「装卸互不牵连：卸掉 mod.analysis 不影响其它模块」。

正确做法（对齐 `mod.script` 的既有先例——它就不写任何 requires，靠门面按需发意图）：
- `mod.report` 的 `provides` = `report.renderer`，`requires` = 空；
- 报告需要分析结果时，**运行时发 `analysis.run` / 查数据集**按需取，取不到则给出诚实提示（"分析模块未启用"），而非启动期硬失败；
- `analysis.app.report` 入口点选 → 发 `report.render`，由 report 器官去拉分析结果。

> 这条是本文最容易做错的地方，实现方必须遵守：**report 对 analysis 是运行期意图调用，不是编译/装载期依赖。**

### 2.4 与现有代码的三个接线点

1. `AnalysisModule::onAttach` 的 `analysis.app.report`（`autopilot.review.report`）：报告器官落地后接真实 `report.render`。
2. `report.preview` 面板：由外壳按 id 特判创建部件（沿用 `ProtocolEditorDialog` 的特判模式），或领域面板贡献。
3. §3.4 的 `finding 挂 dataset 不挂曲线`：报告输入应是 dataset 级 finding，report 器官按此取数。

---

## 3. 模块能力契约全景（实现方据此对齐 provides/requires）

| 模块 | provides | requires | 依据 |
|---|---|---|---|
| `mod.data` | `data.session` | 空 | 代码现状；不拆 |
| `mod.plot` | `plot.renderer` | `data.session` | 代码现状（软依赖以外，这个是真依赖，保持） |
| `mod.analysis` | `analysis.runner` | 空 | 代码现状 |
| `mod.script` | `script.runner` | 空 | 代码现状（刻意无硬依赖） |
| `mod.protocol` | `protocol.editor` | 空 | 代码现状 |
| `mod.settings` | `settings.store` | 空 | 代码现状 |
| **`mod.report`（新）** | **`report.renderer`** | **空（软依赖 analysis）** | 本文 §2.3 裁决 |

> 注：`mod.plot requires data.session` 是既有真依赖（画图必须有数据），保留；这与 report 的情况不同——plot 离开 data 确实无意义，而 report 可以在无 analysis 时做纯数据导出。

---

## 4. "新增器官不改既有代码"契约核验（结论：成立）

逐行核对装载链路，确认这条架构承诺是真的：

| 环节 | 事实 |
|---|---|
| 自注册宏 | `NG_REGISTER_MODULE(Cls)` 在模块 TU 静态期调 `BuiltinModuleRegistry::registerModule` |
| 库类型 | 全部 6 个模块均为 **OBJECT 库**（静态 TU 不被链接器丢弃） |
| 装载入口 | `App.cpp:23` 遍历 `BuiltinModuleRegistry::modules()`，宿主无具体器官类名 |
| 依赖解析 | `ModuleHost::resolveOrder` Kahn 拓扑排序，能力名解耦 |
| 意图通信 | 器官间只经意图，互不 include 头 |

**新增一个器官的改动面**：新增 `ng/modules/<name>/` 目录 + `ng/CMakeLists.txt` 加一行 `add_subdirectory` + `ng/app/CMakeLists.txt` 加一个 OBJECT 库链接项。
→ **两行 CMake，零 C++ 改动**，符合契约（CMake 登记属于"装配"，不是"改既有逻辑"）。

---

## 5. 验收标准（实现方消项后逐条验证）

1. **能力矩阵**：§3 表格 7 个模块的 provides/requires 与代码 `moduleInfo()` 逐字段一致。
2. **report 最小闭环**：`report.render` 用 `analysis.app.default` 产出的默认曲线结果，能渲染出一份真实 HTML 报告；`report.export` 产出 PDF/CSV 各一。
3. **软依赖成立**：禁用 `mod.analysis` 后，`mod.report` 仍能加载（`resolveOrder` 不报错）；此时触发 report 给"分析未启用"诚实提示，不崩溃。
4. **接线生效**：分析页 `analysis.app.report` 点击后进入真实报告流程，不再是占位提示。
5. **新增不改既有**：临时加一个空器官验证——只加目录 + 2 行 CMake 即完成装配，App/宿主/既有模块 C++ 源码零改动。
6. **零回归**：`ctest --test-dir ng/build-release` 现有 11 项全绿；headless（无 Qt Widgets）可跑 report 渲染/导出。
7. **领域纯净**：`mod.report` 源码 grep 无飞行/姿态/CAN/电池等领域词。

---

## 6. 交给实现方的批次建议（拍板后执行）

| 批 | 内容 | 依赖 | 可独立验收 |
|---|---|---|---|
| R1 | 文档追认：`ARCHITECTURE.md` §4.1 器官表补 `data`/`protocol`/`settings`、删 `source`/`dataset` 分立；§11 #11 状态更新 | — | 文档与代码模块名一致 |
| R2 | `mod.report` 骨架：moduleInfo + OBJECT 库 + 自注册 + 模板注册槽 + `report.render/export/preview` 意图处理器（空实现给诚实提示） | R1 | 模块可装载、意图可达、软依赖验收通过 |
| R3 | 导出三后端（HTML/PDF/CSV）+ `report.preview` 面板 + 接 `analysis.app.report` | R2 | §5 第 2、4 条 |
| R4 | 首个报告模板扩展（autopilot review）作为知识包样例 | R3 | §5 第 2 条真实报告 |

> **红线**：需求方未拍板本文 §1 裁决（尤其"不拆 source/dataset"与"report 软依赖"）前，不动实现代码。实现方如有不同结论，在本文追加"复核意见"行，不删除裁决项。

---

## 7. 待需求方拍板项

1. **确认不拆 `mod.source`/`mod.dataset`**，保留 `mod.data`（§1.1）？
2. **确认 `mod.report` 对 analysis 用软依赖**、不写 `requires`（§2.3）？
3. **报告导出后端优先级**：HTML/PDF/CSV 三个是否同批都要？PDF 是否引入新依赖（如 Qt Pdf / 外部工具）？
4. **报告模板落地范围**：R4 的 autopilot review 模板是否本批做，还是只留注册槽、模板后补？
