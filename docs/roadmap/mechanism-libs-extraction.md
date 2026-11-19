# 机制抽库设计：recording 归属 + 注册表/意向/schema→UI 下沉为 libs

> 状态：**已拍板，分批实施中**（2026-11-19 定方案；2026-11-20 落地 B3 `ng_registry`、B4 `ng_intent`、B5 `ng_uischema` 首批、B6 解析器统一+命名收敛、B8 Transform 输入控件重构，见 §11；B7 core 消亡待办；uischema 的 ImportDialog 接入见总方案 §5.2）。
> 需求方三问：① `ng/recording` 为何独立、有何特权？② core 的注册机制（模板、支持多种类）与意向机制抽成 libs；③ 按 schema 让 Qt 绘制对话框/属性面板的能力抽成 libs，重点是**约束 schema 长什么样**。
> 与 [`dataset-lib-extraction.md`](dataset-lib-extraction.md) 同属 core 瘦身动作；所有结论逐行核对过 `ng/core`、`ng/recording`、`ng/gui`。

---

## 0. 一句话结论

1. **recording 没有特权**：它独立是因为"格式 × 后端"是与 core 正交的关注点，独立正确；真正的瑕疵是**接口在 core、实现在 recording 的分家**，应把接口也归位到机制库。
2. **注册表与意向机制可抽**：注册表做成**一个泛型模板** `Registry<T>` 支持多种条目类型，各具体注册表退化为薄配置；意向机制的 `Intent/Job/IIntentHandler/IIntentExecutor` 全是纯契约，天然可独立成库。
3. **schema→UI 抽库是最大收益点**：现在 `PropertySchema`（6 种）、`TransformParam`（3 种）、`IFormatDriver::importSchema`（5 种 JSON）、`IMessageParser::configSchema` **四套各画各的 UI**，抽库必须先**统一 schema 契约**——这是本文重点（§4）。

---

## 1. `ng/recording` 为什么独立？有没有特权？

**结论：没有特权，独立是正当的，因为它是一个正交关注点。**

`ng/recording` 实现的是"**格式（成帧）× 后端（输出）的正交组合**"：

- 格式侧：`RawCodec`（原始字节）、`McapCodec`（MCAP 容器）——决定字节长什么样；
- 后端侧：`Sink.cpp`（`FileSink` / `UdpSink`）——决定字节送到哪；
- `Recorder` 把两者按 `(formatId, backendId)` 查表组合，多路录制 = 多实例。

它依赖 `Qt6::Core` + `Qt6::Network`（UDP 后端需要），逻辑自成一体、不依赖 host/modules/gui。这与 `libs/protocol`（解码内核）、`libs/fastplot`（绘图）是**同一类东西：独立机制库**。独立目录不是特权，是关注点分离。

**但有一个真实瑕疵需要修**：抽象接口 `RecordingFormat.h` / `RecordingBackend.h`（含注册表 `registerFormat`/`registerBackend`）放在 **core**，而实现 `RawCodec/McapCodec/Sink` 放在 **recording**——接口与实现分家。后果：

- core 被录制机制污染（core 本应只有进程级运行态与纯契约）；
- 录制格式/后端注册表与 core 的其它 4 套注册表一样，散落各处、无法复用同一套注册机制（见 §2）。

**裁决**：把 `RecordingFormat.h/.cpp`、`RecordingBackend.h/.cpp` 从 core 移入机制库（与 §2 的注册表库合并归属），recording 改名/归入 `libs/recording` 或并入机制库的一个子模块。接口与实现同处一层，依赖方向变干净：`libs/recording → 机制库（注册表模板）+ core 的 Result`。**这正是"消除特权错觉"的做法：让它按 libs 的规矩办，接口实现不分离。**

> 另注：core 里的 `RecordingFormat.h` 注释已明确"录制格式 ≠ 贡献点（extension）"——它是 app 机制选项，不含领域知识。这进一步支持它应属**机制库**而非 core/extension。

---

## 2. 注册机制抽库：一个泛型模板支持多类型

**现状：5 套注册表，各写各的。**

| 注册表 | 文件 | 条目类型 |
|---|---|---|
| ParserRegistry | `core/include/ParserRegistry.h` | `ParserEntry`（info + vtable + 插件句柄） |
| ExtensionRegistry | `.../ExtensionRegistry.h` | `IToolPack` / `IToolContribution`（二维 kind×domain 索引） |
| PanelFactoryRegistry | `.../PanelFactoryRegistry.h` | `Factory`（QWidget 构造函数） |
| BuiltinPackRegistry | `.../BuiltinPackRegistry.h` | `PackFactory`（编译期自注册） |
| RecordingFormat/Backend | `.../RecordingFormat.h`、`RecordingBackend.h` | 描述结构 |

共性：`register(T) → bool`、`ids() → QStringList`、`get(id) → T*`、`remove(id) → bool`，外加"静态自注册 + 插件句柄"两组可选能力。差异：条目类型、是否二维索引、是否持有句柄。

**裁决：抽 `ng/libs/registry`，提供一个泛型核心 + 两个可选 mixin。**

```
template <typename T, typename Key = QString>
class Registry {                          // 核心：id → T 的登记/查询/移除/遍历
    bool insert(const Key &id, T entry);
    bool remove(const Key &id);
    T *find(const Key &id) const;
    QList<Key> keys() const;
    int count() const;
};

// 可选能力（组合，不继承污染核心）：
template <typename T> class IndexedByKind;   // 二维索引：kind → 条目列表（ExtensionRegistry 用）
template <typename T> class OwnsPluginHandle; // 插件句柄 RAII（ParserRegistry 用）
```

- 各具体注册表退化为**薄配置**：`using ParserRegistry = Registry<ParserEntry>` + `OwnsPluginHandle<ParserEntry>`；`ExtensionRegistry = Registry<IToolPack*>` + `IndexedByKind<IToolContribution*>`。
- **自注册宏**（`NG_REGISTER_PARSER` / `NG_REGISTER_PACK` / `NG_REGISTER_MODULE`）归位到机制库的 `StaticAutoRegistry<T>`，统一实现"静态初始化期登记 + 全局表"这一套（现分散在 `BuiltinPackRegistry`、`host::BuiltinModuleRegistry`、parser 宏）。
- 依赖：只 Qt Core + STL，**无 QWidget、无 Rust**，headless 可用。命名空间 `nextpilot::plotscope::registry`（遵循 libs 惯例）。

> 风险控制：泛型改造是**接口等价重构**，各注册表的对外 API 保持原样（内部委托给模板），调用方零改动。可分"先抽出薄壳、后迁内部实现"两步，降低一次性风险。

---

## 3. 意向机制抽库：纯契约，天然独立

**现状**：`Intent.h`（`Intent` / `IntentResult` / `IIntentHandler` / `Job`）、`IIntentExecutor.h`（`IIntentExecutor`）全在 core，但内容**全是纯契约 + 纯数据**——`QJsonObject/QVariantMap/QString`，无 QWidget、无宿主实现、无领域词。真正的分发/执行在 host 与 gui/shell（`ShellBinder`、`MainWindow`）。

**裁决：抽 `ng/libs/intent`，整体移入这 5 个类型。**

- `Intent` / `IntentResult` / `Job`（可序列化意图序列，GUI 录制与 CLI 执行的公共货币）；
- `IIntentHandler` / `IIntentExecutor`（抽象契约，core/host/extension 都依赖它而非具体实现）；
- `Result` 依赖：与 dataset 抽库同理，`Result.h` 已下沉 `ng_foundation`（见 `dataset-lib-extraction.md` §3），intent 库 PUBLIC 依赖 foundation 即可，无循环。

依赖：Qt Core + `ng_foundation`。命名空间 `nextpilot::plotscope::intent`。**core 反向依赖 intent 库**（`AppContext`、`ScriptFacade` 用 Intent），方向单一，无循环。

---

## 4. schema→UI 抽库：先统一 schema 契约（重点）

需求方点明"**主要是约束 schema 该什么样**"——这是关键。仓库里其实有**四套各画各的 schema**：

> **方向更新（2026-11-19，§10）**：需求方进一步要求"取消手写 schema、框架按元信息自动生成"。本节的 `FormSchema`/`FieldType` **产物契约仍有效**，但生产方从"各实现手写 schema"改为"实现只写 `ParamSpec[]`、框架生成 `FormSchema`"——详见 §10。

| 来源 | 文件 | 字段/控件类型 | 绘制端 |
|---|---|---|---|
| `PropertySchema` | `core/include/PropertySchema.h` | `PropKind` 6 种；`PropRow { key,label,value,kind,editable,unit,options,tooltip }` | `PropertyPanel.cpp` QTreeWidget 手绘 |
| `TransformParam` | `core/include/Transform.h` | `type: "number"/"string"/"bool"` 3 种魔法字符串 | `TransformEditorDialog.cpp` QFormLayout 手写 switch |
| `IFormatDriver::importSchema` | `core/include/IFormatDriver.h` | JSON：`{title, fields:[{key,type,label,default,options?,min?,max?}], ui:{...}}`，`type ∈ bool/int/double/enum/string/file` | `ImportDialog.cpp` 手绘（`:301/:468`） |
| `IMessageParser::configSchema` | `core/include/IMessageParser.h` | 注释明确"与 `IFormatDriver::importSchema` 同构" | 协议检查对话框 |

绘制端也分裂：

- `PropertyPanel.cpp` 用 **QTreeWidget 手绘**（`setData(1, kRoleKind, ...)`，靠 role 存 kind，无统一 widget 工厂）；
- `TransformEditorDialog.cpp` 用 **QFormLayout + 手写 switch**（`if type=="bool"` → QCheckBox，else QLineEdit）——只支持 2/3 种，且魔法字符串判断。

**这就是要抽库的根本理由**：不先统一 schema，抽库只会把两套绘制逻辑搬进同一个库，仍是两份。

### 4.1 统一 schema 契约（本设计的核心产出）

抽 `ng/libs/uischema`，定义**唯一**的字段描述结构，取代上述两者：

```cpp
enum class FieldType {          // 控件类型：穷举，不开放魔法字符串
    Text, Number, Bool, Enum, Color, File, ReadOnly
    // Number 覆盖 importSchema 的 int/double；File 覆盖其 "file" 类型（路径选择）
};

struct FieldSpec {              // 一个字段 = 一行可编辑/只读项
    QString key;                // 稳定键（编辑回传）
    QString label;              // 展示名
    QString type;               // 用 FieldType 的序列化名，不用散字符串
    QVariant value;             // 当前值（类型化，不再全靠字符串化 Parse）
    bool editable = true;
    QString description;        // tooltip / placeholder（统一 description，不再是 description+tooltip 两份）
    QVariant defaultValue;      // 缺省值（TransformParam 需要）
    QString unit;               // 数值单位
    QStringList options;        // Enum 候选
};

struct FormSchema {             // 一份表单/属性文档
    QString title;
    QVector<FieldSpec> fields;
    QString selectedId;         // 属性面板选中项（可选）
    QJsonObject toJson() const;
    static FormSchema fromJson(const QJsonObject &);
};
```

**约束规则（"schema 该什么样"的硬性条款）：**

1. **类型穷举**：控件类型只能是 `FieldType` 枚举值，禁止 `"number"/"string"/"bool"` 这类散字符串——新增控件类型必须改枚举 + 控件映射表（受控扩展）。
2. **类型化值**：`value` / `defaultValue` 用 `QVariant`，携带真实类型（int/double/bool/QColor），不再"字符串化展示 + parse 还原"（现 `PropRow::value` 是字符串，易错且丢类型）。
3. **单一描述字段**：`description` 同时作 tooltip 与 placeholder，取消 `PropRow` 里 `tooltip` 与 `TransformParam` 里 `description` 的双轨。
4. **键稳定、可回传**：`key` 是编辑意图的回传键；`FormSchema` 不含任何 QWidget 概念，纯数据、可 JSON 往返、headless 可见。
5. **意图约定统一**：`prop.schema`/`prop.set`（属性面板）与变换参数编辑共用一套 op 常量，参数键统一为 `{source, object, key, value}`，不再各写一套。

### 4.2 库结构：契约 + Qt 渲染器分离

```
ng/libs/uischema/
  include/uischema/FormSchema.h      # 纯数据契约（Qt Core only，headless 可用）
  include/uischema/FieldWidgetFactory.h  # 控件工厂接口：FieldType → QWidget*
  src/FormSchema.cpp                 # JSON 往返
  src/QtFieldRenderer.cpp            # Qt 实现：按 FieldSpec 生成 QWidget（QCheckBox/QLineEdit/QSpinBox/QComboBox/颜色按钮）
```

- **契约层**（`FormSchema.h`）：无 QWidget，CLI 可查询"某对象有哪些字段"。
- **渲染层**（`QtFieldRenderer.cpp`）：唯一一处 `FieldType → 控件` 映射，杜绝两套 switch。依赖 Qt Widgets，仅 GUI 侧链接。
- **消费者改造**：`PropertyPanel` 改用 `FormSchema` + 渲染器；`TransformEditorDialog` 把 `TransformParam` 适配成 `FieldSpec`（或让 `TransformDescriptor` 直接产出 `FormSchema`）；`ProtocolEditorDialog` 同理。

依赖：契约层 Qt Core + `ng_foundation`；渲染层 Qt Widgets。命名空间 `nextpilot::plotscope::uischema`。

---

## 5. 汇总依赖图（目标态）

```
ng_foundation (INTERFACE, Result.h)
    ▲            ▲             ▲
    │            │             │
ng_registry   ng_intent   ng_uischema(契约层)
    │            │             │
    └────────────┼─────────────┘
                 │
        ng_core (AppContext/ScriptFacade/Selection/PropertySchema 适配层)
                 ▲
    ┌────────────┼─────────────┐
ng_dataset   ng_recording   GUI/ modules / host / extensions

渲染层 uischema::QtFieldRenderer (Qt Widgets) ── 只 GUI 链接
```

---

## 6. 验收标准

1. **注册表**：5 套注册表全部委托同一 `Registry<T>` 模板；各对外 API 不变；新增一种注册表只需 `using` 一行 + 选 mixin。
2. **意向**：`Intent/Job/IIntentHandler/IIntentExecutor` 从 core 移出后，core/host/gui/extension 编译通过；GUI 录制→CLI 回放的 Job JSON 往返测试通过。
3. **schema 唯一**：全仓只有**一个** `FieldType` 枚举、**一处** `FieldType → QWidget` 映射；`PropertySchema`、`TransformParam` 已适配为 `FormSchema`（或标注为兼容别名）；无魔法字符串判断类型。
4. **零回归**：`ctest` 现有 11 项全绿（Debug/Release 双配置）。
5. **领域纯净**：三个新库接口无飞行/姿态/CAN/电池等领域词；headless 可编译链接。

---

## 7. 落地批次

| 批 | 内容 | 依赖 | 验收 |
|---|---|---|---|
| M1 | 下沉 `ng_foundation`（Result） | — | 与 dataset 抽库共享前置，先做一次即可 |
| M2 | `ng_registry` 泛型模板 + 5 套注册表薄壳化 | M1 | §6 第 1 条 |
| M3 | `ng_intent` 移出意向机制 | M1 | §6 第 2 条 |
| M4 | `ng_uischema` 契约 + 统一 `FormSchema` + Qt 渲染器；三处消费者改造 | M1 | §6 第 3 条 |
| M5 | recording 接口归位（`RecordingFormat/Backend` → libs），与 M2 注册表合并 | M2 | §6 第 5 条 + §1 裁决 |

> 与 R0（命名）、R-D1（dataset 抽库）共享 **M1（Result 下沉）** 这一前置——**M1 只需做一次**，三者都受益。建议顺序：M1 → R0 → R-D1 → M2 → M3 → M4 → M5。

---

## 8. 待需求方拍板项

1. **recording**（§1）：确认"独立正当、但接口归位到机制库"的裁决？是否接受目录改名 `ng/recording` → `libs/recording`？
2. **注册表泛型化**（§2）：确认用一个 `Registry<T>` + mixin 统一 5 套？分两步（薄壳→迁内部）是否可接受？
3. **意向抽库**（§3）：确认 `ng/libs/intent` 边界（5 个类型全移）？
4. **schema 统一**（§4，重点）：确认采用 `FormSchema`/`FieldSpec`/`FieldType` 契约与 5 条约束？特别是——
   - `PropKind` 与 `TransformParam::type` 合并为穷举 `FieldType`（新增控件类型需改枚举，受控扩展）——是否接受？
   - `value` 从字符串改为 `QVariant` 类型化——涉及已有 `prop.set` 意图的值解析，是否本批一起改？
5. **批次顺序**（§7）：确认 M1 前置只做一次、按 M1→R0→R-D1→M 序列推进？

---

## 9. `format` 概念辨析：一个词，四个意思（需求方 2026-11-19 追问）

需求方问：`FormatToolbox`、`FormatDomain`、`IFormatDriver`……这些 `Format` 到底是什么意思？逐文件核对后结论：**`format` 在仓库里被用在了四个互不相同的概念上，其中三个是历史误命名（F1/F2/F3），只有 F4 名实相符。** 需求方已于 2026-11-19 对四者全部拍板（见 §9.6）。

### 9.1 四个 `format` 概念

| # | 概念 | 代码落点 | 真实含义 | 需求方裁决（2026-11-19） |
|---|---|---|---|---|
| F1 | **数据格式解析** | `IFormatDriver.h`、`CsvFormatDriver`、`UlogFormatDriver`、`CustomProtocolFormatDriver` | "能把某种文件解析成 Dataset 的能力"，声明 `parserId()` + 表名 + 导入 schema | ✅ **取消门面，并入 `IMessageParser`**：统一为单一解析器接口，多输出函数（见 §9.3 / §10.3.1） |
| F2 | **业务领域** | `FormatDomain.h/.cpp`、`domain()`、`driversByDomain()` | 通用/飞控/汽车/机器人/卫导/视频——是**通用的业务分类学**，与格式无关 | ❌ **误命名** → `Domain`（通用名，不绑工具箱；见 `modules-organ-completion.md` §7） |
| F3 | **工具箱本身** | `FormatToolbox.h/.cpp`、`registerBuiltinFormats()` | 现在装的是**全部**知识包（通用/飞控/汽车/机器人/卫导），数据解析器只是其中一类贡献 | ❌ **误命名** → `Toolbox`（见 §9.6） |
| F4 | **录制格式** | `RecordingFormat.h/.cpp`、`registerFormat()` | raw/mcap 容器——是 app 机制选项 | ✅ **保留 `Format`**——需求方认为改动意义不大，维持 `RecordingFormat` |

**一句话区分**：F1 是"这个文件是什么格式、谁来处理它"；F2 是"这个能力属于哪个业务方向"；F3 是"所有能力的集合容器"；F4 是"录制成什么容器"。四个完全不同的维度，共用一个词，是命名混乱的根源。

### 9.2 两个误命名的证据

- **F2 `FormatDomain`**：头注释自称"工具箱的业务领域分类"；持有者是 `IToolPack::domain()` / `IToolContribution::domain()`，格式驱动只是被它分组的对象之一。`ContributionKind.h:12` 明确"领域（FormatDomain）是稳定的分类学"——**不是格式的属性**。
- **F3 `FormatToolbox`**：`FormatToolbox.cpp` 全文只调 `BuiltinPackRegistry::installAll(registry)`，装的是所有 `IToolPack`（含 panel/script/data/example/doc 各类贡献）。文件名却叫 FormatToolbox，且 `registerBuiltinFormats()` 已是 `registerBuiltinPacks()` 的**兼容别名**（`FormatToolbox.h:20`）——名字停留在"早期只有格式驱动"的阶段，没有跟上泛化。

### 9.3 F1 正名为 Parser：撞名约束与分层方案（需求方裁决）

需求方裁决："**F1 是 Parser，不是 Format**"。意图明确——这层能力的本质是"把某种数据解析进来"，叫 Format 是站在文件后缀的角度，叫 Parser 才是站在能力的角度。

**但有一个必须先解决的硬约束：`Parser` 这个名字已被占用。** 仓库里已存在一整套真正的 SPI 解析器：

| 已存在的 Parser（SPI 层，字节→数值批→Dataset） | 本层（F1，格式门面/声明层） |
|---|---|
| `CsvParser`、`UlogStreamParser`、`CustomProtocolParser`、`PspxParser`（`ext` 命名空间） | `CsvFormatDriver`、`UlogFormatDriver`、`CustomProtocolFormatDriver`（声明 `parserId()="csv"/"ulog"/"..."`，指向左列） |
| `ParserRegistry`、`ParserEntry`、`PsParserVtable`（core SPI） | `IFormatDriver`（`core` 命名空间） |
| 职责：**执行解析**（纯 C ABI 字节转换） | 职责：**声明**用哪个 parser + 表名策略 + 导入 schema/probe |

若把 `CsvFormatDriver` 直接改成 `CsvParser`，会与同名 SPI 实现 `CsvParser` **直接撞名**；`IParser` 又太短、语义不清（需求方 2026-11-19 明确否决）。

**最终裁决（覆盖此前所有 F1 命名方案，2026-11-19）：取消 `IDataParser`，F1 声明层并入 `IMessageParser`。**

需求方最终定调：`IMessageParser` 这个名字保留（自解释，Message 作广义"数据报文"解），`IDataParser` 没必要存在。结合 §10.3.1"一个解析器多输出函数、不绑定下游用途"，F1 与协议检查器统一为**同一个解析器接口**：

| 旧名 | 最终处置 | 说明 |
|---|---|---|
| `IFormatDriver` / 曾拟 `IDataParser` | **取消，并入 `IMessageParser`** | 声明能力（`parserId`/后缀/表名/schema/probe）成为 `IMessageParser` 的自述 + `paramSpecs` 的一部分 |
| `CsvFormatDriver` / `UlogFormatDriver` / `CustomProtocolFormatDriver` | **取消独立门面类**，职责并入对应解析器实现 | 由 `XxxParser` 实现 `IMessageParser`，通过 `outputs()` 暴露能力 |
| `IMessageParser` | **保留为唯一解析器接口** | 一个接口、多输出函数（`parseFrames`/`parseMessages`），名字自解释 |
| SPI `CsvParser` / `UlogStreamParser` / ... | **保持** `XxxParser` | 执行层不变，实现统一接口或经适配层 |
| `acceptsFormat()` / `driverForFormat()` / `params["format"]` | **保持字面量 `format`** | 意图参数/格式标识是文件后缀语义，不属命名变更 |

**理由**：

- 需求方明确：`IParser` 太短看不明白，`IDataParser` 没必要存在——**统一叫 `IMessageParser`**，它就是"解析器"的唯一名字。
- 结合 §10.3.1 的职责纯化：解析器只出数据、用途甩给调用者，数值批（原 F1 产物）与可读帧（原 message 产物）只是同一接口的两个输出函数，没有理由分成两个接口/两套门面类。
- 少一层间接：原 `IFormatDriver → SPI parser` 的声明门面被取消，注册表统一为单一 `ParserRegistry`（按 `outputs()` 能力查询）。
- **意图参数 `format` 与 `RecordingFormat`（F4）保持不动**——F4 保留 Format，`format="csv"` 是文件后缀语义。

> **一致性结果**：从此仓库里 `Parser` 家族只有 `IMessageParser`（统一 C++ 接口）+ SPI `XxxParser`（执行层）+ `ParserRegistry`（统一注册表）三件套，F1 不再有独立的 Format/Data 门面层。F2 `Domain`、F3 `Toolbox` 的改名不受影响。

### 9.4 F1 牵出的第五套 schema（并入 §4 统一）

统一后的 `IMessageParser`（原 F1 声明能力 + 原 F2 协议检查已合并，见 §9.3）的 `configSchema`/`importSchema` 是第 3、4 套 schema（见 §4 表格）——它们的 `type ∈ bool/int/double/enum/string/file` 与 `TransformParam`、`PropKind` 又不一样。**§4 的 `FormSchema`/`FieldType` 统一必须覆盖这四套**，其中：

- `FieldType` 已补 `File`（覆盖 `"file"`），`Number` 覆盖 `int/double`（§4.1 已更新）；
- `importSchema` 的 `ui:{preview,warnings}` 扩展段，在 `FormSchema` 里作为可选 `extras: QJsonObject` 保留，不丢能力；
- `IMessageParser::configSchema` 注释已声明"与 importSchema 同构"，正好一并收敛。

### 9.5 F3 工具箱正名

`FormatToolbox.h/.cpp` 全文只调 `BuiltinPackRegistry::installAll()`，装的是所有 `IToolPack`（含 panel/script/data/example/doc 各类贡献），"Format" 前缀停留在早期只有格式驱动的阶段。裁决：`FormatToolbox` → `Toolbox`（或 `BuiltinToolbox`）；`registerBuiltinFormats()` 已是 `registerBuiltinPacks()` 的兼容别名（`FormatToolbox.h:20`），保留一版 deprecated 别名后移除。

### 9.6 命名裁决汇总（并入 R0）

| 旧名 | 新名 | 类别 | 状态 |
|---|---|---|---|
| `DataSourceRepository` | `SourceRegistry` | 命名 | ✅ 已拍板 |
| `FormatDomain` | `Domain` | F2 | ✅ 已拍板（通用名，不绑工具箱） |
| `IFormatDriver`（F1） | **取消，并入 `IMessageParser`** | F1 | ✅ 已拍板（统一为单一解析器接口，见 §9.3） |
| `CsvFormatDriver` 等 3 个 | **取消门面类**，职责并入解析器实现 | F1 | ✅ 已拍板 |
| `IMessageParser` | **保留为唯一解析器接口** | — | ✅ 已拍板（名字自解释，多输出函数，见 §10.3.1） |
| SPI `CsvParser` / `ParserRegistry` 等 | **保持** | F1 执行层 | ✅ 不改名（避免撞名） |
| `FormatToolbox` | `Toolbox` | F3 | ✅ 已拍板 |
| `registerBuiltinFormats` | 直接用 `registerBuiltinPacks` | F3 | ✅ 保留一版 deprecated 别名 |
| `RecordingFormat` | **保持原名** | F4 | ✅ 已拍板（改动意义不大） |
| 意图参数 `format` / `acceptsFormat` / `driverForFormat` | 保持字面量 | 其他 | ✅ 不属本次改名（文件后缀语义） |

> 全部为机械替换，与 R0 的 `SourceRegistry` 同批处理。副作用：改名后仓库里只剩 F4 `RecordingFormat` 带 `Format` 字样，语义干净。

---

## 10. 参数配置统一：取消手写 schema，框架自动生成（需求方 2026-11-19 提案）

需求方提案原文："把 `IFormatDriver` 的内容直接放进 `IMessageParser` 里面；她不需要输出 schema；调用她的 C++ 根据名字、能力、param 列表自动生成 schema。"

逐条核对后的裁决：**核心诉求（取消手写 schema、框架按元信息自动生成）完全成立，采纳**；但提案里有两处硬约束，不修正就实现不了，下面讲透。

### 10.1 硬约束一：C++ 无反射，param 列表不会凭空出现

"根据名字、能力、param 列表自动生成 schema"里的 **param 列表，当前根本不存在于接口的元信息里**——它现在就藏在手写 schema 的 `fields[].key` 内部（`configSchema()`/`importSchema()` 返回的 QJsonObject）。

- `IToolContribution` 只有 5 个元数据：`id / displayName / kind / domain / provider`，**没有任何参数信息**。
- C++ 是编译期静态语言，没有运行时反射。调用方拿一个 `IMessageParser*`，**不可能"看一眼"就知道它有 `database_path` 这个参数、是 file 类型**。
- 所以若完全不声明参数，`f(名字, 能力, param列表)` 的输入里 `param列表` 为空，框架无从生成——除非调用方硬编码参数表（那比手写 schema 更糟：加参数要改对话框代码）。

**结论：param 列表必须显式声明，但这恰恰是好事**——把"声明"从"完整 UI schema"瘦身为"极简参数规格"，就是用户要的减负。

### 10.2 落地：`schema = f(名字, 能力, 参数规格)`，参数规格是唯一要手写的

把参数声明提为一等接口 `paramSpecs()`，返回 `QVector<ParamSpec>`：

| 输入 | 来源 | 是否手写 |
|---|---|---|
| 名字 | `displayName()` / `id()`（已有） | 已有 |
| 能力 | `kind()` / `domain()` / `parserId()` / `supportedTransports()`（已有） | 已有 |
| **参数规格** | **新增 `paramSpecs()` → `QVector<ParamSpec>`** | ✅ **唯一新增/手写** |
| schema / UI | 自研 `SchemaEngine` 解析参数规格 → 生成表单/对话框 | ❌ **自动生成，不手写** |

#### 10.2.1 schema 是纯 metadata，零 UI 控件词汇（需求方 2026-11-19 追加确认，✅ 已拍板）

需求方明确："后续 lib 库中的 schema 肯定不会指定 UI 控件，而是**数据类型、变量名、可选值，还有其他**——这些不是 UI，是 **metadata**；由**自己写的引擎解析**这个 metadata。"

这条比 §4"框架生成 UI"更彻底，确定为本设计的**最高契约**：`ParamSpec` 的每个字段都必须是**数据语义**，**严禁出现任何控件/布局词汇**（不得有 `widget`、`control`、`QCheckBox`、`row/column`、`visible_if`、`placeholder` 之类）。分界线：

| 字段 | 类别 | 语义 | 是否允许在 schema |
|---|---|---|---|
| `key` | 变量名 | 稳定参数名，回传/配置键（`database_path`） | ✅ metadata |
| `type` | 数据类型 | Bool/Int/Double/Enum/String/File……描述**值是什么**，不是控件是什么 | ✅ metadata |
| `defaultValue` | 数据 | 缺省值（类型化 `QVariant`） | ✅ metadata |
| `options` | 可选值 | Enum 的合法取值集合 `{value, label}`——label 是**枚举语义名**（如"自动探测"），不是 UI 提示文案 | ✅ metadata |
| `min/max` | 值域约束 | Int/Double 的合法范围 | ✅ metadata |
| `fileFilter` | 数据约束 | File 类型接受的后缀模式 | ✅ metadata |
| `unit` | 数据语义 | 物理单位（`ms`/`Hz`） | ✅ metadata |
| `dependsOn` | 数据依赖 | `{key, value}`——"当 X=某值时本项才有意义"，是**参数间的逻辑依赖**，不是 `visible_if` 控件显隐 | ✅ metadata |
| `description` | 数据文档 | 该参数的语义说明（引擎可作 tooltip，但 schema 里它是文档，不是 UI 指令） | ✅ metadata |
| ~~`widget/control`~~ | UI | 指定用哪个控件 | ❌ 严禁 |
| ~~`row/column/layout`~~ | UI | 布局位置 | ❌ 严禁 |
| ~~`placeholder/visible_if`~~ | UI | 呈现/显隐指令 | ❌ 严禁 |

> **判定法则**：字段若回答"这个参数**是什么数据**"（名字/类型/取值/约束/单位/依赖/说明）→ metadata，进 schema；若回答"这个数据**怎么画**"（控件/布局/样式/显隐）→ UI，归引擎。**类型决定控件，控件绝不写进 schema。**

```cpp
struct ParamSpec {              // 纯 metadata，headless 可构造/序列化/查询
    QString key;                // 变量名
    ParamType type;             // 数据类型枚举（Bool/Int/Double/Enum/String/File）
    QVariant defaultValue;      // 缺省数据
    double min = 0, max = 0;    // 值域（数据约束）
    QVector<EnumOption> options;// 可选值 {value, semanticLabel}
    QString fileFilter;         // File 后缀约束
    QString unit;               // 物理单位
    QVector<Dependency> dependsOn; // 参数间逻辑依赖 {key, equals}
    QString description;        // 语义文档（非 UI 指令）
};
// 严禁字段：widget/control/row/column/layout/placeholder/visible_if/style……
```

#### 10.2.2 自研 `SchemaEngine`：解析 metadata → 生成 UI

需求方明确"自己写的引擎解析这个"。引擎归属 `ng/libs/uischema`，是**唯一**把 metadata 翻译成 UI 的地方，分两层：

```
ParamSpec[] (纯 metadata, Qt Core, headless)
    │  SchemaEngine::compile()        ← 自研解析器：类型/可选值/约束/依赖 → 表单模型
    ▼
FormSchema (§4 契约, 仍是纯数据)
    │  QtFieldRenderer::render()      ← 唯一 ParamType → QWidget 映射
    ▼
QWidget / 对话框
```

- **compile 阶段**（headless、可单测）：读 `ParamSpec[]` + 名字/能力，套**类型→控件约定表**（`Bool→开关`、`Int/Double→带范围数值输入`、`Enum→单选`、`File→路径选择+filter`、`String→文本`）生成 `FormSchema`；处理 `dependsOn` 生成启用/禁用规则；label 由 `key` 约定或术语表补。
- **render 阶段**（Qt Widgets，仅 GUI 链接）：唯一一处类型→控件映射，产出真实控件。
- **扩展新控件类型**：改 `ParamType` 枚举 + 约定表（受控），**不改任何 parser**——这正是"schema 只描述数据、引擎负责呈现"的收益。
- **CLI/headless**：只跑 compile，输出 `FormSchema` JSON 或参数清单，不渲染——契约层零 QWidget。

与现有 `TransformParam { key,label,type,description,defaultValue }` 合并：`ParamSpec` 吸收它，`label` 归引擎（key 约定/术语表）、`type/defaultValue/description` 进 metadata——Transform 编辑对话框也接入同一引擎。

**净效果**：四个 driver/parser 手写的 `configSchema()`/`importSchema()` 全部删除，各改成 10 行内的 `paramSpecs()`（纯 metadata）；UI 呈现逻辑在 `SchemaEngine` 里**只有一份**。

### 10.3 硬约束二的历史推演（⚠️ 已被 §10.3.1 取代，保留仅供了解决策过程）

> **注意**：本节最初裁决"双接口 + 公共基类"，随后需求方进一步明确"解析器不绑定下游用途、允许一个解析器多输出函数"（§10.3.1），并最终定调"`IMessageParser` 保留、`IDataParser` 取消"（§9.3）。**最终方案见 §10.3.1 + §9.3，以它们为准**；本节表格/代码是当时的中间状态，不再是有效设计。

需求方最初说"把 `IFormatDriver` 的内容放进 `IMessageParser`"。**直觉有道理**（两者都要自述、参数配置、探测，确实重复），但当时我误以为直接合并成一个接口会出问题：

| 维度 | `IDataParser`（原 IFormatDriver，已取消） | `IMessageParser`（现为统一接口） |
|---|---|---|
| 产物 | Dataset 数值列 → 画图/分析 | DecodedFrame 可读帧 → 检查器 |
| 通路 | 声明 parserId，宿主喂字节给 SPI | 自己 `parse()` 字节 |
| kind | `format` | `message` |
| 注册表槽 | `registerDriver` / `drivers()` | `registerMessageParser` / `messageParsers()` |
| 消费者 | `DataLoadHandler` → `ImportDialog` | `ProtocolInspectorDialog` |

当时的错误前提："硬合并 = 一个接口背两个产物、两个注册表槽、两条通路，是'上帝接口'"——**这个前提在 §10.3.1 被推翻**：产物（数值批/可读帧）本就该是同一接口的不同输出函数，注册表/通路也应统一，不是"上帝接口"而是"单一职责的解析器"。

当时的中间方案（**已废弃**）：抽公共配置层、保留双接口：

```cpp
class IConfigurableContribution : public IToolContribution {  // 公共层（§10.3.1 仍保留这部分）
    virtual QVector<ParamSpec> paramSpecs() const = 0;
    virtual bool configure(const QJsonObject &params, QString *err) = 0;
    virtual QJsonObject probe(...) const = 0;
};
class IDataParser     : public IConfigurableContribution { /* 已取消 */ };
class IMessageParser  : public IConfigurableContribution { /* 现统一接口，见 §10.3.1 */ };
```

当时提到的"公共配置层 `IConfigurableContribution`"这一条**在最终方案中保留**；"双接口"这一条**废弃**。

- 一套参数声明（`paramSpecs`）、一套 schema 生成、一套配置注入——`IConfigurableContribution` 统一；
- 两个对话框（`ImportDialog` / `ProtocolInspectorDialog`）合并为一个**通用参数对话框**，调同一个 `FormSchemaGenerator`；
- 产物/通路/kind/注册表槽保持分离，各接口只保留自己的核心行为。

> 一句话：合并的是"配置能力"（该合），不是"解析接口"（不该合）。用户看到的重叠 90% 在配置层，抽取公共基类后这 90% 就真的只剩一份了。

#### 10.3.1 最终裁决：解析器不绑定下游 + 一个 `IMessageParser` 多输出函数（需求方 2026-11-19 定稿，✅ 已拍板）

需求方质疑："`IDataParser` 为什么要给他附加绘图的能力呀，甩给其他人。可以允许 `IMessageParser` 增加几个函数，不同函数接收不同的参数，输出不同的格式。" 最终定调："`IMessageParser` 叫这个名字，`IParser` 太短看不明白，`IDataParser` 没必要存在。"

**质疑成立，且 SPI 早已如此设计——是我的表述错了。** `ps_parser_spi.h` 头部原文：

> 解析器 = 字节 → 结构化批的纯转换。不读文件、不写存储、不认识宿主、不管生命周期。**结果归宿由调用者决定，解析器不绑定 Rust。** 调用者喂字节、收批、决定批去哪（Rust typed 表 / 其它存储 / 转发 / 丢弃）。

- 绘图、Dataset、Rust、存储**从来都不是解析器的事**，是调用者（`IngestOrchestrator` → `plot`）的事。
- 我上一版把"→ Dataset 画图"写进 `IDataParser` 的产物描述，只是陈述当前实现流向，**不该上升为接口职责**——按需求方要求改回"解析器只出数据、用途甩给调用者"。
- 佐证：`PsParserSink` / `PsMsgSink` 都是**回调出口**——解析器解出数据往 Sink 里写，自己不持有、不决定去向。

**裁决：解析器 = 纯转换 + 多输出函数，输出经 Sink 交还调用者。**

1. **职责纯化**：解析器接口只回答"这段字节里有什么"，输出纯 metadata 数据结构（数值批 / 可读帧 / ……），**不碰 Dataset / Rust / 文件 / UI**。归宿全由调用者决定——"甩给其他人"在 SPI 层已落实，C++ 层对齐。

2. **一个解析器，多个输出函数**：不同函数接收不同参数、输出不同格式，由 `outputs()` 能力声明支持哪些，调用方按需选择：

```cpp
enum class ParseOutput { FrameBatch, MsgFrame /* , Survey, ... */ }; // 纯数据输出类型
class IMessageParser : public IConfigurableContribution {   // 唯一解析器接口（名字自解释，Message 作广义"数据报文"）
    virtual QVector<ParseOutput> outputs() const = 0;       // 能力声明：本解析器能提供哪些输出
    // 输出函数签名各异，调用方按 outputs() 挑选：
    virtual int parseFrames(const QByteArray &, FrameBatchSink, ParseErr *) const;  // → 数值批（调用者拿去入仓/画图）
    virtual int parseMessages(const QByteArray &, MsgFrameSink, ParseErr *) const;  // → 可读帧（调用者拿去检查）
};
```

> 命名说明：`IMessageParser` 虽然带 Message，但此处 Message 取广义"数据报文"义——一个协议报文既能解成数值批也能解成可读帧，名字仍自解释。否决 `IParser`（太短、看不明白）与 `IDataParser`（与前者重复、无存在必要）。

3. **对 10.3 的修正**：`IConfigurableContribution` 公共配置层（自述 + `paramSpecs` + `configure` + `probe`）**保留**；变的是产物组织方式——从"两个接口各一个产物方法"改为"**一个 `IMessageParser` 挂多个输出函数**"。原 `IFormatDriver`/`IDataParser` 门面的 `parserId`/表名/schema 声明并入 `IMessageParser` 的自述与 `paramSpecs`，**取消独立门面层与 `IDataParser` 类型**——与"归并两条数据通路"同向，少一层间接（见 §9.3 最终裁决）。

4. **注册表统一**：单一 `ParserRegistry`，按 `outputs()` 能力查询；原 `drivers()` / `messageParsers()` 退化为能力视图，`kind = format/message` 保留作兼容过滤。

5. **SPI 对齐（另批）**：`ps_parser_spi.h` 与 `ps_message_parser_spi.h` 两个 vtable 可按同思路演进为"一个 vtable + 多输出槽"（按 `struct_size` 判定）——但这是 **ABI 变更**，本批先统一 C++ 层，SPI 另立批次。

**验收补充**：① 解析器接口无 Dataset/Rust/文件/UI 依赖，可 headless 编译；② 一个协议类（如 MAVLink）能同时提供数值批与可读帧两种输出，调用方按需取用；③ 所有输出走 Sink 回调，解析器不持有结果、不决定去向；④ 仓库中不再有 `IDataParser` / `IFormatDriver` / 任何 `XxxFormatDriver` 门面类型。

> **已拍板（2026-11-19）**：取消 `IDataParser`、F1 声明层并入 `IMessageParser`（§9.3），统一接口保留 `IMessageParser` 之名 + 多输出函数（本节）。§10.3 的"双接口"中间方案作废。

### 10.4 与 §4 的关系

§4 定的 `FormSchema`/`FieldSpec`/`FieldType` **仍是唯一产物契约**，不变；变的是**生产方**：

- §4 原假设："各实现产出 `FormSchema`" → 实现方手写完整 schema；
- §10 修正为："实现只写 `ParamSpec[]`（**纯 metadata、零 UI 词汇**），自研 `SchemaEngine` 编译出 `FormSchema`" —— 实现方连 schema JSON 都不写了。

`uischema` 库职责相应扩展为：`ParamSpec(metadata) → SchemaEngine.compile → FormSchema → QtFieldRenderer → QWidget` 全链路，唯一一处 `ParamType → 控件` 映射（在引擎约定表，不在各 parser）。

### 10.5 验收标准（并入 §6）

1. 4 个 driver/parser + Transform 描述子中，**不再有任何 `QJsonObject` 手写的 schema**（`configSchema`/`importSchema` 删除）；
2. 每个组件只提供 `paramSpecs()`，项数/键与旧 schema 的 `fields` 一一对应（不丢参数）；
3. `ImportDialog` 与 `ProtocolInspectorDialog` 合并为通用对话框，二者不再各自 switch 控件类型；
4. 新增一个参数：只改对应组件 `paramSpecs()` 一行，对话框与 schema 生成代码零改动；
5. headless 可查询"某解析器/驱动有哪些参数"（契约层无 QWidget）；
6. §4 的 `FormSchema` JSON 往返、类型化值、类型穷举三条约束仍全部成立。

### 10.6 拍板状态

1. ~~确认 `paramSpecs()` 作为唯一手写项~~ → **✅ 已确认**（2026-11-19，需求方追加说明 schema 是 metadata）。
2. ~~确认"双接口 + 公共基类"~~ → **✅ 最终拍板（2026-11-19）**：取消 `IDataParser`、F1 声明层并入 `IMessageParser`，统一接口保留 `IMessageParser` 之名 + 多输出函数（§9.3 + §10.3.1）；解析器不绑定下游用途、输出经 Sink 交调用者。§10.3 的双接口方案作废。
3. ~~确认字段 label 由 `key` 约定生成 + 预览区框架统一渲染~~ → **✅ 已确认**（§10.2.1 定为"schema 纯 metadata、零 UI 控件词汇"，label/description 归引擎，预览归 probe 统一渲染）。
4. 确认两个参数对话框合并为一个通用对话框（涉及 ImportDialog 的探测预览交互，需保证体验不退化）？——**待拍板**。
5. **✅ 已拍板（2026-11-19）**：schema 只描述 metadata（变量名/数据类型/可选值/值域/单位/依赖/说明），**严禁 UI 控件词汇**；由自研 `SchemaEngine` 解析生成 UI（§10.2.1/§10.2.2）。

---

## 11. 实施记录

### 11.1 B3：`ng_registry` 最小核（2026-11-20，✅ 已实施）

按总方案 B3 落地 §2「注册机制抽库」的第一步——**先做最小核 + 一套薄壳验证**，其余四套注册表待后续批次迁入：

- **新建 `libs/registry`（INTERFACE/header-only）**：
  - `Registry.h`：泛型 `Registry<T, Key=QString>`，提供 `insert`（重复 id 先到者为准）/ `remove` / `find` / `contains` / `keys` / `count` / `clear`。只依赖 Qt Core + STL，无 QWidget / Rust / core 业务类型，headless 可用。
  - `StaticAutoRegistry.h`：`StaticRegistry<T>`（C++17 `static inline` 变量—— libs 铁律允许的「静态对象」例外，非 Meyers 单例）+ `AutoRegistrar` + `NG_AUTO_REGISTER` 宏，统一启动期自登记。
- **薄壳验证：`PanelFactoryRegistry`**（`core/src/PanelFactoryRegistry.cpp`）内部容器由手搓 `QHash` 换为 `registry::Registry<Factory>`，**对外三个静态 API 与 `NG_REGISTER_PANEL` 宏零改动**，调用方无感。兑现 §2「接口等价重构、调用方零改动」。
- **独立链接验收**：新增 `ng_test_registry_standalone`，只链 `ng_registry + Qt6::Core`（不链 ng_core），走通 CRUD + 自注册 + std::function 工厂型条目；编译期即证明无上层反向依赖。
- **namespace**：`registry` 库自身用 `nextpilot::plotscope::registry`；被薄壳化的 `PanelFactoryRegistry` 随 core 保持原 namespace 至 B7 统一收口（与 B2 裁定一致）。
- **构建守卫修复**：configure 期守卫原用 `file(STRINGS)` 逐行剥离注释，遇含 UTF-8 中文的注释行会把字节流截断、误把注释里的字面量当代码（本次踩坑：注释中解释「为什么不用单例」被误报）。改为 `file(READ)` 全文 + `[^\n\r]` 注释正则，注释正确剥离、真违规仍能 FATAL。
- **验收**：Release + Debug ctest 全绿（新增 1 项）。

### 11.2 B4：`ng_intent` 意向契约库（2026-11-20，✅ 已实施）

按总方案 B4 落地 §3「意向机制抽库」：

- **新建 `libs/intent`（STATIC）**：把 `Intent.h` / `IIntentExecutor.h` / `JobRecorder.h` / `Intent.cpp` 四个文件从 core `git mv` 迁入（100% rename）。含 5 个契约类型（Intent / IntentResult / IIntentHandler / Job / IIntentExecutor）+ JobRecorder，全为纯契约与纯数据。PUBLIC 链 `ng_foundation + Qt6::Core`，无 QWidget / Rust / core 业务类型。
- **core 改造**：移除四个源文件条目，PUBLIC 链 `ng_intent`；31 个引用文件走 include 路径传递，零改动。
- **独立链接验收**：新增 `ng_test_intent_standalone`，只链 `ng_intent + ng_foundation + Qt6::Core`（link.txt 实证无 ng_core），走通 Job 双向序列化 + 二次序列化一致性 + 缺 op 报错 + JobRecorder 录制/清空 + 独立实现 IIntentExecutor；并顺带把迁入文件的长注释按写作纪律精简。
- **namespace**：保持 `core` 至 B7 统一收口（与 B2/B3 裁定一致，避免与 Result 跨 namespace 割裂）。
- **验收**：Release + Debug ctest 14/14 双绿；构建守卫通过。

### 11.3 B5：`ng_uischema` 契约 + 引擎 + 唯一渲染器（2026-11-20，✅ 已实施首批）

按总方案 B5 落地 §4/§10 的 schema 契约，采取垂直切——先建完整库 + 一个消费者接入验证，其余消费者待 B6 `paramSpecs()` 接口：

- **新建 `libs/uischema` 两个 target**：
  - `ng_uischema`（STATIC，Qt Core + foundation，headless）：`ParamSpec.h`（纯 metadata：key/type/defaultValue/min/max/options/fileFilter/unit/dependsOn/description，零 UI 控件词汇）、`SchemaEngine.h/.cpp`（`compile()` 唯一持有 `ParamType→FieldType` 约定 + label 术语表覆盖 + 依赖判定）、`FormSchema.h/.cpp`（产物契约，类型化 QVariant + JSON 往返）。
  - `ng_uischema_widgets`（STATIC，Qt Widgets + ng_uischema，只 GUI 链接）：`QtFieldRenderer.h/.cpp`——**全仓唯一一处 `FieldType→QWidget` 映射**，统一 valueOf/setValue/keyOf 读写。
- **消费者接入：`TransformEditorDialog`**：core 的 `Transform.cpp` 加 `transformParamSpecs()` 把 `TransformParam` 魔法字符串适配为 metadata（core PUBLIC 链 ng_uischema）；对话框参数区从手绘 bool/else switch 改用 `SchemaEngine.compile() + QtFieldRenderer.render()`，回填/读取改用渲染器统一 API，中文 label 经术语表覆盖不倒退。
- **独立验收**：`ng_test_uischema_standalone` 只链 ng_uischema + foundation + Qt Core（link.txt 实证无 Widgets/ng_core，headless 成立），覆盖各类型映射、label 生成、类型化默认值、依赖判定、JSON 往返、零 UI 词汇断言；`ng_test_transform_adapter` 覆盖 TransformParam→ParamSpec→FormSchema 适配链路（headless，链 core 不拉 GUI）。
- **范围说明**：ImportDialog / ProtocolInspectorDialog 的 schema 来自 `IFormatDriver::importSchema` / `IMessageParser::configSchema`，须待 B6 合并解析器并加 `paramSpecs()` 后才能接同一引擎，故本批不碰。
- **验收**：Release + Debug ctest 16/16 双绿；构建守卫通过。

### 11.4 B6：解析器统一 + 命名收敛 + paramSpecs 收口（2026-11-20，✅ 已实施）

按总方案 B6 落地 §9.3 / §10.3.1 的解析器合并裁决，分三个子批（各自独立提交、双配置验收）：

- **B6-b 命名收敛**：`DataSourceRepository` → `SourceRegistry`、`FormatDomain` → `Domain`、`FormatToolbox` → `Toolbox`，6 文件 `git mv` + 全仓 51 处符号/include 替换，零旧名残留。`domainKey/domainName` 函数名保留，namespace 仍 `core` 待 B7 收口。
- **B6-a 接口统一**：删 `IFormatDriver.h/.cpp` 与三个 `XxxFormatDriver` 门面类；`IMessageParser` 成唯一接口，并入 `parserId`/`suffixes`/`acceptsFormat`/`preferredTableName`/`probeFile`/`defaultImportOptions`/`prepareParserOptions` 声明，加 `ParseOutput` 能力枚举 + `outputs()` + 多输出函数（`parseMessages` 委托 `parse` 产可读帧，`parseFrames` 明确归 SPI 另批）。三个门面类改名 `XxxFormatParser` 并改继承 `IMessageParser`，删空壳 `load()`。`ExtensionRegistry` 加统一 `parsers()` 视图，`drivers()/driverForFormat()/driverForPath()/messageParsers()` 保留兼容别名（内部按 `outputs()` 含 `FrameBatch` 筛数值批解析器）。SPI 层（`ps_parser_spi.h`/`ParserRegistry`）零改动，`data.load` 与既有测试零改动。
- **B6-c paramSpecs 收口**：`IMessageParser::paramSpecs()` 默认从 `configSchema()` 的 fields 转纯 metadata（headless 可查），`configSchema()` 保留 virtual。`ProtocolInspectorDialog` 参数区改走 `paramSpecs() → SchemaEngine.compile → QtFieldRenderer.render`，删除手绘控件 switch，中文 label 经术语表覆盖。**ImportDialog 未接入**：CSV 动态列枚举来自 probe 列下标，uischema enum 用字符串 label 作值且无 probe 注入通道，强接会退化探测预览；保留自渲染，作为独立小批记录（见总方案 §5.2）。
- **验收**：B6-a/b/c 各批 Release + Debug ctest 16/16 双绿；门面类型全清零；构建守卫通过；rebase 合并远端 protocol(S4a-1) 后复验双配置仍全绿。

### 11.5 B8：Transform 输入控件重构（2026-11-20，✅ 已实施）

按总方案 §7.7.2 落地，分两子批（各自独立提交、双配置验收）：

- **B8-a `LuaScriptLibrary`（script 模块，有状态不进 libs）**：扫描 `scripts/ops/*.lua`，裸 `lua_State` 只开 base/table/string/math，指令计数钩子防死循环。`describe()` 导出 table 经 Lua→QJson 反向转换（区分数组/对象）填 `TransformDescriptor`（id=`lua.<name>`）；无 `describe()` 老脚本降级为文件名 label + 能力全开、不报错。`save` 写文件并注册 descriptor，`resolveSource` 文件为真源、快照兜底，脚本名安全校验（小写/数字/_-）。headless 测试覆盖元数据解析/降级/语法错/死循环截断/沙箱隔离（io.open 失败）/保存注册/loadAll/非法名。
- **B8-b GUI**：`TransformField`（可编辑下拉：选算子带说明 / 直接敲表达式 + uischema 参数区 + 右侧编辑按钮）；`LuaEditor`（代码编辑 + 单点/批量调试复用旧逻辑 + 保存到 `scripts/ops` 并注册，回传 `scriptId` + `luaSource` 快照）。`TransformEditorDialog` 退为承载 `TransformField` 的薄壳，4 个调用点 `run()` API 零改动。脚本能力经 `TransformScriptOps` 回调注入 `AppContext`，gui 不依赖 script 模块；`ScriptModule` 持有 `LuaScriptLibrary` 实例并注入（组合根装配、构造注入）。顺带修 B5 遗留缺陷：`transformParamSpecs` 的 number 参数用 QDoubleSpinBox 默认下限 0 截断负值（取反算子无法输入 -1），补宽松范围 ±1e12。
- **验收**：新增 `ng_lua_script_library`（headless）、`ng_transform_field`（offscreen GUI）两个测试；Release + Debug ctest 18/18 双绿；libs 边界守卫通过。
