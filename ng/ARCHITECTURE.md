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
app/            CLI / GUI 入口（只做装配与参数解析）
  ↓
host/           装载循环、依赖拓扑、意图执行器、生命周期驱动
  ├── modules/      器官：data / plot / script（编译进 app，可配置关闭）
  └── extensions/   工具箱：格式驱动等能力（见下"工具箱"一节）
  ↓
core/           Dataset 的 C++ 薄门面 + DataRepository + 事件 + 意图定义
                + ExtensionRegistry / IFormatDriver（工具箱抽象）
  ↓              （只 include ps_kernel.h 一张 C ABI 门）
kernel (Rust)   数据地基：ps-ndf typed 表 / ps-cache 缓存 / ps-ops 抽稀·滤波·统计
```

依赖方向严格向下。**core 不依赖任何上层；module 之间零直接依赖。**
**数据本体归 Rust kernel 所有，C++ core 只是它的 RAII 门面与 schema 投影。**

**器官（module）与工具箱（extension）是两条正交的线**：
- 器官是 app 的功能组件，有生命周期、可上 UI，由 `ModuleHost` 按依赖拓扑装载/卸载；
- 工具箱是"业务知识包登记处"，按 PlotScope 服务的业务方向组织：**飞控 / 汽车 / 机器人 /
  卫导**四个核心方向，外加通用（CSV/JSON/binary）与视频同步；里面装的是无 UI、
  无独立生命周期的格式驱动，统一注册进 `core::ExtensionRegistry`，由宿主持有；
- 两者都归宿主管，但互不伪装：格式不再是器官，器官也不认识任何具体格式。

**领域是工具箱的分类学**：每个驱动声明 `domain()`，注册表据此分组。目录也按领域组织
（`extensions/{common,autopilot,automotive,robotics,gnss}/`），新增格式落入对应领域目录即可，
不动宿主与器官。具体格式资产见 §3"其他格式（已具备的资产）"。

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

### 4.1 器官（module）—— app 的功能组件

有生命周期、可上 UI 的部分（data / plot / script）实现统一接口：

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

### 4.2 工具箱（ExtensionRegistry）—— 能力登记表

格式解析等"无 UI、无独立生命周期的能力"不伪装成器官，而是注册进工具箱：

```cpp
class ExtensionRegistry {
    bool registerDriver(std::unique_ptr<IFormatDriver>); // 登记（拥有所有权）
    bool unregisterDriver(const QString &id);            // 撤销 + 释放
    IFormatDriver *driverForFormat(const QString &format) const; // 按 format 精确
    IFormatDriver *driverForPath(const QString &path) const;      // 按后缀推断
};
```

`IFormatDriver` 只声明"身份（id/displayName/**domain**）+ 识别规则 + load()"；
解析算法与缓存/入库外壳都在驱动内。领域（`FormatDomain`）是工具箱的一级分类：
`General / Autopilot / Automotive / Robotics / Gnss / Video`，注册表支持
`driversByDomain()` 与 `domains()`，CLI `--list-formats` 按领域分栏展示。
工具箱由宿主持有，经 `ModuleContext` / `App` 暴露；卸载一个驱动 = 从表撤销并释放，
不留悬空能力，也不牵连其他驱动或器官。

### 4.3 两条发现路径，一张注册表

| 路径 | 对象 | 方式 | 本轮状态 |
|---|---|---|---|
| 自注册 | 内置格式（编译进 app） | `registerBuiltinFormats(registry)` 集中登记，app 不认识具体类 | ✅ 已实现 |
| 动态扫描 | 外部插件（第三方动态库） | `scanExternalPlugins(registry, dir)` 扫描→校验 ABI→注册 | ⬜ 入口已预留，未实际 dlopen |

**只有外部插件才走动态扫描**；内置能力一律静态自注册，避免为已知格式付 ABI 代价。
两条路径汇入同一个 `ExtensionRegistry`——宿主与器官对"能力从哪来"完全无感。
插件加载失败（缺符号/ABI 不符/id 冲突）只记入报告，不中断其他插件，更不碰内置格式。

### 4.4 独立装卸隔离

- 器官：见 4.1，所有权即撤销 + 依赖前置。
- 工具箱能力：`unregisterDriver(id)` 精确撤销；`data.load` 由唯一入口按注册表查表分派，
  撤掉 CSV 不影响 ULog，反之亦然；可热插（重新 register 即可恢复）。

## 5. 统一意图层（automation 的灵魂）

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

## 6. 自动化与 headless（已实现）

- 脚本引擎是 core 门面 + 一个 script 模块（Lua 5.4）。脚本只拿到门面 `ps`，永远碰不到 QWidget 和模块内部对象。
- **headless = 装载模块但不调用 `onAttach()`**：没有第二套装配代码。
- GUI 与 CLI 共用同一个装载循环和同一个意图执行器；区别只在是否提供 `UiContext`。
- 脚本门面 API（全部翻译成普通 Intent）：
  - `ps.data.load(path, format, stableId?)` → `data.load`
  - `ps.plot.render(dataset, channel)` → `plot.render`（返回 PlotSpec table）
  - `ps.log(msg)` / `ps.version()`
- **稳定 id**：`data.load` 可携带显式 `DatasetId`，录制/回放用同一个 id，避免会话自增 id 漂移——这是 Job 可回放的前提。
- CLI：`--list-modules`（器官）、`--list-formats`（工具箱格式驱动）、`--info <file>`（按后缀自动选驱动）、`--run <job.json>`、`--script <file.lua> [--record <out.json>]`。
- 录制闭环：`--script demo.lua --record job.json` 把脚本产生的业务意图录成 Job（不含 `script.run` 本身），`--run job.json` 回放得到相同 PlotSpec。
- CI：headless 跑 Job + 导出 PNG/CSV 做基线对比（待办）。

## 7. 我会明确砍掉的复杂度

- `ServiceRegistry` 那种 `type_index→void*` 服务定位器 → 构造函数注入。
- 全局单例 `instance()` → 能力从 Context 注入。
- 遍布全局的命令 id 字符串总线 → QAction 信号 + 意图对象。
- GUI/CLI 两套装配 → 共用装载循环。
- 运行时热重载 UI → 明确"重启生效"。

## 8. 目录结构

```
ng/
├── ARCHITECTURE.md       本文档
├── CMakeLists.txt        独立构建，复用仓库 deps/Qt
├── core/                 数据模型/仓库/事件/意图/脚本门面（无 QWidget）
├── host/                 IModule/装载循环/意图执行器/App
├── modules/              器官：data / plot / script
├── extensions/           工具箱（按业务领域组织）
│   ├── common/           通用：CSV（IFormatDriver 样板）
│   ├── autopilot/        飞控：ULog（UlogStreamParser 流式样板）
│   ├── automotive/       汽车：预留（ASC/BLF/MDF4/CAN）
│   ├── robotics/         机器人：预留（MCAP/ROSbag2）
│   ├── gnss/             卫导：预留（NMEA/UBX/OEM7）
│   └── registry/         集中自注册 + 外部插件扫描入口
├── app/                  CLI 入口
└── tests/                架构契约测试
```

## 9. 落地顺序（进度）

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
8. ⬜ 启用外部插件真实动态加载（定义插件 C ABI + 加载器），并按 ULog 公式把其余格式
   平移进对应领域目录（飞控 apbin/tlog/mavlink、汽车 ASC/BLF/MDF4/CAN、机器人 MCAP/ROSbag2、
   卫导 NMEA/UBX/OEM7）与 GUI 画布。

验证：编译全绿（Release + Debug 双构建）；`--kernel` 显示 ABI=2 且自检通过；契约测试覆盖
typed 多表模型、通道投影、**Rust LOD 抽稀 / 统计 / 抗混叠抽取 / ps-cache 指纹命中与过期**、
**工具箱自注册 / 按 format 与后缀选驱动 / 撤销隔离 / 可逆热插 / 插件扫描入口**、
意图执行、脚本 headless 执行、录制回放闭环，以及
**9000 行 ULog 流式加载（跨 3 批 / F32+U16 typed 列 / U64 微秒时间轴精确 / events+facts 元数据表）**；
CLI 六入口（kernel/list-modules/list-formats/info/run/script）实测可用。
