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

- `ProtocolFormatDriver : IFormatDriver`，后缀 `.bin/.raw/.frames`；`parserId` 指向 L2 SPI 解析器。
- 登记进一个新 Pack（`pack.protocol`，domain General），走 `NG_REGISTER_PACK` 自注册，不改宿主装配。

### L4 模块 + 视图

- `ng/modules/protocol/ProtocolModule`：意图 `protocol.new / protocol.open / protocol.validate`，ribbon 动作；headless 校验走意图，对话框经视图动作表/面板工厂（模块不持有 QWidget）。
- `ng/gui/views/protocol/`：编辑器视图。1857 行超行数预算，拆成：头部字段表、变体段表、变体行、单帧试算预览几个小组件。

## 5. 待落地的一个管道缺口

`IngestOptions` 当前只有 `tableName / useCache / stableId`，**没有 sidecar 模板路径**。自定义协议需要"数据文件 + .psproto 模板"两文件配对。

倾向方案：给 `IngestOptions` 加一个通用 `QVariantMap params`（模板路径放 `params["protocolTemplate"]`），驱动与 openFile 意图透传给解析器；不为协议特例加专门字段。该改动在 P3 开工前确认。

## 6. 分批与验收

| 批次 | 内容 | 验收 |
|---|---|---|
| P1 | L1 `ps-protocol` 库：Spec + Document 平移，JSON 解析/校验/序列化 | 单测：样例协议 JSON 校验通过/报错、类型表字节宽度、Expr 解析与求值 |
| P2 | L2 SPI 解码器：两层帧解析、变体命中、变长段、CRC/条件校验、scale/offset、大小端 | 单测：构造含变体的模板 + 字节帧，断言各通道批值；校验失败帧被拒 |
| P3 | L3 驱动 + Pack 登记；打通"数据文件 + 模板 → 入仓"；补 Options.params 透传 | 集成测试：经 IngestOrchestrator 载入自定义协议文件，数据集通道齐全 |
| P4 | L4 模块意图 + 编辑器视图（打开/展示/校验/保存 JSON/单帧试算） | GUI 测试：意图闭环；面板自注册；CLI 构建保持纯净 |

## 7. 第二批（本轮不做）

- 编辑器变体字段可视化增删、Lua 脚本重建、字段拖拽排序。
- 流传输（串口/网络）下的协议解码；本轮仅文件。
- 协议模板的样例库与向导新建。

## 8. 红线

- `src/` 只读参考，不改一行。
- headless 库不链 Qt Widgets；core 不加新概念。
- 解析器零拷贝纪律：喂进来的字节 buffer 不由解析器持有；产出批走 sink。
- 模块之间不 include；视图 TU 内自注册面板类型。
