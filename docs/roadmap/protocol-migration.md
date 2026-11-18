# 自定义协议（.psproto）搬迁到 ng

| 项 | 值 |
|---|---|
| 状态 | 方案待开工（2026-11-18） |
| 老位置 | `src/application/modules/protocol/`（只读参考，一行不动） |
| 目标 | 把"变长变体"协议的文档模型、解码、入仓、编辑器接到 ng 现有通路（SPI 解析器 + IFormatDriver + Pack + 模块/视图） |
| 硬约束 | `src/` 不改；headless 部分不依赖 Qt Widgets / Dataset；解析器只喂字节、收批，不写存储 |

## 1. 老资产盘点（5429 行）

| 老文件 | 行数 | 性质 | ng 去向 |
|---|---|---|---|
| `protocol/ProtocolSpec.{h,cpp}` | 341 | 结构校验 + 类型表（U8/I8/U16/I16/U32/I32/U64/I64/F32/F64/BOOL/BCD/STR）+ 表达式/运算符宽度 | L1 headless 库 |
| `protocol/ProtocolDocument.{h,cpp}` | 1006 | 文档模型：Expr AST、TypeInfo、CheckSpec、CountSpec、SegmentDef、VariantDef、SeriesDef；JSON 解析/回写 | L1 headless 库 |
| `codec/SequentialCodec.{hpp,cpp}` | 1089 | 顺序帧解码：模板 JSON + 原始字节 → 帧；选择器命中变体、变长段、CRC/条件校验、scale/offset、大小端 | L2 SPI 解码器 |
| `manager/CustomProtocolLoader.cpp` | 912 | 文件 loader：数据文件 + `.psproto` 模板两文件配对，调 codec 入仓（老 ABI） | L3 格式驱动 |
| `dialog/ProtocolEditorDialog.{h,cpp}` | 1857 | QDialog 编辑器：字段表/段表/变体行/单帧试算/JSON 重建 | L4 模块 + 视图 |

输入格式：原始帧 `*.bin *.raw *.frames`；模板 `*.psproto *.json`；仅文件传输。

## 2. 老帧模型两层结构（解码逻辑的事实基础）

**第一层 头部段**（`document.segments`）：先顺序解析，产出
- 选择器：`series.selector` 引用某头部字段（字符串或整数），用于命中变体；
- 计数/大小引用：变长段的 `sizeExpr` / `counts` 引用前面已解析的字段。

**第二层 变体分支**（`series.variants`）：选择器命中 `VariantDef` 后，从 payload 起按该变体自己的 `segments` 解析；变体段可定长标量，也可由 `sizeExpr`/`counts` 变长。

其余机制：段内 `checks`（eq/min/max/crc8/16/32）、`scale/offset`、`bigEndian`、`fallback`（reject/raw）、STR/BCD 处理。

## 3. 本次范围（已拍板）

| 决策点 | 结论 |
|---|---|
| 头部段 | **全搬**：头部段照常解析并作为普通通道输出，变体分支在其后展开（完整两层） |
| 变体段字段 | **定长标量 + 变长数组都支持**，一套解析器处理；只砍掉"无 series、整条帧一套固定布局"的纯定长整帧通路 |
| 段内校验 | **一起搬**：eq/min/max + CRC8/16/32（含 poly/init/xorout/refin/refout/width）随解码平移 |

不搬：编辑器里的 Lua 脚本重建、变体字段的可视化拖拽高级交互（第二批，见 §7）。

## 4. ng 分层方案

### L1 headless 协议模型 —— 新静态库 `ng/libs/ps-protocol`

- 平移 `ProtocolSpec` + `ProtocolDocument`：Expr AST、类型表、各 Spec 结构、JSON 解析/序列化、`validate(QJsonObject)`。
- 纯 Qt Core；不认识 Dataset、不认识 Widgets、不认识老 host ABI。
- 独立成库的理由：L2 解码器和 L4 编辑器视图都要链它；extensions 之间不应互相 link（与 fastplot 同思路）。
- 目录：`ng/libs/ps-protocol/{include/ps_protocol/*.h, src/*.cpp, CMakeLists.txt}`。

### L2 SPI 解码器 —— `ng/extensions/common/protocol/`

- 平移 `SequentialCodec` 解码算法；**输出端改造**：从老 `PsHostApi*` 建 dataset 改为 `PsParserSink::frame_batch`（与 `PspxParser`/`UlogStreamParser` 同一条路）。
- 解析器不读文件、不写存储、不认识宿主；模板 JSON 作为解析器配置传入（见 §5 决策）。
- 可独立单测：构造模板 + 字节帧，断言吐出的批。

### L3 格式驱动 —— Pack 登记

- `ProtocolFormatDriver : IFormatDriver`，后缀 `.bin/.raw/.frames`；`parserId` 指向 L2 SPI 解析器；`paramSchema()` 声明协议模板 File 字段。
- 登记进一个新 Pack（`pack.protocol`，domain General），走 `NG_REGISTER_PACK` 自注册，不改宿主装配。

### L4 模块 + 视图

- `ng/modules/protocol/ProtocolModule`：意图 `protocol.new / protocol.open / protocol.validate`，ribbon 动作；headless 校验走意图，对话框经视图动作表/面板工厂（模块不持有 QWidget）。
- `ng/gui/views/protocol/`：编辑器视图。1857 行超行数预算，拆成：头部字段表、变体段表、变体行、单帧试算预览几个小组件。

## 5. 载入入口：按格式 schema + probe 驱动的通用对话框

### 5.1 老实现事实（参考 `src/application/modules/datasource/widget/dialog/DataSourceDialog.cpp`，1407 行）

老的打开对话框**不是每个格式各写一个对话框**，而是一个通用对话框：每个 loader 贡献自己的 schema（字段定义）和一个 probe 回调，对话框按 schema 建控件、按 probe 结果回填默认值并显示预览。CSV/MAVLink/自定义协议都走这同一个对话框。已确认的能力：

- **控件类型**：`bool` 复选框、`int` 微调框（min/max/suffix）、`double` 双精度微调框（decimals）、`enum` 下拉（options: value/label）、`file` 路径行（浏览按钮；协议模板字段额外带"编辑…"按钮）、`string` 单行输入（placeholder）。
- **条件显隐**：`visible_if {key, equals}`，依赖字段变化时刷新目标字段显隐。
- **probe 探测**：选文件或改参数后，调 loader 的 `probe(path, opts)`，返回列定义、预览行、建议默认值、告警；`initial_from_probe` 把探测值填进字段；下拉选项可来自 probe 结果（如列名）。
- **预览区**：按 schema 的 `ui.preview` 配置渲染预览表（列头 + 前若干行），顶部汇总"N 列 · M 行"；告警条按 `ui.warnings` 规则对比探测值与字段值给出提示。
- **传输段**：live 源（udp/串口等）有独立 transport 字段段；本批只做文件源，transport 留后续。

### 5.2 驱动声明：schema + probe（headless，纯数据）

`IFormatDriver` 增加可选能力，默认空 = 无参数、无探测：

```cpp
struct ParamField {
    enum class Type { String, Int, Double, Bool, Enum, File };
    QString key;
    Type type = Type::String;
    QString label;
    QVariant defaultValue;
    int min = 0, max = 0;                 // Int
    double dMin = 0, dMax = 0; int decimals = 3; // Double
    QString suffix, placeholder;
    QList<QPair<QString, QString>> options;       // Enum：value/label
    QString fileFilter;                           // File
    bool canEdit = false;                         // File：是否带"编辑…"动作（协议模板）
    struct VisibleIf { QString key; QString equals; };
    VisibleIf visibleIf;                          // 空 = 始终可见
};

struct ProbeResult {
    bool ok = false;
    QString error;
    QVariantMap defaults;                          // initial_from_probe 回填
    QList<QPair<QString, QString>> enumOptions;    // 某下拉的 probe 来源列（key 在外层映射）
    QList<QString> columns;
    QList<QStringList> rows;
    QStringList warnings;
};

virtual QList<ParamField> paramSchema() const { return {}; }
virtual ProbeResult probe(const QString &path, const QVariantMap &params) const; // 默认不支持
```

- schema/probe 都是纯值结构，不含 QWidget；驱动可 headless 单测。
- 老 schema 类型对照：`string→String`、`int→Int`、`double→Double`、`bool→Bool`、`enum→Enum`、`file→File`；`visible_if→visibleIf`。
- probe 只读文件头，不写存储；结果归宿由对话框/管道决定。

### 5.3 通用对话框 `LoadDataDialog`

新增 `ng/gui/dialogs/LoadDataDialog`（模态 QDialog，外壳交互能力，与 `pickFilePath` 同性质；不进模块、不进 core）：

1. **格式下拉**：从 `ExtensionRegistry` 枚举 `kind=format` 驱动，动态读 `displayName / id / suffixes`；不写死枚举。
2. **数据文件行**：路径输入 + 浏览按钮，过滤器随选中格式 suffixes 切换。
3. **参数表单区**：按驱动 `paramSchema()` 动态生成控件（类型见 5.2）；`visibleIf` 条件显隐；File 字段按 `canEdit` 决定是否显示"编辑…"（协议模板打开协议编辑器）。
4. **probe 联动**：文件路径或参数变化 → 调 `driver.probe(path, params)` → 回填 `defaults`、刷新 probe 来源下拉、更新显隐、填预览表与告警条；probe 失败显示错误条、隐藏预览。
5. **确定**：收集 `params["path"]`、`params["format"]` 与各字段值，组装 `Intent{op:"data.load"}` 交 MainWindow `host.execute`。

对话框不认识具体格式；新增格式只要声明 schema（和可选 probe），对话框自动长出控件与预览。

### 5.4 管道透传

`IngestOptions` 增加通用 `QVariantMap params`；`DataLoadHandler` 把意图全部参数透传进 Options，再交驱动/解析器。不为每种参数加专门字段。CLI/脚本直接带 `params`，不弹对话框，与 GUI 同一条 `data.load` 通路。

### 5.5 分批与边界

- **P4a**：schema 控件生成 + `visibleIf` + 收集参数发意图（协议模板 File + 编辑动作随本批）。
- **P4b**：probe 联动、预览表、告警条、probe 来源下拉（CSV 级体验）。
- **后续**：live 源 transport 段、多 parser 页签切换、录制旁路勾选。
- 对话框只收集参数与发起意图；匹配/解码/入仓仍走驱动注册表 + 统一摄取管道，不开第二条通路。
- 现有 ng 驱动（CSV/ULog/…）补 schema 是独立后续批次；对话框与控件类型先做全。
- `GuiContext::pickFilePath` 保留给模块内零星选文件；主载入入口换成 `LoadDataDialog`。

## 6. 分批与验收

| 批次 | 内容 | 验收 |
|---|---|---|
| P1 | L1 `ps-protocol` 库：Spec + Document 平移，JSON 解析/校验/序列化 | 单测：样例协议 JSON 校验通过/报错、类型表字节宽度、Expr 解析与求值 |
| P2 | L2 SPI 解码器：两层帧解析、变体命中、变长段、CRC/条件校验、scale/offset、大小端 | 单测：构造含变体的模板 + 字节帧，断言各通道批值；校验失败帧被拒 |
| P3 | L3 驱动 + Pack 登记；驱动声明 `paramSchema`（协议模板 File 字段）；`IngestOptions` 补通用 `params` 透传；打通"数据文件 + 模板 → 入仓" | 集成测试：经 IngestOrchestrator 载入自定义协议文件，数据集通道齐全；模板参数到达解析器 |
| P4a | 通用 `LoadDataDialog`：格式下拉 + schema 控件（String/Int/Double/Bool/Enum/File）+ 条件显隐 + 协议模板编辑动作；L4 模块意图 + 协议编辑器视图（打开/展示/校验/保存 JSON/单帧试算） | GUI 测试：对话框按 schema 产出 `data.load` 意图闭环；面板自注册；CLI 构建保持纯净 |
| P4b | probe 联动：文件头探测回填默认值、probe 来源下拉、预览表、告警条 | 测试：CSV 样例探测出分隔符/表头/列预览；探测错误有提示 |

## 7. 第二批（本轮不做）

- 编辑器变体字段可视化增删、Lua 脚本重建、字段拖拽排序。
- 流传输（串口/网络）下的协议解码；本轮仅文件。
- 协议模板的样例库与向导新建。

## 8. 红线

- `src/` 只读参考，不改一行。
- headless 库不链 Qt Widgets；core 不加新概念。
- 解析器零拷贝纪律：喂进来的字节 buffer 不由解析器持有；产出批走 sink。
- 模块之间不 include；视图 TU 内自注册面板类型。
