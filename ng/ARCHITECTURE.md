# PlotScope NG — 新架构设计

> 这是一套从零设计的新骨架，独立于仓库现有代码（`src/`）。目标不是给旧代码打补丁，
> 而是验证一套更干净的数据绘图分析软件架构：数据模型统一、模块可独立装卸、
> 脚本与 headless 是一等公民。

---

## 1. 本质与总原则

数据绘图分析软件 = **三条主干 + 一个地基**。

- 地基：**统一数据模型**，由 Rust kernel 提供（内存安全 + 列式高性能 + 缓存/抽稀/滤波），C++ 只做薄门面。
- 主干一（数据）：把各种来源变成 `Dataset`。
- 主干二（视图）：把 `Series` 渲染成图、支持交互。
- 主干三（自动化）：把人的操作变成可复现的意图序列。

四条硬原则：

1. **一个模型走天下**：没有第二套数据抽象，没有格式名渗透进绘图代码。
2. **谁创建谁拥有，谁拥有谁撤销**：用 Qt 父子所有权消解"卸载"难题。
3. **通信只有三种**：能力注入、typed 事件、Qt 信号槽（context object 自动断连）。
4. **意图是公共货币**：GUI 点击和脚本调用产出同一种意图，走同一个执行器。

## 2. 分层

```
app/            CLI 入口（仅 Qt Core）/ gui/  GUI 入口与骨架（Qt Widgets）
  ↓             两者共用同一套 app/host/core，行为一致
host/           装载循环、依赖拓扑、意图执行器、生命周期驱动、自注册表
  ├── modules/      器官：app 的组成部分 data / plot / script（编译进同一二进制）
  └── extensions/   工具箱：按领域组织的知识包，含六类贡献（见下"工具箱"一节）
  ↓
core/           Dataset 的 C++ 薄门面 + DataRepository + 事件 + 意图定义
                + 工具箱抽象：IToolPack / IToolContribution（六类）/
                  ExtensionRegistry（按 kind×domain 二维索引）/ UiSlotRegistry
  ↓              （只 include ps_kernel.h 一张 C ABI 门）
kernel (Rust)   数据地基：ps-ndf typed 表 / ps-cache 缓存 / ps-ops 抽稀·滤波·统计
```

依赖方向严格向下。**core 不依赖任何上层；module 之间零直接依赖。**
**数据本体归 Rust kernel 所有，C++ core 只是它的 RAII 门面与 schema 投影。**

**器官（module）与工具箱（extension）是两条正交的线**：
- 器官是 **app 的组成部分**（不是插件）：编译进同一二进制，有生命周期、可上 UI，
  由 `ModuleHost` 按依赖拓扑装载/卸载；彼此**直接用 C++ 接口协作**，不必绕意图层。
  首要目的是**代码组织**——避免 `MainWindow` 变成巨型文件，一类职责一个文件；
- 工具箱是"业务知识包容器"，按 PlotScope 服务的业务方向组织：**飞控 / 汽车 / 机器人 /
  卫导**四个核心方向，外加通用（CSV/JSON/binary）与视频同步；
- 两者都归宿主管，但互不伪装：器官不认识任何领域业务，工具箱不伪装成器官。

**两条线都做到"新增不改既有代码"**：module 与知识包各自静态自注册进全局表
（`NG_REGISTER_MODULE` / `NG_REGISTER_PACK`），宿主遍历装载。见 §4.1.1。

**工具箱是开放贡献点系统，不是格式清单**（两个维度正交）：
- **领域（domain）**是稳定的分类学：`FormatDomain` 枚举——通用/飞控/汽车/机器人/卫导/视频，
  几乎不变；目录也按领域组织（`extensions/{common,autopilot,automotive,robotics,gnss}/`）。
- **贡献类型（kind）**是**开放字符串**（`ContributionKind`，不用枚举）：格式 / UI / 脚本 /
  数据 / 例子 / 文档……以及未来的算子/导出器/协议/主题。用字符串而非枚举，是因为清单会
  随业务"等等一系列"不断生长——新增类型 = 新 typed 接口 + 注册槽，**不改 core 枚举、
  不重编译宿主**，符合开闭原则。
- 每类贡献有 typed 接口（`IFormatDriver`/`IUiContribution`/`IScriptContribution`/
  `IDataContribution`/`IExampleContribution`/`IDocContribution`），共享极小共性
  `IToolContribution`（id/displayName/kind/domain/provider）。
- **知识包（`IToolPack`）是装载/卸载的原子单位**：一个飞控包同时贡献格式+UI+脚本+数据+
  例子+文档，装则全装、卸则全卸，`provider()` 指向所属包，扩展之间天然隔离。
- 注册表 `ExtensionRegistry` 按 `(kind, domain)` 建二维索引；具体格式资产见 §3。

## 3. 统一数据模型（core 门面 + Rust kernel 地基）

数据模型**不是 C++ 另起炉灶实现的**，而是直接复用仓库的 Rust kernel
（`src/application/kernel`：`ps-ndf` typed 表 + `ps-cache` 缓存 + `ps-ops` 算子 +
`ps-abi` C ABI）。这样做的原因：Rust 提供内存安全（panic 不跨 FFI）、列式高性能，
并为缓存、抽稀、滤波、FFT、对齐准备了统一基础设施。C++ 侧只用 `Dataset` 做薄封装。

```cpp
// C++ 侧：Dataset = Rust PsDataset* 的共享 RAII 句柄 + schema 投影。
// 数据本体（typed 列、null 位图、共享时间轴、ring 预算）全在 Rust，不复制到 C++。
class Dataset {
    static Dataset create(name, format);            // ps_dataset_new
    static Dataset adopt(PsDataset*, name, format); // 接管缓存还原的句柄
    PsDataset* nativeHandle() const;

    int addTable(name, QVector<int64_t> timeUs);   // ps_dataset_add_table_typed
    bool addF64Column(table, name, unit, values);  // ps_dataset_add_table_column_typed

    std::optional<ChannelView> channel("table.column"); // ps_dataset_read_window
    QVector<LodTriple> lodMinMax(table, col, t0us, t1us, px); // ps_dataset_lod_minmax
    int decimate(factor, t, v, outT, outV);        // ps_decimate_series
    int applyFilter(id, params, ...);              // ps_filter_apply_series
    std::optional<ColumnStats> stats(v);           // ps_stats_series
    bool saveCache(path, size, mtime);             // ps_cache_save_dataset
    static std::optional<Dataset> loadCache(...);  // ps_cache_load_dataset
};

struct Table {  // 轻量 schema 投影（非真相源），从 native 的 channel 派生
    QString name;
    QVector<int64_t> timeUs;   // UTC 微秒，表级共享
    QVector<Column> columns;   // name/unit/ValueType/rowCount，不含 values
};
```

Rust kernel 侧的真实模型（`ps-ndf/src/typed.rs`，C++ 语义与之对齐）：

- **Dataset = 多张 typed Table 的库**，表名库内唯一。
- **Table = 共享 UTC 微秒 i64 时间轴 + seq + 多列**；一个 topic 一张表。
- **Column = 12 种 `PsValueType` + 独立 null 位图**；列值按类型存储，不强制 f64。
- **ChannelView = `(table,column)` 的 f64 绘图投影**，时间从微秒换算成秒。

关键约束：
- **数据所有权在 Rust**，C++ `Dataset` 只持句柄（`shared_ptr<PsDataset>` + `ps_dataset_free`）。
- **单一真相源**：C++ schema 完全从 native 派生，不在 C++ 侧保存列值副本。
- **数据归 DataRepository 所有**，不归任何模块；模块卸载不删数据。
- **降采样/滤波/统计在 Rust 侧完成**（LOD 铁律），C++ 不持有全量大数据副本。
- **缓存按源文件 size/mtime 指纹**，命中不经解析直接还原 dataset。
- `ng_kernel` INTERFACE target 链接 `libps_abi.a`，所有下游自动继承 C ABI 头文件与库。

### 3.1 零副本门面：百万级数据的内存铁律

这套架构要承受**百万级数据**，内存与速度是硬约束。核心原则只有一句：

> **所有高性能数据处理都在 Rust；C++ 只是门面**（句柄 RAII + C ABI 类型映射 + 意图翻译）。

C++ 不存数据、不解析、不计算、不遍历。由此定死**三条不变量**：

| 不变量 | 含义 | 实现 |
|---|---|---|
| C++ 常驻内存 = O(列数 + 窗口上限)，与行数无关 | schema 只存元数据（行数/首尾时间/列名/类型），不持有时间轴数组或列值 | `Table` 删掉 `QVector<TimeUs>`，只留 `firstTimeUs/lastTimeUs`；`rebuildSchema` O(1) 读首尾 |
| 绘图路径 = O(px)，与行数无关 | 一屏最多几千点；100 万行和 100 行对 UI 同一成本 | `lodMinMax(px)` 返回 px 个三元组；`readWindow(cap)` 有界读，kernel 内等步长抽稀 |
| 所有 O(n) 操作在 Rust | 加载/读取/抽稀/滤波/统计/缓存全委托 kernel | `append_frame` 流式灌入、`read_window`、`ps-ops`、`ps-cache`；C++ 一次调用零遍历 |

**流式加载**：所有格式扩展共用同一范式——解析进固定 4096 行缓冲，满一批 `appendFrame` 灌入 Rust，缓冲复用；所有批次灌完后 `refreshSchema()` **只刷新一次**（kernel 的 `ps_dataset_channel()` 会重建整条 f64 时间轴，O(行数)，不能每批调）。C++ 峰值 = 一批缓冲，与文件大小无关。
- **CSV**：逐行解析，第一列作共享时间轴（秒→微秒）。
- **ULog**（样板）：协议解析算法**逐字复用**旧 extension `UlogParser.cpp`——F 段格式表、A 段订阅展平、D 段字段解码、P/L/C/O 元数据段、U64 微秒时间戳，一个字没重写；**唯一改造是产出端**：旧版把 D 段所有消息攒进 `std::vector<double>` 再一次性建表，ng 版按 topic 分表、每列用类型化缓冲（`primToValueType` 保留 F32/U16/… 原始类型）、满 4096 行即灌入 Rust。时间戳走原生 U64 字节，不经 double，避免 1e15 量级精度损失。

**实测**（100 万行 × 3 列 / 24MB CSV，走真实 `data.load` 路径）：加载 1.79 秒，峰值 RSS **58MB** ≈ Rust 数据本体（约 40MB）+ 运行时（约 15MB）+ C++ 单批缓冲。旧的全量 vector 方案会额外产生约 56MB C++ 副本，峰值 110MB+。契约测试含 20 万行流式加载 + 9000 行 ULog（跨 3 批、typed 列/微秒时间轴/多表/缓存）+ 有界读取 + LOD 断言，全部通过。

**其他格式（已具备的资产）**：旧 `src/extensions/builtin/` 下已有 CSV/JSON/binary、ULog/apbin/tlog/mavlink、ASC/BLF/MDF4、NMEA/UBX/OEM7、MCAP/ROSbag2 等近 6000 行解析实现，统一产出 `PsChannelData` 再交 kernel。它们与 ng 共用**同一个 Rust kernel**（`ng/CMakeLists.txt` 的 `_ng_kernel_dir` 指向 `../src/application/kernel`，C ABI 完全相同）。ULog 样板证明了接入公式：**协议算法照搬、产出端换成流式 `appendFrame`**；其余格式按此公式逐个平移即可，无需重写格式知识。

## 4. 器官、工具箱与扩展发现

### 4.1 器官（module）—— app 的组成部分

**定位澄清（重要）**：module 之所以叫 modules，首要目的是**代码组织**——避免
`MainWindow` 膨胀成巨型文件，让每个功能块是独立编译单元，出问题能立刻定位到文件。
它不是"插件"，不需要动态装载，也不需要跨进程边界。

因此：

- module 编译进 app 同一二进制，**直接用 C++ 接口协作**，不必绕意图层。
  意图层是给 **CLI / 脚本 / 自动化**这条公共通道用的。
- module 依赖 Qt Widgets 会污染 headless 构建，因此 module 只描述界面需求
  （`requestPanel` / `declareSlot` / `addAction`），由 app 骨架的部件工厂提供 QWidget 实现。
- 文件划分原则：**一类职责一个文件**。`MainWindow` 只做装配、零业务；
  ribbon / dock / 画布 / 数据面板各自独立文件（见 §5 GUI 层）。

接口：

```cpp
class IModule : public QObject {
    virtual ModuleInfo info() const = 0;              // id/version/requires/provides
    virtual void initialize(ModuleContext&) = 0;      // 声明能力、订阅事件，不碰 UI
    virtual void onAttach(UiContext&) {}              // 建 UI；headless 时宿主不调用
};
```

- 宿主按 `requires/provides` 拓扑排序装载；缺依赖/成环的器官被跳过，不连坐。
- 卸载 = `delete module` → Qt 父子树回收全部 UI 与信号槽（Qt 原生语义，无 token、无撤销协议）。
- `--list-modules` 只列器官；格式驱动**不再是器官**。

### 4.1.1 零改动新增：编译期自注册

需求是"增加一个 module 不要修改其他代码"。若宿主靠 `App.cpp` 逐个
`registerModule(XxxModule().info(), factory)`，每加一个都要改 `App.cpp`——正是要消灭的耦合。

改为**静态自注册**：模块在自己的 `.cpp` 末尾用宏把自己登记进全局表，宿主启动时遍历装载。

```cpp
//  模块 TU 末尾一行：
NG_REGISTER_MODULE(DataModule)     // 需要 DataModule::moduleInfo() / ::create()
//  知识包 TU 末尾一行：
NG_REGISTER_PACK(::ng::ext::AutopilotPack)   // 需要 packId() / build()
```

于是"新增一个 module"= 新增一个目录 + 一行 CMake；"新增一个扩展"同理。
宿主、App、注册表、既有模块与扩展**一行都不用改**。

> **工程要点**：承载自注册的 target 必须是 **OBJECT 库**（不是 STATIC）。
> 静态库中未被引用的目标文件会被链接器丢弃，其静态初始化不执行，注册会静默失效。

契约测试里内置了一个"探针模块 + 探针包"——它们**只存在于测试文件**，
却被自动装载并可执行。这条测试就是"零改动新增"的回归防线：
哪天机制退化了（必须改 `App.cpp` 才能加模块），测试就会失败。

### 4.2 工具箱（ExtensionRegistry）—— 开放贡献点系统

工具箱不是"格式清单"，而是**按业务方向组织的知识包容器**。两个正交维度：稳定的领域枚举
`FormatDomain` + 开放的贡献类型字符串 `ContributionKind`（不封枚举，清单可持续生长）。
核心抽象：

```cpp
class IToolContribution {                 // 所有贡献的极小共性
    QString id(); QString displayName();
    QString kind();                       // 开放：format/ui/script/data/example/doc/...
    FormatDomain domain();                // 稳定：通用/飞控/汽车/机器人/卫导/视频
    QString provider();                   // 所属知识包 id（按包装卸的依据）
};
class IToolPack {                         // 知识包 = 装载/卸载的原子单位
    QString id(); FormatDomain domain();
    const vector<unique_ptr<IToolContribution>>& contributions(); // 可含任意 kind
};
class ExtensionRegistry {
    bool registerPack(unique_ptr<IToolPack>);   // 整包装入 + 按 (kind,domain) 建索引
    bool unregisterPack(const QString&);        // 整包原子撤销
    QList<IToolContribution*> contributions(const QString &kind, FormatDomain domain) const;
    // 格式快捷 API（内部包装成隐式包），保持 data.load 零改动：
    IFormatDriver *driverForFormat(const QString&) const;
    IFormatDriver *driverForPath(const QString&) const;
};
```

**六类 typed 贡献**（共享 IToolContribution，各带自己的领域数据，均为纯数据、headless 可用）：

| kind | 接口 | 承载内容 |
|---|---|---|
| `format` | `IFormatDriver` | 文件 → Dataset（流式解析，见 §3） |
| `ui` | `IUiContribution` | 菜单/面板/dock/ribbon/画布工具入口（`UiAction` 描述，非 QWidget） |
| `script` | `IScriptContribution` | 可复用脚本（语言 + 正文 + 导出符号） |
| `data` | `IDataContribution` | 样例数据/预置数据集资产 |
| `example` | `IExampleContribution` | 示例工程/demo 入口 |
| `doc` | `IDocContribution` | 规格/知识文档条目 |

新增一类 = 新 typed 接口 + 注册表按 kind 自动索引（kind 是字符串，无需改枚举/宿主）。
当前内置 5 个包：通用（格式+文档）、飞控（**六类齐全**）、汽车/机器人/卫导（UI+脚本/文档，
格式位预留）。`--list-packs` 列知识包，`--list-contributions [kind]` 按 kind×领域列贡献。

**UI 插槽解耦**（`UiSlotRegistry`）：器官/骨架只声明"容器"插槽（`menu.plot`/`dock.left`/
`canvas.tool`…），工具箱的 `ui` 贡献按 `area` 填内容；`app` 骨架与器官代码不知道任何领域
业务，业务 UI 随知识包装卸。插槽与动作都是**纯数据**，headless 也能完整查询
（真实 QWidget 由未来 GUI 层读取后创建）。`--list-slots` 展示插槽填充情况。

### 4.3 两条发现路径，一张注册表

| 路径 | 对象 | 方式 | 本轮状态 |
|---|---|---|---|
| 自注册 | 内置知识包（编译进 app） | `registerBuiltinPacks(registry)` 集中装配，app 不认识具体贡献 | ✅ 已实现 |
| 动态扫描 | 外部插件（第三方动态库） | `scanExternalPlugins(registry, dir)` 扫描→校验 ABI→注册整包 | ⬜ 入口已预留，未实际 dlopen |

**只有外部插件才走动态扫描**；内置能力一律静态自注册，避免为已知包付 ABI 代价。
两条路径汇入同一个 `ExtensionRegistry`——宿主与器官对"能力从哪来"完全无感。
插件加载失败（缺符号/ABI 不符/id 冲突）只记入报告，不中断其他插件，更不碰内置包。

### 4.4 独立装卸隔离

- 器官：见 4.1，所有权即撤销 + 依赖前置。
- 工具箱：`unregisterPack(id)` 按包原子撤销（连带该包全部 kind 贡献，无悬空、不牵连他包）；
  `unregisterDriver(id)` 兼容旧语义（撤销驱动所在包）。`data.load` 由唯一系统级入口
  `DataLoadHandler` 按注册表查表分派，任何包装卸都不影响它；撤掉一个包不影响其他包，
  可热插（重新 register 即恢复）。

## 5. GUI 层：ribbon / docking / ps 数据呈现

### 5.1 文件划分原则（回应"别让 MainWindow 变大"）

GUI 层每个部件一个独立文件、一个独立类，职责互不越界。**`MainWindow` 只做装配，零业务**：

| 文件 | 职责 | 不做什么 |
|---|---|---|
| `MainWindow.{h,cpp}` | 建骨架、声明插槽、装器官 | 不画图、不读数据、不含任何业务分支 |
| `RibbonBar.{h,cpp}` | 按插槽数据渲染 ribbon（页签/分组/按钮） | 不知道动作的业务含义 |
| `DockManager.{h,cpp}` | 四边停靠的创建/归位/显隐 | 不管面板内容 |
| `PlotCanvas.{h,cpp}` | 用 QPainter 画 LOD 三元组 | 不加载数据、不抽稀 |
| `DataManagerPanel.{h,cpp}` | 数据集→表→列 树，选列发信号 | 不取数、不绘图 |
| `GuiContext.{h,cpp}` | 实现 `host::UiContext`，把插槽动作路由到上面 | 不决定动作做什么 |
| `PanelFactoryRegistry.{h,cpp}` | 部件类型 → QWidget 实现的工厂表 | 不认识具体部件 |

出问题时的定位路径：`MainWindow` 只看装配；画布问题看 `PlotCanvas`；
数据列表问题看 `DataManagerPanel`；按钮不显示看 `RibbonBar` + 插槽登记表。

### 5.2 ribbon 与 docking：插槽驱动，不硬编码

app 骨架在 `ModuleHost` 构造时声明标准插槽（纯数据，headless 也存在）：

```
ribbon.page.home          RibbonPage    ribbon 的页签
  ribbon.group.home.data  RibbonGroup   页签内的分组
  ribbon.group.home.view  RibbonGroup
dock.left / dock.right    DockPanel     四边停靠
canvas.center             Canvas        中央画布
menu.plot                 Menu
```

插槽从"一个字符串"升级为**带语义的结构**（`SlotKind` + `parentArea` + `order`），
于是 GUI 层能自动布局，贡献方只需说明"我要什么"：

- 工具箱的 `IUiContribution` 按 area 填内容（业务包自带入口）；
- 器官在 `onAttach` 里 `declareSlot` / `addAction` / `requestPanel`（app 自身功能）。

**ribbon 自研**：Qt 无原生 ribbon。`RibbonBar` 用 QPainter + `QToolButton`
实现"页签行 + 页面栈 + 分组容器（大按钮 + 小按钮列）"，无第三方依赖。

**docking 用原生**：`QDockWidget` + `DockManager`，用户可自由拖拽/浮动/隐藏。

### 5.3 ps 数据 UI 呈现：与行数解耦

数据面板双击某列 → GUI 发 `plot.channel` 意图 → plot 模块返回**像素级三元组**：

```cpp
// plot.channel -> { x[], min[], max[], t0, t1, points, title }
// 三元组个数恒定 <= px；抽稀（min/max 分桶）在 Rust kernel 内完成。
```

画布据此绘制，**内存与耗时都是 O(像素宽)，与数据行数无关**。
这条路径与 CLI / 脚本完全同源（都走意图层），因此可录制、可回放、行为一致。

### 5.4 CLI 与 GUI 分离，互不污染

| 可执行文件 | 链接 | 用途 |
|---|---|---|
| `plotscope-ng` | 仅 `Qt6::Core` | CLI / headless / CI / 服务器批处理 |
| `plotscope-ng-gui` | `+ Qt6::Widgets` | 图形界面 |
| `plotscope-ng-gui-bench` | `+ Qt6::Widgets` | GUI 大数据量手动基准 |

`ldd` 可验证 CLI 不含 `libQt6Widgets` / `libQt6Gui`。
两个可执行文件共用同一套 `app/host/core`，保证行为一致。

## 6. 统一意图层（automation 的灵魂）

> GUI 的每次交互和脚本的每行代码，最终都变成同一种意图对象，交给同一个执行器。

```cpp
struct Intent {
    QString op;             // "data.load" / "plot.addSeries" / "plot.render" ...
    QVariantMap params;
};
```

- GUI 按钮点击 → 生成 Intent → 执行器。
- 脚本 `ps.plot.add(...)` → 生成同样的 Intent → 执行器。
- **意图可序列化**：一串 Intent 就是 Job（JSON），可录制、可回放、可版本管理。

执行器是唯一知道"如何执行意图"的地方。它把意图分派给提供对应能力的处理器。

**按完整意图路由，而非只按 op**：`IIntentHandler::canHandle(const Intent&)` 接收整个意图，
多个处理器可声明同一个 op，按意图参数精确认领，互不抢占。

**data.load 只有唯一入口**（宿主系统级 `DataLoadHandler`，不归属任何可卸载模块）：
它查工具箱注册表——有 `format` 就 `driverForFormat` 精确选驱动，否则按文件后缀
`driverForPath` 推断——再委托驱动的 `load()`。因此格式驱动之间彻底解耦：
新增格式只加一行自注册；卸载 = 从注册表撤销；器官与其他模块零改动。
这正是"格式只是工具箱一部分、可注册可扫描"的落地形态。

## 7. 自动化与 headless（已实现）

- 脚本引擎是 core 门面 + 一个 script 模块（Lua 5.4）。脚本只拿到门面 `ps`，永远碰不到 QWidget 和模块内部对象。
- **headless = 装载模块但不调用 `onAttach()`**：没有第二套装配代码。
- GUI 与 CLI 共用同一个装载循环和同一个意图执行器；区别只在是否提供 `UiContext`。
- 脚本门面 API（全部翻译成普通 Intent）：
  - `ps.data.load(path, format, stableId?)` → `data.load`
  - `ps.plot.render(dataset, channel)` → `plot.render`（返回 PlotSpec table）
  - `ps.log(msg)` / `ps.version()`
- **稳定 id**：`data.load` 可携带显式 `DatasetId`，录制/回放用同一个 id，避免会话自增 id 漂移——这是 Job 可回放的前提。
- CLI：`--list-modules`（器官）、`--list-packs`（知识包）、`--list-contributions [kind]`（按 kind×领域列贡献）、`--list-slots`（UI 插槽填充）、`--list-formats`（格式驱动别名）、`--info <file>`（按后缀自动选驱动）、`--run <job.json>`、`--script <file.lua> [--record <out.json>]`。
- 录制闭环：`--script demo.lua --record job.json` 把脚本产生的业务意图录成 Job（不含 `script.run` 本身），`--run job.json` 回放得到相同 PlotSpec。
- CI：headless 跑 Job + 导出 PNG/CSV 做基线对比（待办）。

## 8. 我会明确砍掉的复杂度

- `ServiceRegistry` 那种 `type_index→void*` 服务定位器 → 构造函数注入。
- 全局单例 `instance()` → 能力从 Context 注入。
- 遍布全局的命令 id 字符串总线 → QAction 信号 + 意图对象。
- GUI/CLI 两套装配 → 共用装载循环。
- 运行时热重载 UI → 明确"重启生效"。

## 9. 目录结构

```
ng/
├── ARCHITECTURE.md       本文档
├── CMakeLists.txt        独立构建，复用仓库 deps/Qt
├── core/                 数据模型/仓库/事件/意图/脚本门面/插槽表/贡献抽象（无 QWidget）
├── host/                 IModule/装载循环/意图执行器/自注册表
├── modules/              器官（app 的组成部分）：data / plot / script
├── extensions/           工具箱（领域 × 开放贡献类型）
│   ├── common/           通用包：CSV 格式 + 文档
│   ├── autopilot/        飞控包：格式+UI+脚本+数据+例子+文档（六类齐全样板）
│   ├── automotive/       汽车包：UI+文档占位（ASC/BLF/MDF4/CAN 待接入）
│   ├── robotics/         机器人包：UI+文档占位（MCAP/ROSbag2 待接入）
│   ├── gnss/             卫导包：脚本+文档占位（NMEA/UBX/OEM7 待接入）
│   └── registry/         外部插件扫描入口 + 工具箱装载入口
├── gui/                  GUI 层：一类职责一个文件（MainWindow 只做装配）
│   ├── MainWindow        窗口骨架与装配（零业务）
│   ├── RibbonBar         ribbon 控件（页签/分组/按钮，自研）
│   ├── DockManager       四边停靠管理
│   ├── PlotCanvas        中央画布（QPainter 绘 LOD 三元组）
│   ├── DataManagerPanel  数据集→表→列 树
│   ├── GuiContext        host::UiContext 的 GUI 实现
│   └── PanelFactoryRegistry  部件类型→QWidget 工厂
├── app/                  CLI 入口（仅 Qt Core，不链接 Widgets）
└── tests/                契约测试：架构（headless）+ GUI（offscreen）
```

## 10. 落地顺序（进度）

0. ✅ 接入 Rust kernel 作为数据地基：`ng_kernel` 链接 `libps_abi.a`，Dataset 改为
   `PsDataset*` 的 RAII 门面；typed 构造 / read_window / LOD / 缓存 / 算子全走 C ABI。
1. ✅ core：Dataset 门面 + schema 投影 + DataRepository + 意图定义。
2. ✅ host：装载循环 + 依赖排序 + 执行器。
3. ✅ 一条竖切：CSV 扩展（缓存优先）+ 数据模块 + plot 意图（含 Rust LOD），端到端跑通。
4. ✅ ULog 格式样板：协议算法复用旧 extension，产出端改流式 `appendFrame`；typed 多表 /
   微秒时间轴 / 缓存 / 卸载隔离全验证。同时把意图路由从「只按 op」修正为「按完整 Intent」，
   使多格式扩展可共存互不抢占。
5. ✅ 脚本引擎与 Job 录制：Lua 门面 `ps` + 稳定 id + `--script/--record` 录制回放闭环。
6. ✅ 器官与工具箱分离：格式从 IModule 抽成 `IFormatDriver`，集中自注册进 `ExtensionRegistry`；
   `data.load` 收敛为宿主唯一入口按注册表查表分派。外部插件动态扫描入口预留（仅扫描框架，
   未实际 dlopen）。契约测试覆盖注册表按 format/后缀选驱动、撤销隔离、可逆热插、插件扫描入口。
7. ✅ 工具箱按业务领域组织：引入 `FormatDomain`（通用/飞控/汽车/机器人/卫导/视频），
   驱动声明领域、注册表按领域分组，目录按领域重组；CSV=通用、ULog=飞控。
8. ✅ 工具箱泛化为**开放贡献点系统**：`IToolContribution` 共性 + 开放 kind 字符串（不封枚举）
   + 六类 typed 接口（format/ui/script/data/example/doc）+ `IToolPack` 知识包（装载/卸载原子单位）
   + `ExtensionRegistry` 按 (kind,domain) 二维索引 + `UiSlotRegistry`（器官声明插槽、工具箱填内容）。
   内置 5 个知识包，飞控包六类齐全；新增贡献类型/领域内容不改宿主与器官。契约测试覆盖六类
   贡献、按包原子装卸、二维索引、UI 插槽填充、开放 kind 可扩展性。
9. ✅ 零改动新增（编译期自注册）：`BuiltinModuleRegistry` / `BuiltinPackRegistry` +
   `NG_REGISTER_MODULE` / `NG_REGISTER_PACK` 宏；App 与宿主不再硬编码任何具体类；
   五个知识包各自独立文件静态自注册；承载自注册的 target 改 OBJECT 库（静态库会丢 TU）。
   契约测试内置"探针模块 + 探针包"证明零改动。
10. ✅ GUI 层：ribbon（自研）+ docking（QDockWidget）+ ps 数据呈现；
    `ng/gui/` 一类职责一个文件、`MainWindow` 只做装配；插槽升级为带 `SlotKind` 的结构；
    `PanelFactoryRegistry` 让器官只描述部件类型、不依赖 Widgets；
    `plot.channel` 意图返回像素级三元组；CLI 与 GUI 分离为两个可执行文件。
11. ⬜ 启用外部插件真实动态加载（定义插件 C ABI + 加载器），并按 ULog 公式把其余格式
   平移进对应领域包（飞控 apbin/tlog/mavlink、汽车 ASC/BLF/MDF4/CAN、机器人 MCAP/ROSbag2、
   卫导 NMEA/UBX/OEM7），以及真正消费全部 UI 贡献类型（绘图/分析/报告）。

验证：编译全绿（Release + Debug 双构建）；`--kernel` 显示 ABI=2 且自检通过；契约测试覆盖
typed 多表模型、通道投影、**Rust LOD 抽稀 / 统计 / 抗混叠抽取 / ps-cache 指纹命中与过期**、
**工具箱自注册 / 按 format 与后缀选驱动 / 撤销隔离 / 可逆热插 / 插件扫描入口 /
六类贡献 / 按包原子装卸 / (kind,domain) 二维索引 / UI 插槽填充 / 开放 kind 可扩展**、
**零改动新增（探针模块/包自动装载且可执行）**、
意图执行、脚本 headless 执行、录制回放闭环，以及
**9000 行 ULog 流式加载（跨 3 批 / F32+U16 typed 列 / U64 微秒时间轴精确 / events+facts 元数据表）**；
**GUI 契约测试（offscreen）覆盖 ribbon 渲染 / 部件工厂 / 停靠区 / 数据集树 /
plot.channel 有界抽稀（5000 行 -> 200 点）/ 画布渲染 / 意图错误语义**；
CLI 九入口（kernel/list-modules/list-packs/list-contributions/list-slots/list-formats/info/run/script）
与 GUI 入口实测可用；`ldd` 验证 CLI 不含 Widgets 依赖。

实测：百万行 ULog（18MB）CLI 加载 0.12s / 峰值 RSS 40MB；
GUI 侧载入 134ms、渲染 7ms，**100 万行 -> 240 个像素三元组**（内存与耗时与行数无关）。
