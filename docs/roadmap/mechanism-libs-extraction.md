# 机制抽库设计：recording 归属 + 注册表/意向/schema→UI 下沉为 libs

> 状态：**架构设计，待需求方拍板**（2026-11-19）。本轮只出边界与契约，**不写实现代码**。
> 需求方三问：① `ng/recording` 为何独立、有何特权？② core 的注册机制（模板、支持多种类）与意向机制抽成 libs；③ 按 schema 让 Qt 绘制对话框/属性面板的能力抽成 libs，重点是**约束 schema 长什么样**。
> 与 [`dataset-lib-extraction.md`](dataset-lib-extraction.md) 同属 core 瘦身动作；所有结论逐行核对过 `ng/core`、`ng/recording`、`ng/gui`。

---

## 0. 一句话结论

1. **recording 没有特权**：它独立是因为"格式 × 后端"是与 core 正交的关注点，独立正确；真正的瑕疵是**接口在 core、实现在 recording 的分家**，应把接口也归位到机制库。
2. **注册表与意向机制可抽**：注册表做成**一个泛型模板** `Registry<T>` 支持多种条目类型，各具体注册表退化为薄配置；意向机制的 `Intent/Job/IIntentHandler/IIntentExecutor` 全是纯契约，天然可独立成库。
3. **schema→UI 抽库是最大收益点**：现在 `PropertySchema`（6 种控件）与 `TransformParam`（3 种）**各画各的 UI**，抽库必须先**统一 schema 契约**——这是本文重点（§4）。

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

需求方点明"**主要是约束 schema 该什么样**"——这是关键。现在有**两套各画各的 schema**：

| 来源 | 文件 | 字段/控件类型 |
|---|---|---|
| `PropertySchema` | `core/include/PropertySchema.h` | `PropKind { Text, Number, Bool, Enum, Color, ReadOnly }`，6 种；`PropRow { key,label,value,kind,editable,unit,options,tooltip }` |
| `TransformParam` | `core/include/Transform.h` | `type: "number"/"string"/"bool"`（3 种，字符串魔法值）；`TransformParam { key,label,type,description,defaultValue }` |

绘制端也分裂：

- `PropertyPanel.cpp` 用 **QTreeWidget 手绘**（`setData(1, kRoleKind, ...)`，靠 role 存 kind，无统一 widget 工厂）；
- `TransformEditorDialog.cpp` 用 **QFormLayout + 手写 switch**（`if type=="bool"` → QCheckBox，else QLineEdit）——只支持 2/3 种，且魔法字符串判断。

**这就是要抽库的根本理由**：不先统一 schema，抽库只会把两套绘制逻辑搬进同一个库，仍是两份。

### 4.1 统一 schema 契约（本设计的核心产出）

抽 `ng/libs/uischema`，定义**唯一**的字段描述结构，取代上述两者：

```cpp
enum class FieldType {          // 控件类型：穷举，不开放魔法字符串
    Text, Number, Bool, Enum, Color, ReadOnly
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
