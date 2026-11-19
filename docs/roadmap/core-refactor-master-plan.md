# Core 重构总方案：core 消亡 + 机制抽库 + 解析器合并 + 命名收敛（2026-11-19 开工版）

> **状态：✅ 需求方已拍板，架构师已做两轮评审，本轮起开工实现。**
> 需求方原话："写成 roadmap 方案，然后开工：① schema、register、intent、dataset/repo 抽象成库；② IMessageParser 合并、去掉 IFormatDriver；③ 部分命名。你是架构师，要判断合理性，我是需求方。"
> **v2 升级（2026-11-19 追加，见 §7）**：需求方进一步裁决——"core 我都不想要了，有意义的就一个 AppContext，其他抽象的实现尽量放在 libs"，并立铁律"'libs 不允许实例化对象（除非静态对象），只能是函数或 class'"。架构师据此把终态从"core 收敛为核心契约层"升级为 **core 目录彻底消亡**：全部内容按机制下沉 libs，AppContext 归 `libs/context` 并改构造注入。**以 §7 为准；§1–§6 作为开工基线与历史裁决保留。**
> 本文件是**总方案 + 开工计划**，三条主线的详细裁决仍以三份专项文档为准：
> - [`mechanism-libs-extraction.md`](mechanism-libs-extraction.md)（注册表 / 意向 / schema→UI / format 辨析 / 解析器合并）
> - [`dataset-lib-extraction.md`](dataset-lib-extraction.md)（dataset 抽库）
> - [`modules-organ-completion.md`](modules-organ-completion.md)（器官拆分 / 命名裁决）
>
> 实现前逐行复核过 `ng/core`、`ng/libs`、`ng/gui`、`ng/host`、`ng/modules`、`ng/extensions`、`ng/CMakeLists.txt`，并跑通基线 `ctest`（Release 11/11 全绿）。

---

## 1. 三条主线汇总

| # | 主线 | 交付物 | 专项依据 |
|---|---|---|---|
| ① | **机制抽库**：把 core 里可复用的地基/机制下沉为独立 libs | `ng_foundation` / `ng_dataset` / `ng_registry` / `ng_intent` / `ng_uischema` 五个库 | mechanism §1–§4、dataset §2–§5 |
| ② | **解析器合并**：取消 `IFormatDriver` 门面，统一为 `IMessageParser` + 多输出函数 | 单一解析器接口、`ParserRegistry` 统一查询 | mechanism §9.3 / §10.3.1 |
| ③ | **命名收敛**：消除 `Format`/`DataSource` 误导命名 | `SourceRegistry` / `Domain` / `Toolbox`；`IFormatDriver` 取消；`RecordingFormat` 保持 | modules-organ §1.4/§7、mechanism §9.6 |

**统一前置**：`Result.h` 下沉 `ng_foundation`（21 处引用、被 5 个库共享）——**只做一次，所有批次受益**。

---

## 2. 架构师合理性判断（本方案的核心增值）

需求方要我"判断合理性"。逐条审视后：**三条主线方向全部成立**，但有 **4 处必须修正/设边界**，否则实现会踩坑。这些修正不推翻需求方裁决，而是让裁决能安全落地。

### 2.1 ✅ 完全合理、直接执行的部分

| 项 | 判断依据 |
|---|---|
| `Result` 下沉 `ng_foundation` | 复核 21 处引用，含 `ThemeManager`/`ModuleHost` 等纯 UI/宿主类。不下沉必然"循环依赖"或"污染所有消费者"，这是抽库的**硬前置**，别无选择 |
| dataset 抽库 | `Dataset.h` 只依赖 `ps_kernel.h` + Qt Core，零 core 业务类型；`test_fastplot_dataset.cpp` 被迫链接整个 `ng_core` 是现成的复用痛点。边界干净 |
| intent 抽库 | `Intent`/`IntentResult`/`Job`/`IIntentHandler`/`IIntentExecutor` 全是纯契约 + 纯数据，无 QWidget、无领域词，天然可独立 |
| schema = 纯 metadata、零 UI 控件词汇 | 需求方定性精准。C++ 无反射，参数必须显式声明（`paramSpecs()`），但声明从"完整 UI schema"瘦身为"数据规格"，方向正确 |
| 命名（`SourceRegistry`/`Domain`/`Toolbox`、取消 `IFormatDriver`、`RecordingFormat` 保持） | 逐文件核对证据充分，均为机械替换，风险低 |

### 2.2 ⚠️ 修正一：注册表泛型化要"克制"——抽最小核，不一次性统一 5 套

专项文档（mechanism §2）提出"一个 `Registry<T>` 统一 5 套注册表"。**我判断这个抽象必须克制，否则会做成泛型怪物。**

复核发现 5 套注册表的差异远大于共性：

| 注册表 | 真正独特的能力（不能塞进泛型核） |
|---|---|
| `ParserRegistry` | 静态单例 + 按后缀/sniff 内容查询 + 插件句柄 RAII（C ABI vtable） |
| `ExtensionRegistry` | `(kind×domain)` 二维索引 + `unique_ptr` 按包原子装卸 |
| `PanelFactoryRegistry` | QWidget 构造函数工厂 |
| `BuiltinPackRegistry` | 编译期静态自注册 |
| `RecordingFormat/Backend` | 纯描述结构 |

共性只有"id→条目 的登记/查询/移除/遍历"这一层薄薄的 CRUD。

**裁决（收窄范围）**：
1. `ng_registry` 第一版**只抽最小 CRUD 核** `Registry<Key, T>`（insert/remove/find/keys/count）+ 一个 `StaticAutoRegistry<T>` 自注册助手（统一散落的自注册宏）。
2. **各注册表特殊的查询语义（sniff / 二维索引 / 插件句柄 / 工厂）保留在各自薄壳里**，不塞进泛型核。
3. 采用专项文档已有的"两步走"：**先抽壳（对外 API 不变、内部委托模板），验证全绿；再逐步迁内部**。本轮**不追求"5 套全部委托模板"的验收指标**——那是终态，不是开工态。
4. `SourceRegistry` 本批**只改名，不纳入泛型模板**（改名与泛型改造叠加风险过高，另批收敛）。

> 理由：抽库的第一目标是"物理归属归位 + 独立可链接"，不是"消灭所有重复代码"。泛型化是手段不是目的，过度抽象会让简单的注册表变复杂、违背"接口等价重构、调用方零改动"的安全原则。

### 2.3 ⚠️ 修正二：解析器合并只动 C++ 接口层，SPI vtable 不碰

专项文档（§10.3.1）已明确 SPI 对齐"另批"，我复核后**强化这条边界并说明原因**：

- 代码里有**两个完全不同的"注册表"**：
  - `ParserRegistry`（core，`ps_parser_spi.h`）——管的是 **C ABI `PsParserVtable`**，服务 `data.load` 摄取管道，带插件 dlopen/ABI 校验；
  - `ExtensionRegistry`——管的是 **C++ `IToolContribution`**（含 `drivers()` / `messageParsers()` 快捷 API），服务工具箱贡献系统。
- "取消 `IFormatDriver`、统一 `IMessageParser`"是 **C++ 接口层**的合并；**不能连带合并 SPI vtable**，那要动 `abi_version` 和插件加载闸门，是 ABI 破坏性变更。
- `IFormatDriver::load()` 的三个实现（CSV/ULog/CustomProtocol）现在都返回 `eunsupported`——门面已萎缩成空壳，取消它**没有功能损失**；其声明职责（`parserId`/`suffixes`/`preferredTableName`/`importSchema`/`probeFile`/`prepareParserOptions`）并入 `IMessageParser` 的自述 + `paramSpecs`。

**裁决**：本批合并范围 = `IFormatDriver` + 三个 `XxxFormatDriver` 门面类 + `IMessageParser` → 单一 `IMessageParser`（多输出函数 `parseFrames`/`parseMessages` + `outputs()` 能力声明）；`ExtensionRegistry` 的 `drivers()/messageParsers()` 收敛为统一 `parsers()` 能力视图（保留旧 API 兼容别名）。**SPI 层（`ps_parser_spi.h` / `ps_message_parser_spi.h` / `ParserRegistry`）零改动**。

### 2.4 ⚠️ 修正三：两个参数对话框本批**不合并**，共用 SchemaEngine 但保持独立

专项文档 §10.6-4 把"两对话框合一"标为**待拍板**。我作为架构师**裁决为"本批不做"**：

复核发现两者用户交互本质不同：
- `ImportDialog`：探测预览（CSV 探测出列/预览数据/警告），是**数据加载**交互；
- `ProtocolInspectorDialog`：解码后的字段树展示，是**协议调试**交互。

强行合并会让体验退化，且收益仅为"少一个对话框类"。

**裁决**：本批让两者**共用同一个 `SchemaEngine`（schema 只有一份、渲染逻辑只有一份）**，但两个对话框外壳保持独立。这样拿到"schema 统一"的实质收益，又不冒体验风险。§10.5-3 的验收指标相应调整为"两个对话框不再各自 switch 控件类型"（共用渲染器），而非"合并为一个类"。

### 2.5 ⚠️ 修正四：`ParamSpec` 补 `optionsFrom`（动态枚举来源），否则 CSV 做不出来

复核 `CsvFormatDriver::importSchema()` 发现一个专项文档 §10.2.1 的 metadata 白名单**漏掉的能力**：

- 时间列 `time.index` 的枚举候选**不是静态的**，而是来自 `probe()` 结果（`source: {probe_key: "columns", label_key: "name", filter_key: "kind"}`）——从探测出的列名动态生成下拉选项。
- 还有 `initial_from_probe`（用探测结果预填字段值）、`visible_if`（字段间依赖显隐）等。

专项文档把 `visible_if` 收敛为 `dependsOn`（逻辑依赖，合理），但**没给"枚举候选从探测结果来"留位置**。

**裁决**：`ParamSpec` 的 metadata 白名单补两项——
- `optionsFrom: {provider: "probe", key: "columns", labelKey: "name", filterKind: "all"}`：**数据语义**（枚举候选的来源），不是 UI 词汇，允许进 schema；
- `initialFrom: {probeKey: "suggested_time"}`：**数据语义**（默认值来源），允许进 schema。

`ui.preview`/`ui.warnings`（预览表格与警告规则）仍归 probe 结果的统一渲染，不进 `ParamSpec`——专项文档 §9.4 的处理保留。

---

## 3. 依赖图与批次顺序

```
批次 1  ng_foundation (Result) ──────────┐   ← 所有库的前置，只做一次
                                          │
批次 2  ng_dataset ──────────────────────┤
批次 3  ng_registry ─────────────────────┤
批次 4  ng_intent ───────────────────────┤
                                          ▼
批次 5  ng_uischema (契约+SchemaEngine+渲染)  ← 依赖 foundation
                                          │
批次 6  IMessageParser 合并 + 命名收敛      ← 依赖 uischema(paramSpecs)
```

**顺序理由**：foundation 是硬前置；dataset/registry/intent 三者互不依赖、可独立验证；uischema 依赖 foundation（契约层）；解析器合并与命名放最后——它们依赖 uischema 的 `ParamSpec`（schema 改由 `paramSpecs()` 提供），且命名是全仓机械替换，放最后避免中途反复改新名字。

> 器官拆分（`mod.source` / `mod.dataset`）**不在本轮三条主线内**（需求方本轮只列了抽库/合并/命名），保持专项文档 `modules-organ-completion.md` R0–R4 独立推进，本方案**不含**。

---

## 4. 开工批次与验收（实现方 = 本 AI，逐批 build + ctest）

| 批次 | 内容 | 关键文件 | 验收 |
|---|---|---|---|
| **B1** | 下沉 `Result` → `ng_foundation`（INTERFACE），21 处引用零改字面量 | 新建 `libs/foundation/`，移 `Result.h`，改 CMake include 传递 | 5 个 target 均可拿到 Result；ctest 11/11 |
| **B2** ✅ | 新建 `ng_dataset`，迁 `Dataset`/`DataRepository`，核心 PUBLIC 依赖 | `libs/dataset/`、`core/CMakeLists.txt`、`test_dataset_standalone.cpp` | 独立 target 只链 ng_dataset+Qt6::Core（实证 link.txt 无 ng_core）走通建表→读窗→仓库增删；12/12 |
| **B3** ✅ | `ng_registry` 最小 CRUD 核 + 自注册助手；`PanelFactoryRegistry` 薄壳化验证 | `libs/registry/`、`core/src/PanelFactoryRegistry.cpp`、`test_registry_standalone.cpp` | 独立 target 只链 ng_registry+Qt6::Core 走通 CRUD+自注册+工厂型条目（无 core/QWidget/Rust 反向依赖，编译即证）；对外 API/宏零改动；13/13 |
| **B4** ✅ | `ng_intent` 移出 5 个意向类型 | `libs/intent/`、`core/`、`AppContext`/`ScriptFacade` | 独立验收 `ng_test_intent_standalone`（只链 ng_intent+foundation）走通 Job JSON 往返+缺 op 报错+录制器+契约实现；Release+Debug 14/14 |
| **B5**（部分） | `ng_uischema`：纯 metadata 契约 + `SchemaEngine` + 唯一 Qt 渲染器；TransformEditorDialog 已接入 | `libs/uischema/`、`core/Transform`、`gui/TransformEditorDialog` | 唯一 `FieldType→QWidget` 映射落在 uischema；headless compile/JSON 往返测试 + transform 适配测试；Release+Debug 16/16。ImportDialog/ProtocolInspectorDialog 待 B6 `paramSpecs()` 接口后接入 |
| **B6** ✅ | 取消 `IFormatDriver` + 三个门面类，统一 `IMessageParser`（多输出函数）；命名收敛 `SourceRegistry`/`Domain`/`Toolbox`；ProtocolInspectorDialog 接入 uischema | `core/`、`extensions/`、`ExtensionRegistry`、`ARCHITECTURE.md` | 无 `IFormatDriver`/`IDataParser`/`XxxFormatDriver` 门面类型；无旧命名残留；Release+Debug 16/16。ImportDialog 动态探测字段接入见 §5.2 |
| **B8** ✅ | Transform 输入控件重构：`TransformField` 可编辑下拉 + 独立 `LuaEditor` + 脚本 `describe()` 自述元数据 | `gui/TransformField`、`gui/LuaEditor`、`modules/script/LuaScriptLibrary`、`core/Transform`、`AppContext` | 下拉可选算子（带说明）/ 直接敲表达式 / 编辑按钮进 Lua Editor；脚本落 `scripts/ops/*.lua` 并注册 `lua.<name>`；能力过滤与在线调试保留；`describe()` 沙箱解析元数据、文件为真源+快照兜底；Release+Debug 18/18 |

**每批硬指标**：Release + Debug 双配置 `ctest` 11/11 全绿；新库领域纯净（无飞行/CAN/姿态词）；headless 可编译。

---

## 5. 风险与红线

1. **基线保护**：每批前确认工作区干净、当前 11/11 全绿；每批后独立提交（便于回滚单批）。
2. **接口等价**：抽库/泛型化期间对外 API 保持不变，调用方零改动；`using` 兼容别名先留一版。
3. **ABI 红线**：B6 不碰 SPI vtable、`abi_version`、插件加载闸门。
4. **命名空间**：dataset 抽库保持 `nextpilot::plotscope::core`（B2 只物理抽库），全局改命名空间另批。
5. **不动项**：器官拆分（`mod.source`/`mod.dataset`）、report 成品、SPI 合并、两对话框合一——本轮**明确不做**。
6. **实现方异议**：如某批发现与裁决冲突的事实，在本文追加"复核意见"，不擅自删改裁决项，先报需求方。

### 5.1 B6 复核意见（2026-11-20，实现方）

开工前逐文件核对三个门面类与两条数据通路，澄清三点，均不改变裁决：

1. **两条通路输出不同不是合并障碍**：`IFormatDriver` 经宿主管道产 Dataset（画图），`IMessageParser` 产 `DecodedFrame`（协议检查器）。B6 合并只动 C++ 声明层——门面的 `parserId`/`suffixes`/`preferredTableName`/`probe`/`paramSpecs` 声明并入 `IMessageParser` 自述，`parseFrames`/`parseMessages` 作多输出函数；SPI 与两条执行通路零改动。裁决 §2.3 本就如此，此处确认无歧义。
2. **`load()` 全 `eunsupported`**：三个门面类的 `load()` 都已返回 `eunsupported`，取消门面无功能损失；`DataLoadHandler` 旧路径（`parserId` 为空才走 `load()`）随门面类一并删除，只剩统一管道一条路。
3. **`prepareParserOptions` 读模板文件不违反解析器纪律**：CustomProtocol 在宿主调用时把 `templatePath` 展开成内嵌 `template` 文本，解析器本身仍不读文件、只吃 `configure` 参数。该函数是**宿主侧编排步骤**，归并后移到统一入口（不再是"格式门面的方法"），`IMessageParser::configure` 禁读文件的约束保持。

### 5.2 B6-c 收口状态（2026-11-20，实现方）

`paramSpecs()` 统一参数契约已落地：`IMessageParser::paramSpecs()` 默认从 `configSchema()` 的 fields 转纯 metadata（key/type/default/min/max/options/fileFilter），headless 可查。`configSchema()` 保留 virtual，供含额外信息的解析器覆盖。

- **已接入**：`ProtocolInspectorDialog` 参数区改为 `paramSpecs() → SchemaEngine.compile → QtFieldRenderer.render`，删除手绘 bool/int/enum/string switch；中文 label 从 configSchema 作术语表覆盖传入。`collectParams()` 改用渲染器统一 `keyOf/valueOf`。
- **已接入（uischema v2 收口）**：ImportDialog 改走 `compileJson(schema, probe) → render`，删除整套手绘 bool/int/enum/source 动态列逻辑，预览与警告渲染保留。原卡点是 CSV 时间列的"显示列名 / 回传列下标"冲突——v2 用 select 的 value→label 分离 + `{from}` 动态来源 + `defaultFrom` probe 预填解决（见 `docs/develop/uischema-syntax.md`）。该遗留项已闭合。

---

## 6. 拍板追溯（需求方 2026-11-19 已定）

- ✅ 三条主线开工；实现由架构师（本 AI）亲自执行。
- ✅ 命名：`SourceRegistry` / `Domain`（通用名）/ `Toolbox` / 取消 `IFormatDriver`（并入 `IMessageParser`）/ `RecordingFormat` 保持 / 否决 `IParser`+`IDataParser`。
- ✅ schema = 纯 metadata（变量名/类型/可选值/值域/单位/依赖/说明），自研 `SchemaEngine` 解析生成 UI。
- ✅ 解析器不绑定下游用途、多输出函数、输出经 Sink 交调用者。
- 架构师修正（§2.2–§2.5）：注册表泛型克制、SPI 不碰、两对话框不合、`ParamSpec` 补 `optionsFrom`——**需需求方知悉**，若反对请在开工前提出；未提出则按修正执行。

---

## 7. v2 升级：core 消亡 + 架构评审条款（2026-11-19 追加，本节为终态裁决）

### 7.1 需求方追加裁决

> "core 只是一个核心，不要把 core 当成所有代码的实现，相当于上帝文件夹了。"
> "其实 core 我都不想要的，我觉有意义的就一个 appcontext……其他抽象的实现，应该尽量放在 libs。"
> "libs 不允许实例化对象（除非是静态对象），只能是函数，或者 class 等。"
> "你是架构设计，最终你的意见为准。"

**终态裁决**：`ng/core` 目录**彻底消亡**，其内容全部按机制下沉 libs；`AppContext` 是唯一被认可的运行态容器，归 `libs/context`，但必须遵守 §7.3 的 libs 铁律。§1–§6 的 B1–B6 是本终态的**前段批次**，本节是**后段批次（B7+）与不可退让的纪律**。

### 7.2 为什么 AppContext 不能进 app 文件夹（架构师裁决，需求方已同意）

需求方最初提议"AppContext 放 app 文件夹"，架构师**反对并已获同意**，两条硬约束：

1. **依赖倒挂**：`app` 是最终可执行目标（`add_executable(plotscope-ng)`），而 `AppContext.h` 被 host / gui / modules / extensions / tests 共 20+ 处 include。下层库不能 include 可执行目标里的头，也不能链接它。
2. **成环**：`app/App.h` 的 `App` 持有 `ModuleHost`（host 层），而 host 层的 `IModule.h` / `ModuleHost.h` 又 include `AppContext.h`。若两者同住一个库，形成 `host ↔ app` 循环。

**落法**：`libs/context` 只提供 `class AppContext` 的**定义**；唯一实例在组合根 `App` 里创建（`AppContext m_ctx`）；host / gui / modules / extensions 通过**构造注入**显式拿到引用，不再 include 一个全局 ctx 用 `ctx.data()` 随手取——这正是 §7.3 铁律要根治的"服务定位器"反模式。

### 7.3 libs 铁律（需求方立，架构师界定边界，不可退让）

> **一句话判据：libs 是无隐藏运行时状态的能力提供者。谁要用谁在上层实例化，生命周期归组合根。**

| 类别 | 允许 / 禁止 |
|---|---|
| 函数、算法、模板 | ✅ 允许——纯逻辑 |
| class / struct **定义**（含其成员变量，随实例生死） | ✅ 允许——类型蓝图和它的组成部分 |
| 常量、枚举、类型别名 | ✅ 允许 |
| **static 自注册对象** | ✅ 允许——工程既有 OBJECT 库自注册惯例（BuiltinContributions / 模块 / 扩展注册表） |
| 全局 / 命名空间级对象实例（跨调用存活状态） | ❌ 禁止 |
| 单例（Meyers、函数内 static 实例） | ❌ 禁止 |
| 服务定位器式"活实例"被任意 include 取用 | ❌ 禁止 |
| libs 内部 new / 持有跨调用存活对象 | ❌ 禁止 |
| libs 依赖上层 / `#include "../"` 逃逸库目录 | ❌ 禁止 |

**边界澄清（需求方已确认）**：`AppContext` 内部的 `DataRepository m_repo;`、`SettingsStore m_settings;` 等**成员对象合规**——它们是类型的组成部分，随实例生死。禁止的是"全局活实例 + 服务定位器取用"，不是禁止 class 拥有成员。

### 7.3.1 边界已做成构建期守卫（让违规编译不过）

本节铁律不只写在文档里，已落为可执行守卫：

- **`ng/cmake/LibBoundaryGuard.cmake`**：configure 期扫描，三条硬失败——
  ① libs 内 `#include "../"` 逃逸库目录；② libs target 链接上层（core/host/gui/modules/app/extensions）；③ Meyers 单例 `static T& instance()`。
  已反向验证：注入违规头 → `FATAL_ERROR`，删除后恢复通过。
- **`ng/AGENTS.md` §5.1**：libs 铁律表 + AppContext 特别规定，供后续 AI 编码前必读。
- 正则不可靠的三项（普通全局对象实例、服务定位器取用、static 自注册例外）**故意不拦**，交 code review + ctest；守卫只加严不加豁免。
- `core` 命名空间残留暂为 WARNING（B7 收口前是合法中间态），B7 完成后改为 FATAL。

### 7.4 架构评审四条约束（架构师对方案本身的保留，随方案一并执行）

1. **接口稳定**：public header 就是契约，改签名 = breaking change。进 libs 的接口宁可多花两轮定对——这是 §2 反复推敲接口边界的意义。
2. **测试底座**：每个 lib 有自己的契约测试钉死行为。"靠谱"不是口头承诺，进库前先立 ctest。真正危险的不是签名破坏（编译器会抓），是**语义破坏**——签名没变但行为变了，消费者静默出错。
3. **依赖单向**：严格按 §7.5 分层，库之间不回头。否则升级一个 lib 拖一片，"升级就受益"的杠杆失效。
4. **演进有路**：接口真要变时留兼容层或版本化，让消费者平滑升级，不一刀切。

> 补充：静态库世界"升级 libs"= 全量重编，没有热替换；"靠谱 + 直接升级"的收益要靠 2、3 两条兑现，不是自动到账。

### 7.5 终态依赖分层（无环，libs 铁律的载体）

```
foundation(Result)
  → 机制库 dataset / registry / intent / uischema / settings / parser
  → context(AppContext 聚合容器，唯一实例在 app)
  → host(模块宿主)
  → modules / extensions / gui
  → app(App 组合根，持有 AppContext + ModuleHost)
  → plotscope-ng(薄 main)
```

**控制库数量**：目标 6–7 个 lib，警惕"为拆而拆"。`SettingsStore` 归 `context`（本就是运行态一部分）；`Selection` / `Transform` 是纯数据契约，并入 `dataset` 或 `uischema`，不单列。

### 7.6 core 内容归属映射表

| 现有 core 内容 | 去处 | 批次 |
|---|---|---|
| Dataset / DataRepository / DataSource / DataSourceRepository（含 `SourceRegistry` 改名） | `libs/dataset` | B2 |
| Intent / IIntentExecutor / JobRecorder | `libs/intent` | B4 |
| ExtensionRegistry / *Contribution / UiSlot / PanelFactory / BuiltinPack / FormatDomain | `libs/registry` | B3 |
| IMessageParser（合并 IFormatDriver）/ ParserRegistry / Ingest* / IByteSource | `libs/parser` | B6 |
| PropertySchema / Selection / Transform | `libs/uischema` | B5 |
| SettingsStore / WorkspaceDataConfig | `libs/context` | B7 |
| **AppContext** | **`libs/context`（构造注入）** | B7 |
| ScriptFacade | 随 script 模块或 `libs/context` | B7 |
| Events / Result | `libs/foundation` | B1 |
| RecordingFormat / RecordingBackend | `recording` | 已在 |
| 5 类 register*/dispatch*（viewAction/property/transformEval/transformScriptOps） | `host`（`InteractionRegistry`） | B7-0，见 §7.9 |

### 7.7 后段批次与命名空间纪律

- **B7（core 消亡批）**：迁 `AppContext` / `SettingsStore` / `WorkspaceDataConfig` 到 `libs/context`，改全仓 include 为构造注入；迁空后**删除 `ng/core` 目录与 target**，全仓移除 `target_link_libraries(... ng_core ...)`，并把守卫里的 `core` namespace WARNING 升级为 FATAL。
- **命名空间纪律（修正 §5.4）**：不再"namespace 永远保持 core 另批统一"。**每个 lib 搬迁时连同 namespace 一起改**（`core` → `context`/`dataset`/`registry`/…），避免"目录叫 context、namespace 还叫 core"的最坏中间态。B1–B6 已落地的 `core` 命名空间在 B7 统一收口。
- **垂直切分**：后段按"一个 lib 一个 lib 搬 + 带自己的契约测试 + 带自己的消费者"垂直推进，每批单独过 ctest，不做"先搬所有 Result 再搬所有 dataset"的水平切——big-bang 水平切中途全是红色、插不进别的需求，11 个 ctest 也兜不住。
- **B7 验收**：全仓无 `ng/core` target、无 `nextpilot::plotscope::core` 命名空间残留、无 `ctx.xxx()` 全局服务定位器取用（改为注入）；Release + Debug ctest 11/11。

### 7.7.1 实现复核：B2 的 namespace 保持 core（架构师裁定）

> 冲突：专项文档 §4 说"dataset 抽库保持 core namespace、R-D2 另批"，总方案 §7.7 说"每个 lib 搬迁时连 namespace 一起改"。

**B2 裁定：物理抽库先行，namespace 暂保持 `core`，统一在 B7 一次性收口。** 依据：

1. B1 的 `Result.h` 已在 `ng_foundation` 但仍用 `core` namespace。若 B2 单独把 dataset 改成 `dataset` namespace，会出现 `dataset::DataRepository` 返回 `core::Result<dataset::DatasetId>` 的**跨命名空间割裂**，比现状更乱。
2. dataset 的 34 个引用文件里 `core::Dataset` / `DatasetId` / `ValueType` 字面量遍布，B2 就改会与 B6 命名收敛（`SourceRegistry`/`Domain` 等）叠加，改动面翻倍、回归难定位。
3. §7.7 的初心是避免"目录叫 context、namespace 还叫 core"的**终态**中间态；B2–B6 是渐进批，B7 一次性把 Result + Dataset + AppContext 等所有 core namespace 收口到各自库名，才是零割裂路径。

**这条不违背 §7.7**：namespace 统一照做，只是集中在 B7 而非摊到每批。B7 验收项（无 `core` namespace 残留）仍是硬指标。

### 7.7.2 B8：Transform 输入控件重构（需求方 2026-11-20 追加）

需求方否决当前 `TransformEditorDialog`（一个弹窗塞满算子/表达式/Lua/参数/单点/批量调试的形态），要的是"输入变换时"的轻量控件。三层裁决：

1. **控件形态**：凡要输入变换处放一个 `TransformField`——可编辑下拉框（选已有算子并展示说明）+ 直接敲文本当数学表达式 + 右侧编辑按钮进 Lua Editor。输出仍是同一份 `TransformRequest` JSON，`TransformEvaluator` 零改动。
2. **能力过滤保留**：控件构造传 `cap`，列表只列 `byCapability(cap)` 的项；运行时 evaluator 二次校验。防"选错就死"不打折。
3. **调试区抽出来复用**：现有单点/批量调试代码抽成 `LuaEditor` 共用，不重写。

**脚本存哪（架构师裁定：文件为真源 + 配置带快照）**：

- Lua Editor 保存 = 写 `.lua` 文件到 `scripts/ops/` → 注册 `lua.<name>` descriptor → 回填控件。
- 配置里 `scriptId`（来源）+ `luaSource`（快照）都写。有 `scriptId` 以文件为准、加载时刷新快照；文件丢失用快照兜底，不让配置变废。`scriptId` 为空 = 纯内联草稿。
- 理由：配置 JSON 跨模块传递、可能拷到别机，纯引用会因脚本挪动悬空；快照保证自包含，同时不丢"文件是真源、可复用"的收益。当前 `evalLua` 本就只吃 `luaSource` 字符串、由调用方解析 `scriptId`，规则正好接上。

**算子说明从哪来（架构师裁定：脚本导出 `describe()`，否决头注释与旁车文件）**：

- 脚本导出 `function describe()` 返回 table：`label` / `description` / `capabilities` / `example` / `params`（按 `ParamSpec` 字段写）。
- 加载器：执行脚本 → 调 `describe()` → table 转 `QJsonObject` → 填 `TransformDescriptor`（id=`lua.<name>`）注册。`params` 直接喂 `SchemaEngine` 生成参数区。
- 无 `describe()` 的老脚本降级：文件名当 label、描述空、能力全开，不报错。
- `describe()` 必须在 `LuaEngine`/`ScriptFacade` 沙箱里调，只构造 table、不跑主逻辑。builtin 仍走 C++ 硬编码注册，不强求统一。
- 否决头注释 `@label`（要写注释解析器、易漂移）与旁车 `.meta.json`（两份文件要同步、用户嫌烦）。

**边界**：`TransformField`/`LuaEditor` 在 gui；脚本文件读写/解析/注册在 `modules/script`（有状态，不进 libs，符合 libs 铁律）；迁移老代码 `ScriptLibrary`（scripts/lib、ops、batch + resolve/invalidate）思路，不另起炉灶。依赖 B5 的 uischema 渲染层。

**实施状态（✅ 2026-11-20 完成，分两子批）**：
- **B8-a（`LuaScriptLibrary`，script 模块）**：裸 lua_State 只开 base/table/string/math，指令钩子防死循环；`describe()` 导出 table → QJson → descriptor（`lua.<name>`），无 `describe()` 老脚本降级；Lua table→QJson 反向转换区分数组/对象；`save` 写文件+注册，`resolveSource` 文件为真源/快照兜底，脚本名安全校验。headless 测试覆盖元数据/降级/语法错/死循环/沙箱隔离/保存注册/loadAll/非法名。
- **B8-b（GUI）**：`TransformField` 可编辑下拉 + uischema 参数区 + 编辑按钮；`LuaEditor` 代码编辑 + 单点/批量调试（复用旧逻辑）+ 保存注册；`TransformEditorDialog` 退为薄壳，4 个调用点 `run()` API 零改动。脚本能力经 `TransformScriptOps` 回调注入 AppContext，gui 不依赖 script 模块。顺带修 B5 遗留缺陷：`transformParamSpecs` 的 number 参数因 QDoubleSpinBox 默认下限 0 截断负值，补宽松范围。
- **验收**：`ng_lua_script_library`（headless）+ `ng_transform_field`（offscreen）两个新测试；Release+Debug ctest 18/18 双绿；libs 边界守卫通过。

### 7.8 拍板追溯（需求方 2026-11-19 追加）

- ✅ core 消亡，内容全部下沉 libs；AppContext 归 `libs/context`。
- ✅ libs 铁律：无全局活实例 / 单例 / 服务定位器；成员对象合规；static 自注册对象例外。
- ✅ AppContext 不进 app 文件夹（依赖倒挂 + 成环），改构造注入。
- ✅ 架构师四条约束（接口稳定 / 测试底座 / 依赖单向 / 演进有路）随方案执行。
- ✅ 边界已固化为 CMake configure 期守卫（§7.3.1），违规编译不过。
- ✅ "最终意见以架构师为准"——本节边界由架构师界定，需求方已逐条同意。

### 7.9 core / host 职责定位与 register* 归位（架构师裁决 2026-11-20）

**定位一句话**：core 不是层，是待消亡的静态机制暂存区；host 是运行时编排层，保留。

| | core | host |
|---|---|---|
| 本质 | 静态机制/契约集合、未分类区 | 运行时编排者 |
| 边界 | 无，什么都能塞（故易成上帝类） | 模块生命周期 + 意图路由 |
| 状态/时序 | 无 | 有：装载 → 激活 → 卸载 |
| 依赖身份 | 被所有人依赖（最底） | 依赖 context+libs，被 modules/gui 依赖 |
| 终态 | 消亡，内容分流进 libs | 保留 |

组合根 `App` 持 `AppContext`（数据）+ `ModuleHost`（编排），两者是"有什么"与"怎么调度"的关系。

**AppContext 里的 register* 是职责污染**（上帝类的直接成因）。逐点核对注册方/消费方：

| 扩展点 | 注册方 | 消费方 |
|---|---|---|
| `registerViewAction` | `gui/views/plot/*` | GUI ribbon |
| `registerPropertyProvider/Setter` | `modules/data`、`gui/views/plot` | GUI 属性面板 |
| `registerTransformScriptOps` | `modules/script` | `gui/LuaEditor` |
| `registerTransformEvaluator` | `modules/script` | host / gui / protocol |

这 5 类都是"注册 + 派发 = 运行时路由"，属 host 编排范畴，不是静态运行态数据。

**归位裁决**：
- 5 类 register*/dispatch* 从 `AppContext` 剥离 → `ModuleHost` 持有的独立 `InteractionRegistry`（headless，`std::function`+`QHash`，符合 libs 铁律）。各注册点由 `appCtx.registerX` 改走 `ModuleContext::host()`。
- 落 host 而非 gui 的依据：注册方含 `modules/data`、`modules/script`，module 不能依赖 gui（依赖倒挂）；`ModuleContext` 同时持有 `app()`/`host()`，host 是 module 与 gui 共有的最低公共层；消费方在 gui，向下够得着 host。
- `AppContext` 只留**静态运行态数据**：repo / settings / toolbox / sources / ingest + activeChannel / selection + 信号。
- `activeChannel` / `selection` **留在 context**：它们是跨前端共享的**状态**（CLI/脚本/GUI 共用），与"行为注册"性质不同；挪进 gui 会让 CLI 失去共享通道。

**B7 顺序修订**（前置剥离，避免把 UI 关注点固化进 libs）：
1. **B7-0**：剥离 5 类 register* → `ModuleHost::InteractionRegistry`，改各注册点走 `host()`。
2. **B7-a/b**：迁纯数据 `AppContext` → `libs/context`，构造注入。
3. **B7-c**：namespace 收口 + 删 `ng/core` + 守卫 core WARNING → FATAL。
