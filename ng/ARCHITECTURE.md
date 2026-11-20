# PlotScope NG — 架构总纲

> **本文是架构设计，不是实现说明。**
> 设计输入是你的需求；老代码（`src/`）在**填代码阶段**可作参考（协议算法、画布绘制、
> ADS/SARibbon 用法），但**类清单与分层不照抄**——那是演进产物，不是设计。

> ✅ **终态裁决已落地（2026-11-20，B7 收口）**：`core/` 目录已彻底消亡，内容全部下沉 `libs/`；
> `AppContext` 归 `libs/context` 且唯一实例在组合根 `App`、其余层构造注入；libs 无全局活实例/单例/服务定位器。
> 本文正文里残留的 `core::` / `core/` 表述均为历史描述，以 `libs/` 现状为准。
> 边界已固化为 configure 期守卫 `cmake/LibBoundaryGuard.cmake`（违规编译不过）与 `AGENTS.md` §5.1。

---

## 1. 目标

做一款**数据 / 绘图 / 分析**软件，支持脚本、CLI、UI 交互。

一条典型流程贯穿全软件：

```
打开数据文件或流（可同时多个）→ parse 解析 → 数据给 Rust
                                    → 分析从 Rust 取数 → 绘图 → 出报告
```

下面每一条架构决策，都由这条流程或你列出的六条需求推出。

## 2. 需求 → 架构约束

| 你的需求 | 架构约束 | 推论 |
|---|---|---|
| (1) modules 是器官，是工具**不懂业务** | 器官只提供**机制 + 插槽**，不含任何领域判断 | "画什么曲线"不是 plot 模块的事，是扩展的事 |
| (1) 增一个 module 不改其他代码 | 器官**静态自注册**，宿主遍历装载 | 宿主/App 不出现任何具体器官类名 |
| (1) modules 相对独立 | 器官之间**只经意图通信**，互不 include 对方头文件 | 器官可单独装卸、单独测试 |
| (2) extension 是知识库 | 贡献类型**开放字符串**，知识包是聚合单位 | 加一类知识 = 新接口 + 注册槽，不改 core 枚举 |
| (2) 增一个扩展不改其他代码 | 扩展**静态自注册**进包表 | 同上 |
| (3) 百万千万级数据 | 数据本体**只在 Rust**；一切取数**有界** | C++ 永不物化全量；绘图内存 = O(像素宽) |
| (4) SARibbon + ADS | 外壳**只做绑定**，不自研 ribbon/docking | 第三方组件负责结构与交互，ng 只提供插槽数据 |
| (4) ps 数据 UI 管理与呈现 | 数据浏览与绘图是**两个独立器官** | 数据集浏览 ≠ 绘图渲染 |
| (5) 同时打开多个数据源 | **Source 是一等对象**，有 id 与独立生命周期 | 数据源 ≠ 数据集；多源对比 = 多 dataset id |
| (5) plot 页面直接交互 | View 是**可插拔类型**，交互框架由器官提供 | 12 种视图不必塞进一个 3800 行类 |
| (5) 分析掌握知识 | 分析的**算法来自扩展**，器官只调度 | "算什么"是知识，"怎么跑"是机制 |
| (5) 脚本掌握自动化 | 脚本经**同一意图管道**，可录制可回放 | GUI 操作与脚本调用等价 |
| (6) 录制与回放，**同时录多种格式、多种后端** | 录制 = **格式 × 后端** 的正交组合，两个独立注册表 | 一路录制 = (raw\|mcap, file\|udp, 端点)；多路 = 多个组合实例 |
| 数据源是广义完整的 | 三段聚合成一个 **DataSource 运行时实例** | 但聚合的是**绑定关系**，资产（解析器）仍全局共享 |
| 解析：**单前端 + 多解析器** | 绑定语义是**扇出**不是分流；`topic` 降级为可选过滤器 | 同一份字节同时喂给 N 个解析器，各产独立数据集 |
| app 统一管理 dataset/setting | 进程级运行态收敛到 **AppContext**（唯一持有者） | 器官只认 `ctx.app()` 一个入口，不再两条路径混用 |
| UI 只是显示 | AppContext **绝不含 QWidget 指针** | CLI 保持纯 Core（ldd 验证无 libQt6Widgets） |

## 3. 核心抽象：DataSource（来源 + 解析绑定 + 数据集）→ 消费者

```
   ┌──────────────┐   原始字节    ┌──────────┐   结构化批   ┌──────────────┐
   │   Source     │ ───────────▶ │  Parser  │ ──────────▶ │   Dataset    │
   │  数据源       │              │  解析器   │             │   数据集      │
   │              │              │          │             │              │
   │ 有 id、有状态 │              │ 纯转换    │             │ Rust typed 表 │
   │ 文件/流/实时   │              │ 不认存储  │             │ 唯一数据形态  │
   │ 可录可放      │              │ C ABI    │             │              │
   └──────────────┘              └──────────┘             └──────┬───────┘
          ▲                                                       │
          │ 录制/回放（字节级）                                      │ 只认 id + 时间窗
          └───────────────────────────────────────────────────────┤
                                                                  ▼
                                        ┌────────────┬────────────┬────────────┐
                                        │   View     │  Analysis  │   Report   │
                                        │   绘图      │   分析      │   报告      │
                                        └────────────┴────────────┴────────────┘
```

### 3.1 DataSource —— 广义完整的数据源（三段聚合）

需求原文：「一个广义完整的 datasource，一般是包含文件或者流 + parser + dataset」。

**架构判断**：这三段确实聚合成一个用户面对的概念，但**聚合的是"运行时绑定关系"，
不是"资产归属"**。三者的复用粒度不同：

| | 是什么 | 复用粒度 | 归属 |
|---|---|---|---|
| **Parser** | 知识资产 | 全进程一份，可被 N 个数据源共享 | 注册表（extension 提供） |
| **RecordingFormat** | 机制选项 | 同上 | 注册表（官方内置 + 插件可注册） |
| **Dataset** | 产物 | 被分析/绘图多方引用 | 仓库（Rust 拥有） |
| **DataSource** | **运行时实例** | 一次打开 = 一个实例 | 运行时登记表 |

于是：用户操作层面是统一的（打开一个数据源 → 立刻可用），
但它内部持有的是**对共享资产的引用**，而非资产副本——
同一份 ULog 解析逻辑被 N 个数据源共享，状态互不干扰（解析器实例是每次打开时新建的）。

```cpp
struct DataSourceInfo {
    int id;                        // 稳定标识；多源并存靠它区分
    QString name, origin;
    SourceKind kind;               // File / Stream / Live / Replay
    SourceState state;             // Opening / Streaming / Paused / Closed / Error
    QString error;
    qint64 bytesTotal, bytesRead;

    bool replayable;               // 原始流可被重放

    // 录制只是配置
    bool recordEnabled;            // 是否需要录制
    QString recordFormatId;        // 选哪种格式（raw/mcap/插件自定义）
    QString recordPath;

    QVector<ParserBinding> parsers;// 通道 → 解析器绑定（可多路）
    QVector<int> datasetIds;       // 该源解析出的数据集
};
```

**三段就绪是渐进的**，这正是 sniff 存在的意义：

```
刚打开      → 只有 ① 来源（还不知道是什么格式）
嗅探/选定后 → ① + ②（知道用谁解析了）
解析后      → ① + ② + ③（拿到了数据）
```

**按通道绑定解析器**：一个广义数据源里可能有多条语义不同的字节流
（MAVLink 链路有遥测与指令、CAN 总线有多个报文 ID、MCAP 录制文件每个 channel 是独立话题），
所以「源 → 解析器」不是一对一，而是**按通道的一对一**。

### 3.2 Parser —— 解析器（纯转换）

```c
/* C ABI：任意语言可实现，可放进 .so/.dll。当前 PS_PARSER_ABI_VERSION = 2 */
typedef struct PsParserVtable {
    uint32_t abi_version, struct_size;
    void *(*create)(void);  void (*destroy)(void *ctx);
    int32_t (*sniff)(void *ctx, const uint8_t *data, uint32_t size);
    int32_t (*feed)(void *ctx, const uint8_t *data, uint32_t size,
                    const PsParserSink *sink, PsError *err);
    int32_t (*finish)(void *ctx, const PsParserSink *sink, PsError *err);
    void  (*reset)(void *ctx);
    /* v2 尾部槽（按 struct_size 判定；无参数解析器可留空）：
     * create 后、feed 前调用一次，把宿主收集的 JSON 键值对参数交给解析器。
     * 解析器只取自己 schema 声明的参数，不读全局。 */
    int32_t (*configure)(void *ctx, const char *opts_json, PsError *err);
} PsParserVtable;
```

> **为什么升 v2**：v1 没有配置入口，解析器拿不到意图里的参数（CSV 分隔符、协议模板路径等），
> 只能读全局——破坏单测、重放与多实例隔离。v2 把 `configure` 升为正式语义槽：
> C++ 基类是普通虚函数 `configure(const QJsonObject&)`，C 插件走上面的可选尾部槽传 JSON 键值对，
> 两形态语义一致。`abi_version` 管"是否接受 v2 语义"，`struct_size` 管"这个 vtable 是否实现该槽位"，
> 两者正交（无参数解析器可合法留空 configure）。版本升级是硬切换，v1 解析器需重编到 v2。

职责边界（这是本设计的核心）：

| 角色 | 做什么 | 不做什么 |
|---|---|---|
| **Parser** | 字节 → 结构化批 | 不读文件、不写存储、不认识宿主、不管生命周期 |
| **调用者（宿主）** | 喂字节、收批、决定批去哪（Rust / 其它） | 不关心字节怎么解析 |

收益：同一份 ULog 解析器既能读文件、也能读串口/网络流；解析器可独立单测；
结果归宿由调用者决定，解析器不绑定 Rust。

**批结构复用老仓库 `ps_extension_api.h` 的 `PsFrameBatch`**——与 Rust `append_frame`
入参同构，落库零转换。老代码的 `builtin/*/datasources/*/XxxParser.{h,cpp}`
协议算法**原样复用**，只把"产出端"从"自己建 Dataset"改成"通过 sink 吐批"。

#### 两类解析器，不要混

| | 数据摄取 Parser（§3.2 上文） | 协议检查器 IMessageParser（新增） |
|---|---|---|
| 接口形态 | 纯 C ABI vtable（`ps_parser_spi.h`） | C++ 接口类 `IMessageParser`（内置）+ C ABI 镜像 `ps_message_parser_spi.h`（插件） |
| 产出 | `PsFrameBatch` 数值批 → Rust Dataset → 画图/分析 | `DecodedFrame` 可读字段树 → 协议检查对话框 |
| 消费者 | `IngestPipeline` → `data.load` | 专用协议对话框（`gui/shell/ProtocolInspectorDialog`），不产 Dataset |
| 用途 | 大数据绘图/分析主通路 | 逐帧看字段/枚举/校验，协议调试 |

- 为什么检查器是 **C++ 接口类**：它带 Qt 数据（`QVariant` 字段），且要"继承一个接口类、自述元数据"——
  继承 + 自述是 C++ 侧的自然形态。外部 C/Rust 插件仍走 C ABI 镜像 vtable，两者汇入同一注册表，
  宿主无感。**反例**：若把 C ABI vtable 删掉只留 C++ 类，外部 `.so` 协议插件永久进不来
  （C++ 虚表跨不了 dll 边界）——故镜像保留。
- **自述四要素**（对话框零改代码即可列新协议）：`protocolName()` 协议名、`domain()+usage()`
  使用范围（领域分类 + 人读用途）、`supportedTransports()` 支持的 transfer 协议
  （复用 `MessageTransport` id，与 `IByteSource`/`RecordingBackend` 对齐）、`configSchema()` 可配置参数
  （与 `IFormatDriver::importSchema` 同构的 JSON 字段描述）。
- **生命周期**：`create → configure(params) → parse → destroy`。`configure` 是普通虚函数，
  默认把参数快照存进受保护 `m_params`；派生类只用 `param(key, default)` 读取
  （支持 `time.mode` 点分嵌套）。**硬约束：只读 configure 传入的值，禁止读全局**
  （SettingsStore / QSettings / 环境变量 / 固定文件路径）——否则无法单测、无法重放、多实例串味。
  老 NMEA 解析器在构造函数里扫全局路径找模板，正是被这条约束纠正的坏味道：
  新实现默认用内置模板，换模板只能由宿主经 `database_path` 参数传入。
- 样板：`extensions/gnss/NmeaMessageParser`（继承 + 自述 + configure + parse）。

### 3.3 Dataset —— 数据集（唯一数据形态）

Rust 里的 typed 表集合。C++ 侧只有门面（句柄 + schema 投影 + 有界读取）。
消费者（View/Analysis/Report）**只认 dataset id + 时间窗**，不关心数据从哪来——
于是"多数据源对比"就退化成"一个视图里引用多个 dataset id"。

### 3.4 消费者：View / Analysis / Report

三者都是"从 Dataset 取数 → 产出东西"，但产出物不同：

| 角色 | 产出 | 机制归谁 | 知识归谁 |
|---|---|---|---|
| **View** | 像素 + 交互状态 | plot 器官（画布宿主、交互框架、LOD 取数） | 扩展（具体视图类型：姿态曲线/频谱/地图/3D） |
| **Analysis** | 指标 / 统计 / 频谱结果 | analysis 器官（调度、进度、结果缓存） | 扩展（算什么：飞行品质、振动指标…） |
| **Report** | HTML / PDF / CSV 文档 | report 器官（模板引擎、导出） | 扩展（报告模板与章节） |

> **老代码没有 Report**——这是你的新需求，架构上要给它位置：
> 它是"分析结果的再呈现"，属于**知识**（模板）而非**机制**。

### 3.5 录制：格式 × 后端的正交组合

需求原文：
> 「数据录制，可以同时录制多种格式（raw，mcap）、多种后端（文件,udp）」
> 「其实就是配置是否需要录制，以及录制格式是什么。官方默认提供了 raw 和 mcap，
>   插件可以定义其他的格式，注册进来就可以选。跟解析格式一样的……」

**架构判断**：录制有两个**正交**的维度，各自独立注册：

| | 格式 RecordingFormat | 后端 RecordingBackend |
|---|---|---|
| 回答什么 | 字节**长什么样**（容器结构） | 字节**送到哪**（投递方式） |
| 内置 | raw（裸字节流）/ mcap（消息容器） | file（本地文件）/ udp（组播） |
| 注册表 | `RecordingFormatRegistry` | `RecordingBackendRegistry` |
| 插件可扩展 | 是 | 是 |

于是**一路录制 = (格式, 后端, 端点)**：

```cpp
struct RecordingTarget {
    QString formatId;    // raw / mcap / 插件自定义
    QString backendId;   // file / udp  / 插件自定义
    QString endpoint;    // 文件路径 或 "host:port"
    bool enabled = true;
};
```

而一个数据源持有**一个 RecordingTarget 列表**——所以"同时录多种格式、多种后端"
不是特例分支，而是两个独立注册表的自然组合结果：

```
{raw,  file, "/tmp/a.raw"}
{mcap, file, "/tmp/a.mcap"}
{raw,  udp,  "239.0.0.1:5600"}     ← 三路同时工作，互不干扰
```

实现上，成帧器与输出端彻底解耦：

```
Recorder（组合层）
  ├── 成帧器（格式侧）: RawRecorder / McapRecorder  —— 决定字节长什么样
  └── 输出端（后端侧）: FileSink / UdpSink            —— 决定字节送到哪
```

`raw→file` 与 `raw→udp` 走**同一条成帧逻辑**；`mcap→file` 与 `mcap→udp` 也是。
这正是"格式 × 后端正交"在代码里的体现。

一份录制包含三样东西：

```
① 字节（消息体）   ② 通道/topic 归属   ③ 时间戳
```

raw 与 mcap 的差别只在**②③如何编码**；①都是原始字节——
因此**回放一定等价于原始**：同一批字节喂给同一组解析器，结果一致。
这就是录制的全部意义（确定性）。

| 格式 | 通道结构 | 按时间定位 | 适用 |
|---|---|---|---|
| raw | 无 | 否 | 单通道源，最简单通用 |
| mcap | schema + channel | 是（有 chunk 索引） | 多通道源，需按 topic 选择性回放 |

**归属层**：录制是 app 的**机制**，不是领域知识——所以 raw/mcap 由 app 官方提供，
插件可定义自己的格式注册进同一张表。与解析格式同构，但注册表分开。

### 3.6 AppContext —— 进程级运行态的唯一持有者

需求原文：「app 具备统一的 settings 管理」「ui 支持重启恢复之前的状态」，
以及「是不是应该创建一个 appcontext 或者 appsession 的类，持有 app 进程中的数据」。

**架构判断**：采纳 AppContext，**拒绝 AppSession**。理由有三，都能在代码里验证：

1. **AppSession 现在是空壳**。它的价值来自「多实例并存」（多窗口/多工作区），
   而需求(5) 说的是"同时打开多个数据源"——那是一个会话里多个**源**，
   不是多个会话。没有第二个实例，AppSession 只是一层转发。
   而"当前打开哪些源、布局如何"这类状态，`SettingsStore` 的 Workspace 作用域
   已经承载了。**不要重犯"照抄老代码的 SessionManager"的错。**
2. **AppContext 是真实需求**。此前 app 级状态分散在三处：`App` 持 repo/recorder、
   `RuntimeHost` 持 settings/toolbox、注册表是全局静态。于是器官取数据走
   `ctx.data()`，取设置走 `ctx.host().settings()`——**两条路径、两种持有方式**，
   这正是"上帝类"的温床。
3. **但必须有铁律**：AppContext **绝不包含任何 QWidget 指针或 GUI 专属类型**。
   CLI 与 GUI 共用同一份 AppContext；一旦引入 QWidget，CLI 的依赖树被污染，
   "纯 Core"承诺失效（ldd 验证无 `libQt6Widgets`）。

于是状态分三层，各归其位：

| 层 | 内容 | 归属 | 判据 |
|---|---|---|---|
| **进程级运行态** | DataRepository / SettingsStore / ExtensionRegistry / SourceRepository / IngestPipeline / 活动视图 | **AppContext** | 生命周期 = 进程，与 GUI 无关，可序列化 |
| 会话级 | （暂不建）多窗口/多工作区 | 未来需要时再引入 | 需要「多实例并存」才有价值 |
| 编译期资产目录 | ParserRegistry / RecordingFormat / RecordingBackend / BuiltinPack | **保持全局静态** | 靠静态初始化自注册，收进上下文会导致"多 App 实例时重复初始化" |
| GUI 纯展示态 | ribbon 页签 / dock 布局 / 视口缩放 / 曲线高亮 | **留在 GUI 层** | 不可序列化、CLI 不需要；放上下文会污染 headless |

装配顺序严格单向：

```
core: AppContext ──▶ host: RuntimeHost ──▶ app: App（组合根） ──▶ gui: MainWindow
```

器官取状态只认一个入口 `ctx.app()`。

**活动视图是"指针"不是"副本"**：存的是 id（活动源 id / 活动表 / 活动列），
数据始终在仓库里。这让"用户正在看哪个通道"成为**跨前端共享**的运行态——
GUI 点一下通道，脚本随后 query 得到同一答案。

#### 工作区数据源配置：存"配方"，不存"数据"

需求：「数据源的端口、文件、解析器、解析器参数、录制参数、后端、时间轴平移、
衍生数据都属于配置，需要保存到 settings」。落点判断：

- 这八项本质是**一份数据源申请单**的字段——"用什么参数打开哪个源"。它们是**原子**
  的：拆成独立键会出现"文件换了但解析器参数还是上一个源的"错配。故收成一个
  `WorkspaceDataConfig`（`core/WorkspaceDataConfig.h`），整份作为一个 JSON 块存
  `SettingsStore` 的 **Workspace** 作用域键 `ws/data/sources`。选 Workspace 而非
  Document/App：它是会话可恢复状态，跟随当前工作区但不作为可分享文档分发。
- **衍生数据只存配方**（`DeriveRecipeSpec`：origin + 表 + 列 + 算子 + 参数），
  **不存结果数组**。理由三条：① 结果是百万行，塞进 JSON 是灾难；② 数据只在 Rust，
  settings 里放数组违反内存铁律；③ 配方是可序列化意图，重开时用 `data.derive`
  让 Rust 重算即可。配方用 `sourceOrigin` 而非会话期 `datasetId` 定位通道——后者
  每次打开都可能变，存下来重放就指错了。
- **恢复 = 重放**：`data.workspace.restore` 读配置，对每个源发 `data.load`
  （带 `record=false`，避免把恢复出来的源又当新打开重复记录），对每条配方发
  `data.derive` 重算。数据本体始终在 Rust，settings 全程只有"怎么做"。
- `data.load` 成功即增量记一条申请单；`data.workspace.save` 负责 flush 到磁盘。
- **当前边界（如实标注）**：来源、解析器 id、派生配方的存取与重放已闭环；
  解析器参数、录制格式×后端、时间平移的**字段已进配置结构**，但 `data.load`/
  摄取管道尚未接收这些参数，故重放时暂不生效——待后端接入，不在这里造空按钮。

---

### 3.7 摄取管道：单前端 → 多解析器扇出 → 多数据集

需求原文：「数据解析，一般只能选择一个前端（文件或流），但是可以选择多种解析器
（因为源，比如 udp 中可能包含多种格式数据），也就是将这份数据同时喂给多个 parse，
出的结果落库 dataset。」

这条需求决定了绑定语义是**扇出**而非分流：

```
字节流 ──┬──▶ parser A（独立实例 a1）──▶ dataset A
         ├──▶ parser B（独立实例 b1）──▶ dataset B
         └──▶ parser C（独立实例 c1）──▶ dataset C
```

三个要点：

1. **扇出而非分流**：同一份字节喂给所有绑定的解析器，不是"按 topic 分给一个"。
   每个解析器自己 sniff / 识别自己关心的部分——一个 udp 源里可能同时混着
   多种格式（MAVLink 与 GNSS NMEA 交织）。
2. **实例隔离**：每个 (数据源, 解析器) 组合一个独立实例（create/destroy），
   状态互不干扰。被共享的是**解析代码**，不是解析状态。
3. **产出隔离**：每个解析器的帧落到**自己的数据集**，不混表、不互相覆盖。

因此 `ParserBinding` 的语义变了：

```cpp
struct ParserBinding {
    QString parserId;   // 绑定的解析器（扇出的一路）
    QString topic;      // 可选通道过滤（空 = 接收全部）——不再是路由键
    bool active = true;
};
```

落地是 `context::IngestPipeline`：持有通道表，喂字节时扇出给所有活跃通道；
一个解析器报错只记在该通道上并继续其它通道（一个格式坏了不该拖垮整条源）。

**内存纪律**：C++ 侧只保留一个批次缓冲，不物化全量；帧立即 `appendFrame` 进 Rust。
schema 投影的刷新是 O(行数)，因此**只在通道收尾时刷一次**，绝不每批都刷。

#### 3.7.1 三层分工：格式知识 / 宿主策略 / 机制

归并数据通路后，一件事变得清晰：**不是所有"跟数据有关"的东西都属于解析器**。
SPI 解析器的职责被刻意收窄为「字节 → 结构化批」，于是有三类东西分属三层：

| 层 | 归属 | 具体 | 为什么不在别处 |
|---|---|---|---|
| **格式知识** | 解析器（SPI） | 协议怎么解、哪些列是数值、时间单位是秒还是微秒、列名从哪来 | 只有格式本身知道 |
| **宿主策略** | 编排器（`IngestOrchestrator`） | 缓存（源文件指纹）、stable id（录制/回放）、表名改写 | 与"这次怎么加载"有关，与"格式是什么"无关 |
| **机制** | 管道（`IngestPipeline`） | 扇出、实例隔离、批次缓冲、schema 刷新时机 | 与内容和策略都无关 |

三者的接口边界：

- **表名**：解析器只认字节，给不出"这张表叫什么"（CSV 该叫文件名还是 topic？）。
  所以由扩展层**声明**（`IFormatDriver::preferredTableName()`），
  宿主在扇出绑定时转成通道的改名规则（`IngestPipeline::setTableRename()`）。
  只在**单表格式**上声明——多表格式（ULog 的多 topic）的表名由内容决定，宿主不插手。
- **缓存**：粒度是 **(源文件, 解析器)**——因为管道下每个解析器一个数据集，
  缓存的正是"某一路解析出来的数据集"。路径 `<源文件绝对路径>.<解析器id>.psc`。
  命中时**跳过解析**直接还原数据集，省掉的是解析开销。
- **stable id**：`IngestPipeline::bindParser(..., preferredDatasetId)` 在**入仓那一刻**
  就用指定 id，录制/回放时数据集 id 因此不随会话漂移。

这条分工的价值：**新增一种格式只需写一个解析器**，不必再写一份"读文件 + 缓存 + 入库"的外壳。
外壳只有一份，在宿主里，天然覆盖所有格式——包括将来从插件进来的格式。

---

### 3.8 数据变换与衍生：两个概念，原始数据零污染

变换与衍生是**两个概念**（设计语义经用户逐条敲定）：

| | 变换（transform） | 衍生（derive） |
|---|---|---|
| 本质 | 非破坏视图：只改值不改字段名 | 独立派生字段：生成新字段 |
| 归属 | dataset 级配方（`data.transform.set`，settings 持久化）或 plot 级曲线配方 | 派生注册表（`DerivedSeriesRegistry`，宿主层 `libs/context`） |
| 作用域 | dataset 级对**所有后端**生效；plot 级只影响当前曲线 | 全后端可读（`data.series` / `plot.channel` 按派生名穿透） |
| 原始数据 | 永不写回 | 永不写回（不污染 dataset） |
| 形态 | SISO：值进值出（op 算子 / 数学表达式 / Lua） | SISO（previewFilter）+ MIMO（如 q0-q3→roll/pitch/yaw） |

关键裁决：**派生注册表下沉 `libs/context`**（AppContext 持有）而非 host——`context`
不能反向依赖 host，派生属于"跨前端共享的应用状态"，与源注册表同级。

#### MIMO 派生的时间同步语义（基准轴 + 显式对齐）

`data.derive { filter: "quat_to_rpy", inputs: [w,x,y,z] }` 的多输入时间同步不依赖
"行对齐"假设，统一走**基准轴 + 插值对齐**：

- 基准轴 = **第一个输入的时间轴**；所有其它输入对齐到它，输出与基准同轴。
- 门面 `Dataset::readWindow` 会把 null/NaN 样本**成对剔除**（绘图语义：null 点不画），
  同表各列读出点数因此可能不同——行对齐不可假设，必须显式对齐。
- 同表且轴完全一致（无剔除）走快路径直取；否则插值。时间点完全重合时插值恒等于
  原值，精度无损；跨表不同采样率同理被同一机制覆盖。
- `interp`: `linear`（缺省）/ `step`（前向填充）/ `nearest`；范围外取端点值
  （边界延拓，不产 NaN）。输入元素支持 `"列名"`（用顶层 table）或
  `{table, column}` 对象（跨表；不用限定名字符串——列名本身可能含点，如 `q.w`）。
- 防线：对齐后仍含 NaN 的输入**明确拒绝**（`enan`），绝不静默产出垃圾角度。
- 数学底座：Rust `ps_quat_to_rpy`（数组级 FFI，不写回 dataset），公式对齐
  PJ4 `toolbox_quaternion`。

**插值边界纪律（硬规则）**：插值**只允许出现在派生对齐路径**。绘图与数据出口
（`readWindow` 抽稀、`lodMinMax` 分桶、游标最近样本读数、`data.series`）一律
输出原始样本点——抽稀只丢点不造点，null 点跳过不造点，绝不插值造点，否则
会破坏绘图点的准确性。这与 PJ4 同纪律：其绘图永远画样本点，`atTime` 插值
只用于 formula/marker 查询语义。

**与 PJ4（增量引擎）的对账**：PJ4 的 MIMO 对齐在引擎层 join——精确时间戳交集
（各输入时刻必须完全相等，否则丢弃），"nearest-sample-to-primary" 仅是头文件
注释中预留的契约、未实现；对象级多输入用"≤ 请求时刻的最新样本"（step 语义）。
NG 的插值对齐覆盖了 PJ4 覆盖不了的跨采样率派生；NG 的**增量执行**（见下）也已
落地，补齐了 PJ4 chunk 水位推进的效率优势。

**派生增量执行（水位机制，对齐 PJ4 chunk 水位思想）**：

- 注册表 Entry 携带 `waterUs`（基准轴水位，微秒整型）与 `recipe`（配方指纹：
  inputs 对象数组 + interp + degrees + unwrap）。
- **同名重发 `data.derive` = 增量刷新**：指纹一致且未 `force` → 只读
  `[waterUs, tNow]` 新样本窗口（tNow = 各输入表 `lastTimeUs` 最大值，O(1)），
  只算新段，追加缓存；水位推进到新段最大时间戳。指纹变了自动回全量。
- `data.derive.refresh {dataset}`：对该数据集全部 MIMO 派生按配方指纹分组逐组
  增量刷新（同组三条输出，首条推进水位后其余自动 no-op）。返回
  `groups`/`refreshed`（refreshed 只计产出新样本的组）。
- 无新样本 → 幂等 no-op（`noop=true, count=0`），不重复计算。
- 边界纪律：增量窗口内时间比较一律在微秒整型域做（double 秒往返 `llround`
  精确），避免 1ulp 误判；其它输入窗口含 waterUs 时刻样本是合法插值左锚点
  （输出行时间全部 > waterUs，不重复输出）。
- 实时 `appendFrame` 不刷 C++ schema 投影（O(列数)，不能每批调）——增量刷新
  是低频操作，入口处刷一次拿最新 `lastTimeUs`。

---

## 4. 器官（modules）：机制提供者

### 4.1 「是工具不懂业务」的操作化定义

一条硬规则：**器官里不允许出现任何领域词汇**（飞行、姿态、CAN、电池……）。
器官提供**机制 + 插槽**，具体内容由扩展填。

| 器官 | 提供的机制 | 声明的插槽 | 明确不做 |
|---|---|---|---|
| `mod.source` | Source 生命周期：打开/关闭/录制/回放；驱动 Parser | `source.list` 面板 | 不认识任何格式 |
| `mod.dataset` | 数据集目录：表/列/时间范围查询；订阅变化 | `dataset.tree` 面板 | 不认识格式，不取业务语义 |
| `mod.plot` | View 宿主：多页面、画布、LOD 取数、交互框架 | `view.canvas` 中央区 | 不知道画哪种曲线 |
| `mod.analysis` | 分析调度：执行、进度、结果、报告触发 | `analysis.result` 面板 | 不知道算什么指标 |
| `mod.report` | 报告产出：模板渲染、导出 | `report.preview` 面板 | 不知道报告内容 |
| `mod.script` | 脚本引擎：Lua 门面、Job 录制回放 | — | 不知道业务语义 |

> **实现状态（2026-11-20）**：上表已落地。`mod.data` 按 source/dataset **拆开**（v1 合并判为错）、`mod.report` 暂不建成品（只备扩展点）；`mod.protocol`（协议编辑）与 `mod.settings`（设置）已实现，见 §11。数据模型 `Dataset`/`DataRepository` 已下沉独立库（`ng/libs/dataset`）；注册表/意向/schema→UI 三项机制已下沉 libs。

### 4.2 器官之间相对独立

- 器官之间**只经意图通信**，互不 include 对方头文件；
- 每个器官声明 `provides` / `requires` 能力名，宿主做拓扑排序；
- 装卸互不牵连：卸掉 `mod.analysis` 不影响 `mod.plot` 继续画图。

### 4.3 新增一个器官不改任何既有代码

器官在自己的 TU 里静态自注册：

```cpp
// 器官 TU 末尾一行：
NG_REGISTER_MODULE(SourceModule)   // 需提供 moduleInfo() / create()
```

宿主启动时遍历全局表装载。**宿主、App、其它器官一律不改。**
承载自注册的 target 必须是 **OBJECT 库**（静态库里未被引用的 TU 会被链接器丢弃，
其静态初始化不执行，注册会静默失效）。

## 5. 扩展（extensions）：知识库

### 5.1 一个知识包聚合什么

以飞控包为例，它聚合**一个领域的所有知识**：

| 贡献类型（kind） | 飞控包里的内容 |
|---|---|
| `format` | ULog 文件的识别规则与后缀 |
| `parser` | ULog 协议解析器实现（复用老代码算法） |
| `view` | 姿态曲线视图、航迹视图、电机转速视图 |
| `analysis` | 飞行品质指标、振动评估、能耗分析 |
| `report` | 飞行报告模板 |
| `ui` | ribbon 入口、面板、画布工具 |
| `script` | 飞控脚本库（自动化示例） |
| `data` | 样例数据资产 |
| `doc` | ULog 协议文档、指标定义 |

**kind 是开放字符串**（不是封闭枚举）——因为知识清单会持续生长，
封成枚举每加一类就要改 core 并全量重编译，违反开闭原则。
加一类知识 = 新增一个 typed 接口 + 一个注册槽，**不改 core、不重编译宿主**。

### 5.2 知识包是装载/卸载的原子单位

装一个飞控包 = 一次性登记它的格式 + 解析器 + 视图 + 分析 + 报告 + UI；
卸一个 = 一次性撤销全部。包之间天然隔离。

### 5.3 新增一个扩展不改任何既有代码

包在自己的 TU 里静态自注册（`NG_REGISTER_PACK`），两条发现路径汇入同一张注册表：

```
内部扩展：NG_REGISTER_PARSER / NG_REGISTER_PACK  ─┐
                                                    ├─▶ ParserRegistry / ExtensionRegistry
外部插件：目录扫描 → dlopen → ABI 校验 ────────────┘
```

### 5.4 注册表全景：一张表说清谁是什么

**架构裁决：`Registry<T,Key>` 是唯一的容器机制，其余全是数据，挂进去就完事**
（uORB 的组织方式：类型提前定义好，接口极少，任何新能力都不新增概念——只是
又一条 `<T>` 数据）。所有注册表的容器本体都委托它（保序：keys() 按插入序），
壳只保留各自的领域语义。看名字就知道层次：

| 层 | 注册表 | 容器本体 | 壳保留的领域语义 |
|---|---|---|---|
| **机制**（libs/registry） | `Registry<T,Key>` / `StaticRegistry<T>` | —— 自身 | 纯值容器：insert/find/remove/keys/values/clear，保序 |
| **数据挂载**（libs/registry） | `ExtensionRegistry` | 七张 `Registry<T>`：packs / uiActions / panels / scripts / dataAssets / docs / examples | mount/unmount 按包原子装卸（写 provider/domain 归属字段）、解析器视图（唯一行为式条目） |
| | `ParserRegistry` | `Registry<ParserEntry>`（静态） | C ABI 解析器资产（进程一份）；插件句柄同 id 存放 |
| | `PanelFactoryRegistry` | `StaticRegistry<工厂>` | 面板实现工厂（编译期自注册） |
| | `TransformRegistry` | `Registry<TransformDescriptor>`（静态） | 内置算子不可覆盖（领域语义）；保序=all() 顺序 |
| | `BuiltinModuleRegistry` | `Registry<ModuleEntry>`（静态） | 模块工厂（编译期自注册），与包自注册同构不再复写 |
| | `UiSlotRegistry` | 插槽声明 + 绑定视图 | 骨架槽唯一声明处、动作→槽绑定 |
| **运行态**（会话实例） | `DatasetRegistry`（原 DataRepository） | `Registry<Dataset,int>` | id 分配、datasetAdded/Removed 信号 |
| | `SourceRegistry` | `Registry<DataSourceInfo,int>` | id 分配、信号、解析器绑定关系、dataset 反查 |
| | `DerivedSeriesRegistry` | `Registry<Entry,QString>` | 同名覆盖刷新缓存、水位推进、按 dataset 级联清理 |
| | `SinkRegistry`（规划） | `Registry<SinkSession,int>` | 流录制会话：id 分配、start/stop、样本计数 |

**贡献体系纪律（无虚基类、无包装类、无 kind 字符串索引）**：

- 六类贡献 = 六个纯数据 struct（`UiAction` / `PanelSpec` / `ScriptSpec` /
  `DataAsset` / `DocEntry` / `ExampleEntry`，见 `Contribution.h`），身份
  （id / provider / domain）是字段，不是虚函数；provider/domain 由 mount()
  自动写入，包作者永远不填。
- 包 = 一份纯数据清单（`Pack`：六个列表 + 解析器所有权）。包作者填清单 →
  `toolbox.mount(std::move(pack))` 挂进去；`unmount(packId)` 按 provider 原子撤。
- 查询 = 按类型读表：`toolbox.uiActions.find(id)` / `panels.values()`，类型即
  身份，无转型、无 `(kind,domain)` 二维索引（领域过滤就是对条目 domain 字段过滤）。
- 解析器是唯一有行为的贡献（真有 parse），走注册表内的解析器通道，与数据表并列。
- **静态自注册纪律**：进程级静态表一律 `StaticRegistry<T>` / `Registry<T>`（函数内
  static 持有），禁止手写 `static QHash` + 自造 register/get/all——曾出现
  TransformRegistry / BuiltinModuleRegistry / ParserRegistry 三处手写复本，已收敛。
- 历史遗留已清除：`IToolContribution` / 6 个 `I*Contribution` / `IToolPack` /
  `ToolPack` 六个 add 方法 / `IndexedByKind` / `ContributionKind`（kind 字符串）全部删除。

**词汇表纪律**：`Repository` 一词已消灭——会话实例容器一律 `XxxRegistry`，
内部委托泛型表；看到类名即知"运行态实例"还是"进程级能力"。

**Sink 裁决（流数据录制，非 job 录制）**：

- Sink 是**流数据的出口会话**：实时源样本流入 → 落盘/转发，与 Source（入口）
  对称。`JobRecorder`（意图录制回放，automation 用）不是 Sink，两者无关系。
- 录制**格式**（csv 增量追加 / NDF 落盘）是能力，作为 `exporter` 贡献登记进
  ExtensionRegistry（与 parser"怎么读"对称的"怎么写"）；
- 录制**会话**（"正在录制到 xxx.ndf，已写 N 样本"）是运行态实例，进
  `SinkRegistry`（委托泛型表），壳保留 start/stop/计数语义。
- 意图面：`data.sink.start / data.sink.stop / data.sink.list`。

## 6. GUI：外壳 + 功能视图（两层）

**不自研 ribbon / docking**——SARibbon（MIT）与 Qt-ADS（LGPL-2.1）已在仓库内 vendored，
不联网、不引新依赖。ADS 以共享库方式链接（与老仓库一致）。

外壳**不设** `DockManager` / `RibbonManager` 这类中转类：
第三方组件已提供对应能力，再包一层同名类是零价值转发。
ng 只保留**一个绑定器**：

```
registry::UiSlotRegistry（纯数据：有哪些页签/分组/面板/动作）
        ↓
gui::ShellBinder（唯一知道 SARibbon / ADS 的地方）
   ribbon.page  → SARibbonCategory
   ribbon.group → SARibbonPanel
   action       → QAction → SARibbonToolButton
   dock.*       → ads::CDockWidget
   canvas.*     → 中央 dock
```

插槽是**纯数据**（在 core，无 QWidget），所以：

- headless 也能查询"某领域贡献了哪些入口"（CLI 可用）；
- 业务 UI 随知识包装卸，app 骨架不动；
- 第三方组件可整体替换，只改绑定器。

#### 骨架槽：唯一声明处

骨架槽（ribbon 页签/分组、左右停靠区、中央画布、画布工具、菜单）是 app 自己
提供的**容器**，声明在 `UiSlotRegistry::declareSkeletonSlots()` —— **一处**。

为什么必须一处（这是一次真实缺陷换来的）：`declareSlot` 是幂等的（先到者为准），
若让 host 层与 GUI 层各自声明：

| 后果 | 说明 |
|---|---|
| **信息更全的那份被丢弃** | host 层先声明（构造即跑），但它不知道 GUI 的容器语义——没有 `kind`、没有父槽、label 也不准；GUI 层那份更完整却被静默挡掉 |
| **label 不准** | `dock.left` 显示"左侧面板"而非"数据集"，`dock.right` 显示"右侧面板"而非"属性" |
| **幽灵槽** | 只在 host 层声明、GUI 层无容器实现的槽，其 `kind` 落成默认值；扩展往里填的动作因此没有真实去处 |
| **headless 看到残缺的骨架** | 只在 GUI 层声明的槽（ribbon 页签/分组、中央画布）在 CLI 里完全不可见 —— 槽集变成半套 |

单一来源同时解决"重复"与"残缺"：GUI 与 headless 拿到**同一份**完整槽集。
契约测试钉死它（`test_architecture`：槽齐备、kind/父槽/label 完整、无未归属贡献）。

### 6.1 按功能分组，不产生上帝类

需求原文：「不要把所有的 ui 功能全部塞到 mainwindow.cpp，导致以后无法管理，
生成了一个上帝类 …… 按照功能器官拆分，一个功能组织在一起。」

**架构判断**：GUI 分两层，**外壳层不认识任何功能视图**：

```
gui/
  shell/                    外壳层：只做装配，零业务
    MainWindow               窗口骨架：建 ribbon/dock、装器官、存取状态
    ShellBinder              唯一知道 SARibbon / ADS 的地方
    GuiContext               host::UiContext 的实现，把器官的 UI 需求路由出去
    PanelFactoryRegistry     部件类型 → QWidget 工厂（视图层自注册进这里）

  views/                    通用绘图能力（将来抽成独立绘图库，类似 Qwt）
    plot/                    绘图画布 + 通道视图 —— 是**可独立复用的绘图库**，
                             不属于任何器官/领域；bench/shot 等工具也直接用它
```

**plot 的演进方向**：绘图会被抽取成一个类似 Qwt 的独立库（`views/plot/` 是它的
前身），所以它**不属于任何器官、不聚合进 `modules/`**——它是能力库，器官
（`PlotModule`）只是通过 `requestPanel("canvas.center", "plot-canvas")` 使用它。
这也决定了它必须留在 `gui/views/`（或未来的 `libs/plot/`），而非功能目录。

#### 统一定则：物理位置与逻辑位置正交

**这是每个 module 都必须遵守的定则，不是 data 的特例：**

> **物理位置为开发管理，逻辑位置靠 CMake 表达，两者正交。**
>
> - **物理位置**（文件放哪个目录）只服务于**开发时便于查找、编辑、管理**：
>   一个功能的器官、专属 UI、action、意图聚合在一起，作为完整单元。
> - **逻辑位置**（这段代码属于 headless 还是 GUI、进哪个 target）**一律由 CMake 表达**，
>   与文件放在哪个目录无关。
> - 两者**正交**：不因逻辑归类去搬目录，也不因物理聚合去模糊逻辑边界。

据此，一个功能（器官 + 它的 UI + action + 意图）**物理上放在一个目录**，
靠 CMake 拆成两个 target 实现**编译期分离**：

```
modules/data/                     一个"数据"功能，物理聚合
  DataModule.{h,cpp}          ┐ → ng_module_data     （headless，CLI 装载）
  DataManagerPanel.{h,cpp}    ┘ → ng_module_data_gui （GUI 装载，链 Widgets）
  CMakeLists.txt                  在此把 headless / GUI 两类文件分开
```

**一个反复出现的误区，在此钉死**：「DataModule 只是 UI 贡献者 / 是空壳，所以该搬去
`gui/` 或删掉」——这是拿**逻辑归类**去干涉**物理布局**，方向就错了。data 的非 UI 能力
（load/reload/export/...）是宿主级 handler，UI 是专属视图，它们物理上仍同处
`modules/data/`；逻辑归属由 CMake target 划分，headless 链不装载 GUI 那一半即可。
目录不该因为"算不算 module"这种归类问题而变动——**归类影响的是 target，不是文件夹。**

判据（一个 view 归哪）：

| view 的性质 | 归属 | 例子 |
|---|---|---|
| 只服务一个功能的专属 UI | **聚合进该功能目录**，`<功能> + <功能>_gui` 两个 target | `dataset-tree` → `modules/data/` |
| **通用能力库**（可独立复用，将来抽成 Qwt-like 库） | **留 `gui/views/`**（未来 `libs/plot/`），不进任何器官 | `plot-canvas` —— 绘图是能力库，不是器官 UI |
| 形态随领域变（知识包提供） | **留 `extensions/<域>/gui/`**，永不进 modules | `car.can-browser` |

注意区分"跨功能"与"通用能力库"：`plot-canvas` 留 `gui/views/` 的根本原因是
**它是要独立成库的绘图能力**，而非单纯因为被 bench/shot 多处使用——后者只是
"可独立复用"的表现。领域面板则相反：它**永远属于领域知识**，放 `extensions/<域>/gui/`，
既不进 `modules/`（器官是机制，不懂业务），也不进 `gui/views/`（会污染通用层成领域杂物箱）。

> **绘图内核已落地为 `libs/fastplot/`**（原"将来抽 Qwt-like 库"兑现）：纯 Qt Widgets、
> 不依赖 core，含视口/刻度/轴/包络曲线/缩放平移读数/六仪表/频谱柱状等部件。
> `gui/views/plot/PlotCanvas` 退化为**宿主**：只做取数（Rust LOD）+ 把 LOD 喂给 fastplot，
> 不再自绘（旧 `paintEvent` 已删）。**集成边界**：fastplot 库不认识 ng 主题/数据，
> 颜色由宿主在 `applyPalette()` 里从 palette 注入，曲线色在 `pushToPlot` 时按当时主题取——
> 库保持纯净，定制在宿主侧做，不往库里塞 ng 概念。集成测试：`fastplot_*` 三套 + `ng_plot_io`。

关键：**物理聚合不改逻辑解耦**——器官仍只发字符串 id（`requestPanel`），
不 include 视图头；视图靠 `NG_REGISTER_PANEL` 自注册到 core 工厂表。
CMake 保证 CLI 只链 headless 那一半：`ldd` 验证 CLI 与 headless 测试
都无 `libQt6Widgets`，也不含 `DataManagerPanel.cpp.o`。

两条铁律：

1. **外壳不 include 任何 views/ 下的头文件**。MainWindow 从 201 行降到 117 行，
   且对视图类的引用数为 **0**。
2. **视图之间靠 AppContext 的共享状态协作，不是互相引用**：

```
数据集树双击 ──▶ AppContext.setActiveChannel(数据��, 表, 列)
                            │（共享状态，星形）
                            ▼
                  绘图视图订阅 activeChannelChanged
                            │
                            ▼
                  走意图层 plot.channel 取像素数据 ──▶ 画布
```

于是数据集树**不认识**绘图视图，绘图视图也**不认识**数据集树。
加第三个视图（比如"频谱面板"）时，它只要订阅同一个信号即可，
**谁都不用改**。若改成视图间直接调用，就成了网状结构，加一个视图要改多个文件。

### 6.2 视图层自注册

每个视图在自己的 TU 里静态登记自己的工厂：

```cpp
// modules/data/DataManagerPanel.cpp 末尾（功能专属视图已聚合进功能目录）
QWidget *createDatasetTreePanel(AppContext &, IIntentExecutor &, QWidget *);
NG_REGISTER_PANEL("dataset-tree", datasetTree, createDatasetTreePanel)
```

器官只声明类型字符串（`ui.requestPanel("dock.left", "dataset-tree")`），
不依赖 Qt Widgets——CLI 保持纯 Core。

**工程要点**：`ng_gui` 必须是 **OBJECT 库**。静态库中未被引用的目标文件会被
链接器丢弃、静态初始化不执行，自注册会**静默失效**（工厂表为空、功能消失
且无任何报错）。契约测试显式断言两个视图都已登记，守住这条不变量。

### 6.3 领域自带面板：形态随业务变

器官需要某个面板，但**面板的形态本身就是领域知识**时怎么办？
（CAN 报文浏览器长什么样取决于 DBC 信号；波形调参面板取决于参数表；
地图轨迹面板取决于地图数据。）

结论是：**形态归知识包**，而不是塞给 `gui/views/`（那会让通用视图层长成领域杂物箱），
也不是让器官给出（器官是机制，不懂业务）。

分工是本机制的核心：

| 层 | 载体 | 内容 | 谁可见 |
|---|---|---|---|
| **描述**（纯数据） | `registry::IPanelContribution` / `PanelSpec` | id / 标题 / 建议区域 / 图标 / 附加数据 | headless 也可见（CLI `--list-contributions panel` 可查） |
| **机制**（纯数据） | `registry::PanelFactoryRegistry` | `QHash<id, 工厂>`；`QWidget` 只前置声明 | registry，谁都能调 |
| **实现**（QWidget） | 知识包的**呈现层** | 真正的面板类，TU 末尾 `NG_REGISTER_PANEL` | 仅 GUI |

描述与实现用**同一个 id** 关联。知识包因此拆成两个产物：

```
extensions/<域>/               知识层：协议 + 描述（纯数据，CLI 也链接）
extensions/<域>/gui/           呈现层：QWidget 面板（只 GUI 链接）
                                  依赖方向：呈现层 → core，不依赖 ng_gui
```

**器官侧的用法**（它不知道面板是领域还是通用、由谁提供）：

```cpp
// 1) 按领域问清单（纯数据，headless 也成立）
auto specs = ui.availableDomainPanels(FormatDomain::Automotive);
// 2) 按清单里的 id 请求；实现来自同一张工厂表
QWidget *p = ui.requestDomainPanel(spec.area, spec.id);
```

`requestDomainPanel` 会用 `PanelSpec::label` 覆盖通用插槽的标题——
领域面板的形态（含标题）由知识包决定，而不是被 `dock.right` 的"属性"覆盖掉。

**两条工程不变量**（都在契约测试里钉死）：

1. **呈现层必须是 OBJECT 库**：自注册靠 TU 静态对象构造函数，STATIC 库会丢弃未引用目标文件。
2. **每个需要领域面板的可执行文件都必须显式链接呈现层**——包括 GUI 测试。
   这一点曾经踩坑：`ng_test_gui` 没链 `ng_ext_automotive_gui`，`CanBrowserPanel.cpp.o`
   根本没进测试二进制，注册自然不发生，表现为"工厂表里只有两个通用视图"。
   headless 测试则**故意不链**，借此守住 CLI 不含 `libQt6Widgets`。

`test_gui` 的闭环：知识包声明描述 → 工厂表出现实现 → 器官按领域查清单 →
按 id 请求到面板 → 标题/形态由知识包决定。`test_architecture` 则确认描述是纯数据，
headless 也能查到。

### 6.4 主题子系统：token 化配色，深浅双主题

主题是**横向 GUI 基础设施**（深浅两套主题必须同时成立，见根 `AGENTS.md` §12）。
它不抄老代码 `ThemeManager` 的 389 行结构，按新架构重新设计为一条单向链：

```
语义 token  →  双主题色表（唯一色值来源）  →  QSS + palette  →  各控件
     ↑                    ↑
  视图只认 token      ThemeManager 认领 theme.* 意图
```

**组成部分**（都在 `gui/theme/`，不进 core）：

| 部件 | 职责 | 关键约束 |
|---|---|---|
| `ThemeManager` | token 取色、双主题色表、QSS/palette 生成、当前主题状态与持久化、认领 `theme.set/toggle/get` | 十六进制色值**只在色表出现一次**；视图一律 `color(token)` / `styleSheet()` |
| `ShellBinder::applyTheme` | 把 token QSS 套到 SARibbon | 绑定器是唯一知道 SARibbon 的地方（§7 原则）；不碰 ThemeManager 具体类型 |
| `PlotCanvas`（fastplot 画布宿主） | `changeEvent(PaletteChange)` 里调 `applyPalette()`，从 `palette()` 取色重设 fastplot 的画布/轴/标题 | fastplot 库内不认识主题、颜色在构造时定死；宿主侧用 palette 覆盖，库保持纯净；曲线色在 `pushToPlot` 时按当时 palette 取 |

**三条设计判据**（都是踩过坑或守住红线的结果）：

1. **三通道一起换肤，缺一不可**。只设 QSS 会漏掉自绘画布与 SARibbon 内部控件；
   必须 `Fusion style + 全局 palette + 全局 QSS` 三管齐下，再给 SARibbon 单独设
   token QSS（它自绘、全局 QSS 压不住）。实测反例：只设 QSS 时画布与 ribbon 保持浅色。
2. **主题走意图，且是系统级**。`theme.set/toggle/get` 由 GUI 装配期创建的 ThemeManager
   以 `@system/theme` 注册（不经模块初始化期）；按钮只声明 `intentOp`，点击/脚本/回放
   同一条路径。headless 无此 handler，收到 `theme.*` 诚实报"无处理器"——主题是纯展示态，
   不该在无界面的 CLI 里造假实现。
3. **生命周期归宿主**。ThemeManager 的 QObject parent 是 `RuntimeHost`，与系统级 handler
   同生共死；`themeChanged` 信号驱动 ribbon 换肤，主题状态写入 `app/theme`（SettingsStore，
   跨工作区）。

`test_gui` 钉死：双主题同 token 不同色、契约 token 齐全、未知 token 显洋红告警、曲线色
随主题、`mapStyleSheet` 替换、意图切换改状态并发信号、全局 QSS/palette 落地。

### 6.5 数据源面板：源卡片 + 字段/参数/消息三视图（对齐老 dock 与截图）

老代码 `DataSourceDock` 2442 行、50+ 个注入回调，是"胖视图"。ng **不搬它的实现**，
只对齐它承载的**信息层与交互形态**（见 `docs/screenshots/PlotScope-Source-Inspector.png`），
数据全部走 ng 仓库，按新抽象重做：

```
标题：数据源 · N 已连接
上段：源卡片        DataSourceRepository（状态点 / 名称 / N 通道 / 元信息 tooltip）
   ↓ 选中
中段：字段 N | 参数 N | 消息 N  ← 三 chip（计数来自真实元数据）
      [ 搜索字段 / 参数 / 消息 ]
下段：检查器（QStackedWidget）
      字段树   数据集 → 表 → 列（列可拖到画布 / 双击激活）
      参数表   参数 / 值（ULog params/facts 文本键值表）
      消息表   消息 / 速率 / 字段 / 首时刻（事件 / 话题时序表）
```

**为什么是三视图**（纠正一次历史偏差）：字段/参数/消息是老 dock 已确立、
截图沿用的领域信息分层——字段是数值通道（画图用），参数是键值配置，
消息是事件/话题流，三者的数据形态和消费者都不同。曾有一版按"新架构统一产出表"
擅自把它压成单一"数据集→表→列"树、宣称"参数/消息不再单列"——那是脱离需求的
架构想象，截图里明明有三 tab，已纠正回来。**需求/截图优先于我的架构偏好**，这是纪律。

| 老 dock | ng | 原因 |
|---|---|---|
| 50+ 个注入回调 | 订阅 `AppContext` + `DataSourceRepository` 信号 | 面板只读元数据，不持有业务状态 |
| m_ctx 里的老 Repository/Service | 数据源只用 `DataSourceRepository` / `DataRepository` | 不引入老类型，物理逻辑都归 ng |

**只读展示骨架的边界**：参数/消息 tab 当前展示真实的**表清单 / 列结构 / 行数 / 首时刻**；
文本键值（`params`/`facts` 的 name→value）的真实值暂留占位——因为 `Dataset::readWindow`
经 C ABI 只返回 `double`，读不出 `PS_VALUE_TEXT` 的字符串。要显示真实文本值需给 Rust
侧加一个文本读取槽（下一轮，不伪造数据糊弄）。数据源卡片的文件加载完成态显示"已加载"
而非"已停止"——加载完成是文件的自然终态，不是人为停止，与老 dock/截图一致。

**不搬的部分**（ng 尚无底层支撑，不做点了没反应的按钮）：live 源连接/暂停、
设备调试、协议检查（后者归 `extensions/<域>/gui/` 的领域面板机制）。派生通道已在
字段树右键菜单实装（走 `data.derive` 只读预览，见 §数据派生）。

**源右键菜单**只放有底层支撑的动作，全部走意图层：`data.reload` / `data.export` /
`data.clearCache`；"设为活动源"是状态切换，直接改 `AppContext`。

#### 判据一：源 id 与数据集 id 是两套空间，不可混用

一个源可扇出多个解析器、产出多个数据集，故 `activeSourceId`（源）与
`activeDatasetId`（数据集）必须分开。曾把 datasetId 写进 `activeSourceId`，
于是 `data.reload` 按源 id 查源表必然落空——**表现为"重载"按钮永远报
"没有可重载的数据源"**，而单元测试因显式传了源 id 一直是绿的，缺陷只在视图路径上。

现在：`DataSourceRepository::sourceOfDataset(datasetId)` 提供反查，
`setActiveChannel` 只改活动数据集、不动活动源。
`test_gui` 用**真实加载路径**（`data.load` 意图）覆盖这条链——
直接往仓库塞数据集是测不到源归属的。

#### 判据二：显示名与定位路径分开存

`DataSourceInfo.name` 是**显示名**（文件名），`origin` 是完整路径。
早前把完整路径塞进 `name`，列表里就是一长串被列宽截断的路径。

#### 判据三：卡片/chip 用 palette 关键字取色，深浅主题自动适配

源卡片与三 chip 的样式用 Qt QSS 的 `palette(button)` / `palette(highlight)` /
`palette(alternate-base)` 等关键字取色，深浅两套主题自动跟随、不写死十六进制色值
（根 `AGENTS.md` §12.5）。状态语义由卡片文字如实呈现（运行中/已加载/错误…）。
`ThemeManager::activeTheme()` / `themeColor()` 静态入口仍供其它需要按 token 动态取色的
视图使用。

#### 判据四：拖拽画图——画布只解码，取数仍走意图层

字段树的"列"节点可拖到画布（`ChannelMime` 线协议：`datasetId|table|column` 逻辑标识，
不传裸指针）。两端职责严格切开：

- **数据树**只 `encode`，**画布**只 `decode`——两者唯一共享的是无依赖小头 `ChannelMime.h`，
  互不 include（星形解耦，不是网状）。
- `PlotCanvas::dropEvent` 解码合法 MIME 后**只发 `channelDropped` 信号**，不认识意图层；
  `ChannelView` 订阅后调 `setActiveChannel` → `activeChannelChanged` → `showChannel`
  → `plot.channel` 意图取数。**与双击同一条单一路径**，所以拖拽/双击/脚本三条入口
  行为必然一致，且可录制回放。
- 之所以用"更新活动通道"而非直接 `showChannel`：避免 `setActiveChannel` 发出的
  `activeChannelChanged` 再触发一次渲染（同一拖拽画两遍）。单一数据变更源，
  多个订阅者各自响应——这是 `AppContext` 共享状态的意义。
- 非法 MIME（非通道格式）被拒、不改画布状态；`dropChannel()` 把"解码+派发"与
  Qt 的 QDrag 投递机制解耦，headless 测试可直接验证这条链（真实拖拽由 QDrag 投递同一函数）。

## 7. 视图层：可插拔，而非模式枚举

老代码 `PlottingCanvas` 用一个 `PlotMode` 枚举承载 12 种绘图模式
（TimeSeries/Xy/Scatter/Bar/Histogram/Spectrum/Horizon/Compass/Gauge/Traj3D/GpsMap/Surface3D），
paintEvent 里按模式分支——**加一种模式就要改这个 3800 行的类**。

ng 的做法：

- **plot 器官**提供 View 宿主机制：画布、页面管理、LOD 取数、交互框架
  （缩放/平移/框选/游标/图例/书签/联动光标）；
- **扩展**提供具体 View 类型：每种一个独立类，注册到视图注册表；
- 加新视图 = 新增一个类 + 注册，**不改任何既有视图、不改 plot 器官**。

老代码的 12 种模式是**首批迁移目标**：实现时参考其绘制与交互代码，
但按新边界拆成独立类。

## 8. 分层与依赖

```
app/         入口：CLI（纯 Core）· GUI（+ Widgets）· 脚本
  ↓
gui/         GUI（仅 GUI 进程）
  shell/       外壳：MainWindow · ShellBinder · GuiContext · 部件工厂
  views/        通用绘图能力库（将来独立成 Qwt-like 库；自注册到部件工厂）
  ↓
recording/   录制子系统（app 内置机制）：raw/mcap 编解码实现
  ↓
host/        装载循环 · 意图执行器 · 注册表（器官表 / 包表 / 解析器表）
  ├── modules/     器官（机制）：source / dataset / plot / analysis / report / script
  ├── extensions/  知识包：common / autopilot / automotive / robotics / gnss
  └── parsers/     解析器 SPI 与注册表
  ↓
core/        Dataset 门面 · 仓库 · 意图 · 插槽表 · 贡献抽象（无 QWidget）
             ↑ B2 起 Dataset/DataRepository 已迁至 libs/dataset/，core 仍 PUBLIC 链之；
               此处保留历史职责描述，终态 core 消亡见顶部指针
  ↓
kernel       Rust：typed 表 · LOD · 缓存 · 算子（只经 ps_kernel.h）
```

依赖严格向下。**core 无 Qt Widgets、无第三方 GUI 库**——headless 纯净的保证，
也是 CLI 与 GUI 行为一致的前提。

> 上述分层是机制视角。各目录的**定位、放什么/禁放什么、可依赖/禁依赖**以 [`LAYERS.md`](LAYERS.md) 为准——新增代码先在那里判归属。跨层依赖由 `cmake/LayerContractGuard.cmake` 在 configure 期强制。B7 后 core 目录删除，届时以 LAYERS.md 终态为准。

### 8.1 机制资产清单（uORB 式：先备齐，业务直接用）

**架构裁决**：libs 里的中间件是**预先准备好的机制资产**——业务 libs/modules/extensions
依赖它们，就像 PX4 的 uORB 把所有消息集中在 uORB 定义好、节点直接用。
**写代码前先查本清单；清单里没有的机制，先在清单登记、在对应 lib 实现，
再开始用——禁止在业务代码里临时自造一套**（再出现一个手写 QHash 注册容器
即违规）。

| 中间件（libs/） | 预备好的机制 | 业务何时直接用 / 禁止什么 |
|---|---|---|
| `foundation` | `Result<T>`（统一错误传播）、`Events`（事件主题集中定义） | 一切错误返回与跨层通知；禁止自造错误码体系、禁止字符串主题散落 |
| `registry` | `Registry<T,Key>`（唯一容器机制，保序）、`StaticRegistry`（启动期自注册）、`Contribution.h`（贡献契约：基类 + 6 类声明式贡献 + ToolPack）、`ExtensionRegistry`（包/贡献容器） | 任何"按 id 登记与查询"；运行态注册表一律委托泛型表；禁止手写 QHash 注册容器、禁止为声明式资产新建类 |
| `intent` | `Intent`（统一意图）、`IIntentExecutor`（统一执行契约）、`JobRecorder`（意图录制回放） | 一切功能入口与跨器官调用；禁止器官间直调、禁止旁路意图管道 |
| `parser` | `IMessageParser`（怎么读）、`ParserRegistry` | 新格式 = 新解析器贡献；禁止在 handler 里 if-else 认后缀 |
| `dataset` | `Dataset`（kernel 门面）、`DatasetRegistry`/`SourceRegistry`（运行态容器，委托泛型表） | 一切数据进出 kernel 的路径；禁止绕过门面直捅 Rust FFI |
| `context` | `AppContext`（会话上下文）、`IngestPipeline`（入库）、`DerivedSeriesRegistry`、`ChannelTransformConfig` | 派生/变换机制复用；禁止业务层私建派生账本 |
| `uischema` | 变换契约（能力位 + 自描述）、Form/Property/Marker schema | 新算子先登记能力位；禁止无自描述的算子 |

新增机制的流程（三步，缺一不可）：
1. 本清单加一行（机制名 + 一句话能力 + 使用纪律）；
2. 在对应 lib 实现并配契约测试；
3. `LAYERS.md` 同步归属。

## 9. 统一意图管道

所有前端的所有请求，最终都是一个 `Intent{op, params}`，交给同一个执行器。
GUI 点击、CLI 命令、脚本调用**走同一条路**，因此可录制、可回放、行为一致。

关键 op（按领域划分，器官各自认领）：

```
source.open / source.close / source.list / source.record / source.replay
dataset.list / dataset.schema / dataset.query
view.create / view.render / view.interact
analysis.run / analysis.result
report.generate
script.run
```

`IntentResult` 必须可序列化 → GUI 操作能录成 Job、脚本能回放同一序列。

#### data.* 系统级意图：归口宿主，只搬有底层支撑的能力

数据加载与派生操作（`data.load` / `data.reload` / `data.export` / `data.recent` /
`data.clearCache`）都是**宿主级 handler**，不归属可卸载模块——因为它们要访问
宿主持有的 `AppContext`（repo / sources / settings / pipeline）。所有 data 意图
集中在宿主一处，模块装卸不影响，也不建第二条路径。

参考老代码 `datasource/DataSourceCommands` 补齐能力时，遵守一条铁律：
**只实现底层有 API 支撑的能力，不造点了没反应的空按钮。**

| 老代码命令 | ng 归属 | 底层支撑 |
|---|---|---|
| `file.open` / `data.quickOpen` | `data.load`（已有）+ 骨架 `app.action.openData` | 格式驱动 + 摄取管道 |
| `data.reload` | `data.reload`（宿主） | `sources` + `data.load`，**稳定 id 保持数据集不换 id** |
| `data.exportSnapshot` | `data.export`（宿主） | `Dataset::channel` 有界取数写 CSV |
| `data.recent` | `data.recent`（宿主）+ `data.load` 成功自动记入 | `SettingsStore` 持久化 |
| `data.clearCache` | `data.clearCache`（宿主） | `.psc` 缓存路径约定 |
| `layout.save/import` | 已有 `plot.layout.save/load` | — |
| `protocol.inspect` | 领域面板 `extensions/<域>/gui/`（非 data 意图） | — |

`DataModule.onAttach` 只把这些能力声明成 ribbon"数据"组的 action（纯数据，
绑真实 intentOp），不自己实现、不碰 Widgets——headless/CLI 同样能执行这些意图。

## 10. 与老代码的关系

| 维度 | 关系 |
|---|---|
| **领域知识与协议** | 直接继承（各格式解析算法、指标定义） |
| **实现代码** | 填代码时参考（`UlogParser` 怎么解段、`PlottingCanvas` 怎么画、ADS/SARibbon 怎么用） |
| **类清单与分层** | **不照抄**。老代码的分层是演进产物，ng 按本文重新划分 |
| **稳定契约** | 复用 `ps_extension_api.h` 的 `PsFrameBatch` / `PsHostApi` 布局（C ABI） |

明确不照抄的五处：

| 老代码 | ng 的做法 | 理由 |
|---|---|---|
| shell 层 9 个 Manager（2973 行） | 1 个 `ShellBinder` | Manager 满天飞是演进产物 |
| loader 直接产 Dataset | Source → Parser → Dataset 三段分离 | 支持多源并存与字节级录制回放 |
| `recordable` 挂在各 loader | 录制在 Source 层 | 一次实现覆盖所有格式 |
| 12 模式合一 canvas | 视图注册表，每种独立类 | 加视图不改既有代码 |
| `IMessageParser`（C++ 虚函数） | C++ 接口类（内置）+ C ABI 镜像 vtable（插件） | 内置协议要"继承接口自述元数据"，C++ 自然形态；外部插件仍走 C ABI，虚函数跨不了 dll 的问题由镜像 vtable 解决，两者汇入同一注册表 |
| 贡献里嵌 `QWidget*` | 纯数据插槽 + 部件工厂 | headless 可用 |

## 11. 落地顺序（进度）

| # | 事项 | 依赖 | 状态 |
|---|---|---|---|
| 1 | **DataSource 抽象**：多源并存 / 通道绑定 | — | ✅ 已建 |
| 2 | **Parser SPI**：`ps_parser_spi.h` + `ParserRegistry` | — | ✅ 已建（含 pspx 样板） |
| 3 | **录制格式注册表**：raw / mcap | — | ✅ 已建 |
| 4 | **录制后端注册表**：file / udp | 3 | ✅ 已建 |
| 5 | **录制正交组合**：`Recorder`（格式 × 后端）+ 多路同时录 | 3,4 | ✅ 已建 |
| 6 | **AppContext**：进程级运行态唯一持有者 | — | ✅ 已建 |
| 7 | **摄取管道**：`IngestPipeline`（单前端 → 多解析器扇出 → 多数据集） | 1,2 | ✅ 已建 |
| 8 | **统一设置**：`SettingsStore` 三作用域 + 变更通知 + 原子落盘 | — | ✅ 已建 |
| 8b | **工作区数据源配置**：`WorkspaceDataConfig` 原子申请单（来源/解析器/录制/平移/派生配方）存 `ws/data/sources`；`data.load` 自动记录，`data.workspace.restore` 重放恢复 | 8 | ✅ 已建（配方存取闭环；解析器参数/录制参数的**重放生效**待 data.load 接对应参数） |
| 9 | **GUI 两层化**：shell/ + views/<功能>/，视图自注册 | 6 | ✅ 已建 |
| 10 | 宿主喂数据流程接通 `DataSource`（读文件/流 → 管道 → 落库） | 7 | ✅ 已做（`IByteSource` + `IngestOrchestrator`；ulog / csv 均走管道，格式解析各只有一份实现；表名/缓存/stable id 三项宿主策略已归位） |
| 11 | 器官补齐：`mod.source` / `mod.dataset` / `mod.plot` / `mod.analysis` / `mod.script` + `mod.protocol` / `mod.settings` | 7 | ✅ 已建（source/dataset **拆开**，v1 合并判为错；report 暂不做成品只备扩展点；core 瘦身配套：dataset 已下沉独立库） |
| 12 | 视图注册表 + 首批 View（从老代码 PlottingCanvas 拆） | 11 | ✅ 已建（plot-canvas / dataset-tree 自注册；PlotCanvas 退化为 fastplot 宿主） |
| 13 | 外部插件扫描：dlopen + ABI 校验（解析器 + 录制格式 + 后端） | 2,3,4 | ⬜ 待做 |
| 14 | 其余格式按 SPI 平移进对应知识包 | 2,10 | 🟡 进行中（ulog / csv / custom-protocol 已完成；其余格式 mavlink/nmea/oem7/asc/blf/mdf4 待平移） |
| 15 | **plotting 会话层重写**：多曲线叠加 / 图例 / 拖拽追加 / 缩放重取 LOD / 导出 / 视图状态 | 12 | ✅ 已建（基于 fastplot 重写，不平移老 7081 行 UI 外壳） |
| 16 | **protocol 逻辑资产平移**：`ng_protocol` 纯逻辑库 + SPI v2 解析器 + 薄协议编辑器 | 2 | ✅ 已建（2407 行纯逻辑平移；不做老 1749 行胖 UI） |

### 11.1 本轮裁决：plotting 与 protocol 两个 module 的搬迁

用户需求是"正常使用 plot 功能"与"protocol 逻辑资产 + 解析器 + 编辑器"。
按总纲"老 `src/` 只作需求/算法参考，类结构与分层不在 ng 复刻"，两项都**不平移老外壳**：

**plotting（会话层重写，不搬老 UI）**
- 老 `modules/plotting` 约 7286 行，其中 7081 行是 `PlottingCanvas` 的 UI 外壳
  （自研命令栈 / 属性页 / 粘性坐标 / 12 模式），真正 core 资产仅约 156 行。
  平移外壳会把老 `CommandManager` / `PropertyManager` / `ModuleContext` 耦合带回 ng，
  故只重写**会话层**能力，绘制内核用 fastplot。
- `PlotCanvas` 从"单条 LOD"升级为 `QVector<PlotSeries>` 多曲线宿主，逐条
  `addCurve(Curve::fromLod(...))`，全局 Y 范围取所有可见曲线的 min/max，
  颜色由 palette 黄金角色相偏移分配。
- `ChannelView` 从"呈现一个通道"改为"叠加 N 条通道"：`addChannel` 按
  `datasetId|table|column` 去重，双击/拖拽字段都走同一追加路径；缩放/平移后按
  可见时间窗与像素宽重新走 `plot.channel` 意图取数。内存铁律不变：
  每条曲线 `(x,min,max)` 三元组数 ≤ 画布像素宽，与数据行数无关。
- **双 Y 轴是用户选择**：每条 `PlotSeries` 有 `rightAxis`，画布右键菜单可勾选
  某曲线挂右轴（view action `plot.series.axis`）；`pushToPlot` 对左右轴**分别**
  用 `setYRange` / `setYRange2` 定界，不再混算（否则右轴曲线被左轴量级压扁）。
  挂轴分配随 `saveViewState` 序列化。
- 新增 view actions `plot.series.clear` / `plot.series.list` / `plot.series.axis` /
  `plot.setType` / `plot.instrument.setKind`，配合已有的
  `plot.export.png` / `plot.export.csv` / `plot.state.save|restore` / `plot.legend.toggle`。
- 页面/拆分/最大化、AB 测量、书签/时标、播放头仍是**增量需求**，不阻塞主线。

### 11.1.1 UI 体验对齐老代码：图库与快捷键（本轮）

用户明确"UI 体验抄老代码、架构不抄"——据此把老 `PlottingRibbon` / `DataSourceRibbon`
的**图库（SARibbonGallery）体验**平移进 ng，但坚持 ng 分层：

- **gallery 是纯数据声明**：`UiAction` 加 `galleryGroup`（组名）/`visual`（缩略图种类）
  两个字符串字段，headless 可读、core 不碰 QWidget。`ShellBinder` 按组把动作收进一个
  `SARibbonGallery`（抄老代码单行模式 + IconOnly + 分组）。某个包卸载，图库项随动作一起消失。
- **plot 类型 gallery**（`ribbon.group.home.plottype`）：抄老 `plotThumb` 的 12 种图型缩略图
  与"常用/曲线/地图3D/仪表"分组。缩略图绘制逻辑平移到 `gui/theme/PlotThumbnails`，
  颜色走 ng 的 `ThemeManager`（**注意 ng token 名与老工程不同**：`panel` 而非 `panelAlt`，
  未知 token 会返回醒目洋红 `#FF00FF`——曾是缩略图发紫的根因）。
  点击走 `plot.setType`：曲线/散点在 PlotCanvas 内切（`Curve::setScatter`）；
  仪表/地图/3D 切到 `plot-instrument` 视图（`plot.instrument.setKind`）。
  数据绑定（把当前通道喂进仪表）属后续增量，gallery 切换本身已可用。
- **parse 类型 gallery**（`ribbon.group.home.sources`）：抄老 DataSourceRibbon"来源"组，
  由格式驱动动态生成图库项（CSV/ULOG/自定义协议…），点选按该驱动后缀弹文件对话框，
  仍走 `data.load` 意图——与通用导入、CLI/脚本同一加载路径。
- **快捷键是动作属性**：`UiAction.shortcut`（如 `Ctrl+O`），`ShellBinder` 建 QAction 时
  `setShortcut`。快捷键 → 同一 action id → 同一意图，GUI/脚本/快捷键语义一致；
  反例：老代码把快捷键散在 `QShortcut`，动作换控件承载就失效。默认值抄老代码
  `file.open=Ctrl+O`、主题切换 `Ctrl+T`。
- 契约测试钉死：12 图型分 4 组、格式驱动进来源库、打开数据 `Ctrl+O`、双轴 round-trip、
  图型切换路由——全部 headless 可读，不依赖起 GUI。

### 11.1.2 UI 顺序对齐老代码：order 显式化（本轮）

老代码靠 `RibbonContribution.order` + panel/items **数组顺序**钉死 UI 排布；ng 若靠绑定先后
（工具箱来源在前、骨架来源在后）则顺序随模块装载顺序漂移。本轮把顺序变成纯数据契约：

- **`UiSlot.order`**：`slotsOfKind(RibbonPage)` 按 (order,label) 排 → 8 页顺序固定；
  `childrenOf(page)` 按 group.order 排 → 同页面板左右顺序固定（抄老 panel 数组顺序）。
- **`UiAction.order`**：`actionsIn(area)` 按 (order,id) 稳定排 → 组内按钮顺序、gallery 分类
  出现顺序都固定。`ShellBinder` 按 `actionsIn` 返回顺序聚合 `galleryOrder`，于是图库分类
  顺序也随 order 可控（gallery 用十位=分类、个位=组内序的全局编号）。
- 意义：顺序与装载/注册顺序彻底解耦；headless 可断言（test_gui 2a-4 段钉死页顺序、
  绘图页 7 组顺序、分组归属、组内升序、gallery 分类顺序），GUI 表现即可复现。
- 反例：靠 `QList::append` 先后决定顺序——插件加载顺序一变，按钮位置就跳；老代码用数组顺序
  规避这点，ng 用 order 字段达到同样效果且不写死在 shell。

**protocol（纯逻辑库 + SPI 解析器 + 薄编辑器）**
- 老 `protocol` 约 5342 行，可搬的是 `ProtocolDocument` / `ProtocolSpec` /
  `SequentialCodec` 约 2407 行纯逻辑；老 1749 行 `ProtocolEditorDialog` 是胖 UI，不搬。
- 新增 `libs/protocol`（`ng_protocol`，只依赖 `Qt6::Core`）：保留段解析 / 表达式 /
  变体 / 校验 / 过滤算法，产出端从老 `PsHostApi` / `psBuildDataset` 改为中立的
  `ProtocolDecodeResult`（channels + 统计），宿主再决定是否落 Dataset。
- 新增 SPI v2 解析器 `CustomProtocolParser`（`extensions/common`）：`configure` 收
  `{ "template": "<psproto JSON>" }`，feed 缓冲字节、finish 一次解码，经
  `PsParserSink::frame_batch` 吐出 `PsFrameBatch`。解析器自身**不读文件**。
- 配置边界：`templatePath` 是配置路径，由 `IFormatDriver::prepareParserOptions()`
  读文件展开为 `template` 文本注入解析器；`DataLoadHandler` 调用该钩子，但
  Workspace 申请单仍记录原始 `templatePath`，保证可重放。
- 新增通用 `ImportDialog` 的 `file` 字段类型（QLineEdit + 浏览按钮），供协议模板选择。
- 新增薄协议编辑器 `ProtocolEditorDialog`：新建 / 打开 / 保存 / 校验 / 试算单帧，
  逻辑全部委托 `ng_protocol`，不复制任何解码规则。
- 新增 `PropertySchema`（`core`）作为属性契约：schema 由提供者声明、属性表只是视图，
  先定契约避免 plotting 属性页与协议编辑器长出两份私有属性面板。

验证：编译全绿（Release + Debug 双构建）；`--kernel` 显示 ABI=2 且自检通过；契约测试覆盖
**typed 多表模型 / 通道投影 / Rust LOD 抽稀·统计·抗混叠抽取 / ps-cache 指纹命中与过期**、
**工具箱自注册 / 六类贡献 / 按包原子装卸 / (kind,domain) 二维索引 / 开放 kind 可扩展**、
**AppContext 单一入口 + 活动视图共享 + 变更可订阅**、
**录制格式×后端正交 / 多路同时录制各写各的 / 未知后端明确报错**、
**摄取单前端→多解析器扇出→各产独立数据集（结果互不覆盖）**、
**视图层自注册（dataset-tree / plot-canvas 均已登记）**、
**多曲线叠加（3 条不同色曲线不互相覆盖 / 同 key 刷新不新增 / 单项移除 / series.list）**、
**protocol 逻辑资产（validate / parseDocument 往返 / 表达式求值 / 顺序帧解码成通道 /
SPI v2 configure+feed+finish 吐 frame_batch / 未配置模板明确失败）**、
意图执行、脚本 headless 执行、录制回放闭环，以及
**9000 行 ULog 流式加载（跨 3 批 / F32+U16 typed 列 / U64 微秒时间轴精确）**；
CLI 九入口实测可用；**CLI 无任何 GUI 依赖**（ldd 验证）；
百万行 ULog：CLI 载入 0.13s / RSS 40MB；GUI 载入 125ms、渲染 6ms、
**100 万行 → 240 像素三元组**（与行数无关）。

## 12. 明确不做

- 不自研 ribbon / docking（用 SARibbon / ADS）；
- 不用 C++ 虚函数做扩展边界（跨不了 dll）；
- 不让解析器直接写存储（它只做转换）；
- 不在 core 引入 QWidget（headless 纯净）；
- 器官里不出现领域词汇（机制与知识严格分离）；
- 不做数据库后端（当前不需要，数据层为此预留）。

## 附录 A.2 工具箱的领域分类（#31/#32 契约）

**工具箱 = 四个业务方向（飞控 / 汽车 / 机器人 / 卫导）+ 通用 + 视频**。

- **领域是分类维度，不是边界**：`Domain` 枚举只有 6 个值（General/Autopilot/
  Automotive/Robotics/Gnss/Video），工具箱按 `(kind, domain)` 二维索引贡献；
  新增领域 = 枚举加一个值 + `extensions/<域>/` 建包，其余代码不动。
- **格式是领域内的能力**：一个格式驱动（IMessageParser）总属于一个领域——
  CSV/自定义协议/Pspx 归通用，ULog 归飞控，NMEA 归卫导；同一领域可以有
  任意多个格式、分析应用、面板、脚本。
- **目录结构即领域结构**：
  `extensions/common|autopilot|automotive|robotics|gnss/`（知识层，纯数据，
  CLI 也链接）+ `extensions/<域>/gui/`（呈现层，QWidget 面板，只 GUI 链接，
  依赖方向呈现层 → core 的 PanelFactoryRegistry，绝不依赖 ng_gui）。
- **CLI 按领域分组展示**：`--list-formats` 按 通用→飞控→汽车→机器人→卫导→视频
  的顺序分节列出驱动；`driversByDomain(domain)` / `domainName(domain)` 是
  查询契约（test_architecture 断言覆盖领域归属与中文域名）。
