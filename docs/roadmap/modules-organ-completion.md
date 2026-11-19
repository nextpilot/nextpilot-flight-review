# 器官补齐架构设计：source/dataset 拆分裁决 + report 扩展点预留

> 状态：**架构设计 v2，待需求方拍板**（2026-11-19）。拍板后交实现方分批落地，本轮**不写实现代码**。
> 定位：对应 `ng/ARCHITECTURE.md` §11 落地顺序 #11「器官补齐」。地基（#1–#10、#12、#15、#16）已立，这是工具箱开工前的最后一块底座。
> 机制事实以代码为准，本文所有结论均逐行核对过 `ng/host`、`ng/modules/*`。

---

## 0. 一句话结论

器官补齐要办两件事：
1. **`source` / `dataset` 拆开**——架构上应拆（v1 判"不拆"是错的，见 §1.1），模块边界与 UI 边界解耦；
2. **`report` / autopilot review 暂不做成品**——它是工具箱子产品、形态未定；只把 extension 需要的**扩展点、契约、意图常量**铺好，器官实体等子产品成形再建。

---

## 1. source / dataset 拆分裁决

### 1.1 改判：从"不拆"到"拆"（含自我纠错）

v1 曾用"dataset 离开 source 没有数据来源、二者是假独立"作为不拆理由。**这个理由是错的**：它把"dataset 的**产生**依赖 source"偷换成了"dataset **模块**依赖 source 模块"。

事实是：dataset 一旦产生就落在共享仓库 `DataRepository`（§3.1 明确归属"仓库，Rust 拥有"），可持久化到 `ws/`、离线打开、workspace 恢复、导入——**生命周期不依附于任何单一 source**。所以二者是真独立。

### 1.2 架构上应该拆的三个理由

1. **变化轴不同**：source 随"格式 × 传输 × 录制 × 回放 × 嗅探"变化；dataset 随"查询 × 订阅 × 派生 × 多消费者"变化。合在一处，改一边要回归另一边。
2. **资产归属不同**：Dataset 是**共享资产**（被 plot/analysis/未来报告多方引用、可由多源产出），不是 source 的私有产物。把它绑在"打开源"的模块里，是把资产错当实例附属品。§3.1「聚合的是运行时绑定关系，不是资产归属」正支持此判断。
3. **依赖方向错位**：现在 `plot requires data.session`（源管理），但绘图只需要**查数据**，不需要知道源如何打开。拆后 plot/analysis 应依赖 `dataset.catalog`。

### 1.3 拆分方案：模块边界 ≠ UI 边界

| 模块 | provides | requires | 职责 |
|---|---|---|---|
| `mod.source` | `source.session` | 空 | 打开/关闭/录制/回放/嗅探/解析器绑定；只认字节，不产业务语义 |
| `mod.dataset` | `dataset.catalog` | 空 | 表/列/时间范围查询、订阅变化、派生数据；经 `AppContext.data()` 查共享仓库 |
| `mod.plot` | `plot.renderer` | `dataset.catalog`（原 `data.session`） | 画布宿主 |
| `mod.analysis` | `analysis.runner` | 空 | 分析调度；按需发意图取数，不写硬依赖 |
| `mod.script` | `script.runner` | 空 | 脚本门面 |
| `mod.protocol` | `protocol.editor` | 空 | 协议编辑/校验 |
| `mod.settings` | `settings.store` | 空 | 设置 |

- **`mod.dataset` 不依赖 `mod.source`**：它查的是共享仓库 `AppContext.data()`，不调用源模块。二者经共享状态通信，符合 §4.2「器官间只经意图/共享态通信，互不 include 头」。
- **UI 保持一个复合树**：数据源树是 shell/views 层面板，直接读 `AppContext.sources() + data()`，**不属于任何模块**——模块装卸不影响它。用户"每行一个源 → 展开看表/列"的体验不变。
- **独立装卸成立**：headless 可只装 `mod.dataset`（查已有/恢复数据），不装 `mod.source`。

### 1.4 迁移代价（一次性、可控）

- `DataModule` 一分为二：`mod.source`（源生命周期动作）+ `mod.dataset`（查询/订阅动作）；`DataManagerPanel` 留在 views 层，改为直接读共享态。
- `mod.plot` 的 `requires` 由 `data.session` 改 `dataset.catalog`。
- 系统级 `@system/data.load`、`@system/data.commands` 仍归宿主（不随模块装卸），保持现状。
- 现有 `data.*` 意图按归属拆分：`source.open/close/record/replay` 归 source；`data.derive/export/recent/clearCache` 归 dataset（意图名可沿用，避免破坏脚本）。

---

## 2. report / autopilot review：暂不做成品，只备扩展点

### 2.1 需求方决定（2026-11-19）

- **不做**：`mod.report` 器官实体、报告预览 UI、autopilot review 报告模板、PDF/HTML/CSV 导出实现。
- **理由**：报告是工具箱的子产品，形态尚未想清；现在做成品等于赌一个未完成的设计。
- **要做**：把 extension 将来做报告/复盘时需要的**依赖、扩展点、契约、意图常量**准备好，让 extension 有地方挂、有契约可依。

### 2.2 为 extension 准备的依赖清单

| 依赖 | 现状 | 本轮要备的东西 | 归属 |
|---|---|---|---|
| **finding 契约** | 已有：§3.4 finding 挂 dataset 不挂曲线 | 固化 `Finding` 结构（datasetId/kind/severity/payload），作为报告类扩展的分析输入 | 已有，补文档固化 |
| **dataset.catalog** | §1 拆分后产生 | 报告扩展按表/列/时间范围取数的查询契约 | `mod.dataset` 提供 |
| **报告模板贡献接口** | 无 | 定义 `IReportTemplateContribution`：`id/title/inputKinds/render(ctx)`，**只有接口、无实现、无注册实例** | core/host 接口层 |
| **导出能力契约** | 无 | 定义导出接口（HTML/PDF/CSV）与格式枚举；**不绑 handler、不引依赖** | core/host 接口层 |
| **意图 op 常量** | 部分缺失 | `report.render` / `report.export` / `report.preview` 在 Intent 常量表登记；extension 可发，注册 handler 前给"未实现"诚实提示 | Intent 常量表 |

### 2.3 什么时候才建 mod.report

当工具箱的子产品（如 autopilot review）形态确定、且至少一个真实报告模板要落地时，再按 §4.1「机制归器官、内容归扩展」建 `mod.report`：器官提供模板注册槽 + 渲染调度 + 导出，模板内容由 extension 贡献。本轮**不建**。

---

## 3. "新增器官不改既有代码"契约核验（结论：成立）

逐行核对装载链路，确认这条架构承诺是真的：

| 环节 | 事实 |
|---|---|
| 自注册宏 | `NG_REGISTER_MODULE(Cls)` 在模块 TU 静态期调 `BuiltinModuleRegistry::registerModule` |
| 库类型 | 全部模块均为 **OBJECT 库**（静态 TU 不被链接器丢弃） |
| 装载入口 | `App.cpp:23` 遍历 `BuiltinModuleRegistry::modules()`，宿主无具体器官类名 |
| 依赖解析 | `ModuleHost::resolveOrder` Kahn 拓扑排序，能力名解耦 |
| 意图通信 | 器官间只经意图/共享态，互不 include 头 |

**新增一个器官的改动面**：新增 `ng/modules/<name>/` 目录 + `ng/CMakeLists.txt` 加一行 `add_subdirectory` + `ng/app/CMakeLists.txt` 加一个 OBJECT 库链接项。
→ **两行 CMake，零 C++ 改动**，符合契约（CMake 登记属"装配"，非"改既有逻辑"）。

> 依赖警示（实现方注意）：`resolveOrder` 对 `requires` 的处理是——能力无人提供或提供者被禁用，消费者**直接加载失败**。因此只有"离开对方确实无意义"的依赖才写 `requires`（如 plot→dataset）；可取可不取的依赖（analysis 取数、report 取分析结果）一律用运行期意图软调用，不写 `requires`。

---

## 4. 验收标准（实现方消项后逐条验证）

1. **拆分闭环**：`mod.source`、`mod.dataset` 各自可独立装载/卸载；只装 dataset 时 headless 能查询已恢复的数据。
2. **依赖正确**：`mod.plot requires dataset.catalog`，`mod.dataset requires` 为空；禁用 source 后 dataset/plot 仍加载正常。
3. **UI 不变**：数据源树仍是单树（源卡片 → 展开表/列），与拆分前截图逐项一致。
4. **意图兼容**：既有 `data.*` 意图名不破坏脚本；`data.load` 仍归宿主系统级。
5. **扩展点就绪**：`IReportTemplateContribution`、导出契约、`report.*` 意图常量三个接口文件存在且可编译；无任何实现/注册实例。
6. **诚实提示**：extension 发 `report.render` 但无 handler 时，返回稳定错误码 + "报告能力未启用"，不崩溃。
7. **零回归**：`ctest --test-dir ng/build-release` 现有 11 项全绿；headless 可跑。
8. **领域纯净**：core/host 新增接口无飞行/姿态/CAN/电池等领域词。

---

## 5. 交给实现方的批次建议（拍板后执行）

| 批 | 内容 | 依赖 | 可独立验收 |
|---|---|---|---|
| R1 | 文档追认：`ARCHITECTURE.md` §4.1 器官表改为 source/dataset/plot/analysis/script/protocol/settings（无 report）；§11 #11 状态更新 | — | 文档与拆分裁决一致 |
| R2 | 拆 `mod.data` → `mod.source` + `mod.dataset`：moduleInfo/OBJECT 库/自注册/意图处理器拆分；`mod.plot.requires` 改 `dataset.catalog` | R1 | §4 第 1、2、4 条 |
| R3 | 数据源树改为 views 层复合面板，直接读共享态；UI 截图对比 | R2 | §4 第 3 条 |
| R4 | 扩展点与契约（**仅接口/常量，无实现**）：`IReportTemplateContribution`、导出契约、`report.*` 意图常量 + 诚实提示 handler | R1 | §4 第 5、6 条 |

> **本轮明确不做**：`mod.report` 器官、报告 UI、autopilot review 模板、PDF/HTML/CSV 导出实现。等工具箱子产品形态确定后再立新批。
> **红线**：需求方未拍板 §1（拆 source/dataset）与 §2（report 只备依赖）前，不动实现代码。实现方如有不同结论，在本文追加"复核意见"行，不删除裁决项。

---

## 6. 待需求方拍板项

1. **确认 source / dataset 拆开**，且 `mod.plot.requires` 改 `dataset.catalog`（§1）？
2. **确认 report / autopilot review 本批不做成品**，只备扩展点 + 契约 + 意图常量（§2）？
3. **导出后端**：本批只定接口不引入任何 PDF/HTML 依赖，确认？（§2.2）
4. finding 结构固化为 `datasetId/kind/severity/payload` 是否够用，还是要留扩展字段？（§2.2）
