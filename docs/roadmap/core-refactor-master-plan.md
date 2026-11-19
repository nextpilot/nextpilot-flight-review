# Core 重构总方案：机制抽库 + 解析器合并 + 命名收敛（2026-11-19 开工版）

> **状态：✅ 需求方已拍板三条主线，架构师已做合理性判断，本轮起开工实现。**
> 需求方原话："写成 roadmap 方案，然后开工：① schema、register、intent、dataset/repo 抽象成库；② IMessageParser 合并、去掉 IFormatDriver；③ 部分命名。你是架构师，要判断合理性，我是需求方。"
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
| **B2** | 新建 `ng_dataset`，迁 `Dataset`/`DataRepository`，核心 PUBLIC 依赖 | `libs/dataset/`、`core/CMakeLists.txt`、`fastplot_dataset_test` | "只链 ng_dataset 不链 ng_core" 的复用测试通过；11/11 |
| **B3** | `ng_registry` 最小 CRUD 核 + 自注册助手；至少 1 套注册表薄壳化验证 | `libs/registry/`、选 `PanelFactoryRegistry` 先行 | 对外 API 不变；11/11 |
| **B4** | `ng_intent` 移出 5 个意向类型 | `libs/intent/`、`core/`、`AppContext`/`ScriptFacade` | Job JSON 往返测试；11/11 |
| **B5** | `ng_uischema`：纯 metadata 契约 + `SchemaEngine` + 唯一 Qt 渲染器；两个对话框共用渲染器 | `libs/uischema/`、`PropertyPanel`/`TransformEditorDialog`/`ImportDialog`/`ProtocolInspectorDialog` | 全仓一处 `FieldType→QWidget` 映射；11/11 |
| **B6** | 取消 `IFormatDriver` + 三个门面类，统一 `IMessageParser`（多输出函数）；命名收敛 `SourceRegistry`/`Domain`/`Toolbox` | `core/`、`extensions/`、`ExtensionRegistry`、`ARCHITECTURE.md` | 无 `IFormatDriver`/`IDataParser`/`XxxFormatDriver` 门面类型；无旧命名残留；11/11 |

**每批硬指标**：Release + Debug 双配置 `ctest` 11/11 全绿；新库领域纯净（无飞行/CAN/姿态词）；headless 可编译。

---

## 5. 风险与红线

1. **基线保护**：每批前确认工作区干净、当前 11/11 全绿；每批后独立提交（便于回滚单批）。
2. **接口等价**：抽库/泛型化期间对外 API 保持不变，调用方零改动；`using` 兼容别名先留一版。
3. **ABI 红线**：B6 不碰 SPI vtable、`abi_version`、插件加载闸门。
4. **命名空间**：dataset 抽库保持 `nextpilot::plotscope::core`（B2 只物理抽库），全局改命名空间另批。
5. **不动项**：器官拆分（`mod.source`/`mod.dataset`）、report 成品、SPI 合并、两对话框合一——本轮**明确不做**。
6. **实现方异议**：如某批发现与裁决冲突的事实，在本文追加"复核意见"，不擅自删改裁决项，先报需求方。

---

## 6. 拍板追溯（需求方 2026-11-19 已定）

- ✅ 三条主线开工；实现由架构师（本 AI）亲自执行。
- ✅ 命名：`SourceRegistry` / `Domain`（通用名）/ `Toolbox` / 取消 `IFormatDriver`（并入 `IMessageParser`）/ `RecordingFormat` 保持 / 否决 `IParser`+`IDataParser`。
- ✅ schema = 纯 metadata（变量名/类型/可选值/值域/单位/依赖/说明），自研 `SchemaEngine` 解析生成 UI。
- ✅ 解析器不绑定下游用途、多输出函数、输出经 Sink 交调用者。
- 架构师修正（§2.2–§2.5）：注册表泛型克制、SPI 不碰、两对话框不合、`ParamSpec` 补 `optionsFrom`——**需需求方知悉**，若反对请在开工前提出；未提出则按修正执行。
