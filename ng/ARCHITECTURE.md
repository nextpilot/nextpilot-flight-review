# PlotScope NG — 架构总纲

> **本文是架构设计，不是实现说明。**
> 设计输入是你的需求；老代码（`src/`）在**填代码阶段**可作参考（协议算法、画布绘制、
> ADS/SARibbon 用法），但**类清单与分层不照抄**——那是演进产物，不是设计。

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
/* C ABI：任意语言可实现，可放进 .so/.dll */
typedef struct PsParserVtable {
    uint32_t abi_version, struct_size;
    void *(*create)(void);  void (*destroy)(void *ctx);
    int32_t (*sniff)(void *ctx, const uint8_t *data, uint32_t size);
    int32_t (*feed)(void *ctx, const uint8_t *data, uint32_t size,
                    const PsParserSink *sink, PsError *err);
    int32_t (*finish)(void *ctx, const PsParserSink *sink, PsError *err);
    void  (*reset)(void *ctx);
} PsParserVtable;
```

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
   `ModuleHost` 持 settings/toolbox、注册表是全局静态。于是器官取数据走
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
core: AppContext ──▶ host: ModuleHost ──▶ app: App（组合根） ──▶ gui: MainWindow
```

器官取状态只认一个入口 `ctx.app()`。

**活动视图是"指针"不是"副本"**：存的是 id（活动源 id / 活动表 / 活动列），
数据始终在仓库里。这让"用户正在看哪个通道"成为**跨前端共享**的运行态——
GUI 点一下通道，脚本随后 query 得到同一答案。

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

落地是 `core::IngestPipeline`：持有通道表，喂字节时扇出给所有活跃通道；
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

## 6. GUI：外壳 + 功能视图（两层）

**不自研 ribbon / docking**——SARibbon（MIT）与 Qt-ADS（LGPL-2.1）已在仓库内 vendored，
不联网、不引新依赖。ADS 以共享库方式链接（与老仓库一致）。

外壳**不设** `DockManager` / `RibbonManager` 这类中转类：
第三方组件已提供对应能力，再包一层同名类是零价值转发。
ng 只保留**一个绑定器**：

```
core::UiSlotRegistry（纯数据：有哪些页签/分组/面板/动作）
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

#### 视图的物理归属：功能专属聚合，通用能力库与领域面板各归其位

一个功能（器官 + 它的 UI + action + 意图）**物理上放在一个目录**，方便作为
完整单元开发/查找/编辑；靠 CMake 拆成两个 target 实现**编译期分离**：

```
modules/data/                     一个"数据"功能，物理聚合
  DataModule.{h,cpp}          ┐ → ng_module_data     （headless，CLI 装载）
  DataManagerPanel.{h,cpp}    ┘ → ng_module_data_gui （GUI 装载，链 Widgets）
  CMakeLists.txt                  在此把 headless / GUI 两类文件分开
```

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
| **描述**（纯数据） | `core::IPanelContribution` / `PanelSpec` | id / 标题 / 建议区域 / 图标 / 附加数据 | headless 也可见（CLI `--list-contributions panel` 可查） |
| **机制**（纯数据） | `core::PanelFactoryRegistry` | `QHash<id, 工厂>`；`QWidget` 只前置声明 | core，谁都能调 |
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
  ↓
kernel       Rust：typed 表 · LOD · 缓存 · 算子（只经 ps_kernel.h）
```

依赖严格向下。**core 无 Qt Widgets、无第三方 GUI 库**——headless 纯净的保证，
也是 CLI 与 GUI 行为一致的前提。

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
| `IMessageParser`（C++ 虚函数） | C ABI | 虚函数跨不了 dll，违背"不限语言" |
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
| 9 | **GUI 两层化**：shell/ + views/<功能>/，视图自注册 | 6 | ✅ 已建 |
| 10 | 宿主喂数据流程接通 `DataSource`（读文件/流 → 管道 → 落库） | 7 | ✅ 已做（`IByteSource` + `IngestOrchestrator`；ulog / csv 均走管道，格式解析各只有一份实现；表名/缓存/stable id 三项宿主策略已归位） |
| 11 | 器官补齐：`mod.source` / `mod.dataset` / `mod.analysis` / `mod.report` | 7 | ⬜ 待做 |
| 12 | 视图注册表 + 首批 View（从老代码 PlottingCanvas 拆） | 11 | ⬜ 待做 |
| 13 | 外部插件扫描：dlopen + ABI 校验（解析器 + 录制格式 + 后端） | 2,3,4 | ⬜ 待做 |
| 14 | 其余格式按 SPI 平移进对应知识包 | 2,10 | 🟡 进行中（ulog / csv 已完成；其余格式 mavlink/nmea/oem7/asc/blf/mdf4 待平移） |

验证：编译全绿（Release + Debug 双构建）；`--kernel` 显示 ABI=2 且自检通过；契约测试覆盖
**typed 多表模型 / 通道投影 / Rust LOD 抽稀·统计·抗混叠抽取 / ps-cache 指纹命中与过期**、
**工具箱自注册 / 六类贡献 / 按包原子装卸 / (kind,domain) 二维索引 / 开放 kind 可扩展**、
**AppContext 单一入口 + 活动视图共享 + 变更可订阅**、
**录制格式×后端正交 / 多路同时录制各写各的 / 未知后端明确报错**、
**摄取单前端→多解析器扇出→各产独立数据集（结果互不覆盖）**、
**视图层自注册（dataset-tree / plot-canvas 均已登记）**、
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
