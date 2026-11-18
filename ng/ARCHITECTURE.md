# ng 架构总纲

> 本文是 ng 新骨架的**架构总纲**。
> 定位：**我是架构师，这里是我的设计决策与理由**。
> 老代码（`src/`）在实现阶段可作参考——但**类清单与分层不照抄**，那是老代码的历史包袱，不是设计。

---

## 1. 这个 App 是什么

PlotScope 是**一条数据流水线**，不是一堆并列的功能：

```
字节流 ──解析──▶ 结构化数据 ──落库──▶ Rust 数据本体
                                              │
                            ┌─────────────────┼─────────────────┐
                            ▼                 ▼                 ▼
                          绘图              分析              报告
                            └─────────────────┴─────────────────┘
                                              │
                                    GUI / CLI / 脚本
                                  （同一管道的三个入口）
```

**推论**：GUI、CLI、脚本是**平级前端**，共用同一条意图管道与同一批能力。
不是"GUI 是主程序，CLI 是降级模式"。这决定了后面所有分层。

## 2. 三条不可违背的约束

| 约束 | 来源 | 架构后果 |
|---|---|---|
| **百万级数据** | 数据量要求 | 数据只有一个主人（Rust）；UI 内存 = O(像素宽)；一切取数有界 |
| **新增能力不改既有代码** | 扩展性要求 | 能力接入是**纯数据贡献**；宿主不认识具体能力；贡献类型用开放字符串 |
| **解析器不限语言** | 解析器要求 | 解析器边界是 **C ABI**，不是 C++ 虚函数 |

## 3. 骨架：意图管道

所有前端的所有请求，最终都是一个 `Intent{op, params}`，交给同一个执行器。

```
GUI 点击 ─┐
CLI 命令 ─┼─▶ Intent ─▶ 执行器 ─▶ 能力提供者 ─▶ IntentResult
脚本调用 ─┘                        │
                                   └─▶ 数据层（Rust）
```

- **执行器**（`host`）是唯一知道"意图怎么落地"的地方；
- **能力提供者**分两类（见 §4），彼此正交；
- `IntentResult` 必须可序列化 → GUI 操作能录制成 Job、脚本能回放。

这条管道是**唯一**的跨部件通信方式，避免了老代码里六套并行通信机制的历史问题。

## 4. 能力层：两条正交的线

### 4.1 器官（modules）—— app 自带的手脚

**定位澄清**：写 `modules` 的首要目的是**代码组织**——不让 `MainWindow` 膨胀，
每个功能块是独立编译单元，出问题 grep 一个文件就定位。不是"插件"。

| 属性 | 设计 |
|---|---|
| 生命周期 | 有：宿主按依赖拓扑装载/卸载，卸载 = `delete` → Qt 父子树回收全部 UI |
| 协作方式 | 同进程同事，**直接 C++ 接口调用**，不必绕意图层 |
| UI | 通过 `UiContext` 描述需求（要什么面板/动作），**不直接依赖 QWidget** |
| 扩展方式 | 静态自注册进全局表 → 新增 module 不改宿主/App |

> 为什么 module 不依赖 Widgets：CLI/headless 必须保持纯 Core（无 `libQt6Widgets`）。
> module 只说"我需要一个数据集树面板"，由外壳的部件工厂给实现。

### 4.2 业务包（extensions）—— 领域知识

按业务方向组织：**飞控 / 汽车 / 机器人 / 卫导**（+ 通用）。

一个包聚合任意种类的贡献，贡献类型是**开放字符串**（`format` / `parser` / `ui` /
`view` / `script` / `analysis` / `report` / `doc` / `example` …），不是封闭枚举——
因为清单会持续生长，封成枚举每加一类就要改 core 并全量重编译。

**知识包是装载/卸载的原子单位**：装一个飞控包 = 一次性登记它的格式 + 解析器 +
视图 + 面板 + 脚本；卸一个 = 一次性撤销全部。

**扩展方式**：包静态自注册 → 新增包不改宿主/App；外部插件 `dlopen` + ABI 校验 →
汇入同一张注册表。

### 4.3 纯数据贡献模型（关键设计）

所有扩展点的第一类形态都是**纯数据**（不含 `QWidget*`），放在 `core`：

```cpp
struct UiSlot  { QString area; QString label; QString owner;
                 SlotKind kind;      // RibbonPage/RibbonGroup/DockPanel/Canvas/Menu
                 QString parentArea; int order; };
struct UiAction { QString id; QString label; QString area;
                  QString intentOp; QString data; };
```

好处是三重的：

1. **headless 可查询**：CLI 能问"这个领域贡献了哪些入口"，无需起 GUI；
2. **GUI 外壳只做绑定**：插槽数据 → SARibbon/ADS 对象，规则集中在一处；
3. **业务 UI 随包装卸**：包撤了，插槽内容自动消失，app 骨架不动。

## 5. 数据层：Rust 拥有数据

```
core::Dataset  ── 只读 ps_kernel.h 一张 C ABI 门 ──▶  Rust PsDataset
   ↑ 句柄 + schema 投影 + 有界读取（不持任何列值副本）
```

- 流式灌入：`appendFrame` 分块喂，C++ 侧内存 = O(一批)；
- 有界读取：`readWindow(cap)` / `channel(cap)`，百万行也只返回几千点；
- 视口抽稀：`lodMinMax(px)` 返回 px 个 `(x,min,max)` → **绘图内存与行数无关**；
- 算子：滤波 / 抽取 / 统计 / FFT 全在 Rust。

**C++ 侧永不物化全量**。这是百万级能落地的唯一前提。

## 6. 解析器 SPI：喂数据 → 出结果 → 调用者落库

需求原文：「解析器核心工作，由调用者给他喂数据，解析器出来结果，然后调用者把数据
推送到 rust 或者其他位置。解析器不限制编写语言。允许主动注册，也支持自动扫描。」

### 6.1 职责切分

| 角色 | 做什么 | 不做什么 |
|---|---|---|
| **解析器** | 字节 → 结构化批 | 不读文件、不写存储、不认识宿主 |
| **调用者** | 喂字节、收批、决定批去哪 | 不关心字节怎么解析 |

这一刀带来的收益：同一份 ULog 解析器既能读文件、也能读串口/网络流；
解析器可独立单测（喂一段字节，断言输出结构）；结果归宿由调用者决定，解析器不绑定 Rust。

### 6.2 契约（C ABI）

```c
typedef struct PsParserVtable {
    uint32_t abi_version, struct_size;
    void *(*create)(void);  void (*destroy)(void *ctx);
    int32_t (*sniff)(void *ctx, const uint8_t *data, uint32_t size);
    int32_t (*feed)(void *ctx, const uint8_t *data, uint32_t size,
                    const PsParserSink *sink, PsError *err);
    int32_t (*finish)(void *ctx, const PsParserSink *sink, PsError *err);
    void  (*reset)(void *ctx);
} PsParserVtable;

typedef struct PsParserSink {
    void *ctx;
    int32_t (*frame_batch)(void *ctx, const PsFrameBatch *batch);  /* 表+时间轴+typed列 */
    void    (*progress)(void *ctx, uint64_t done, uint64_t total);
} PsParserSink;
```

- **纯 C**：无 C++ 类型泄漏 → 任意语言可实现；
- **结果出口是回调**：批可零拷贝直接落 Rust，不经中间缓冲；
- **批结构复用老仓库 `ps_extension_api.h` 的 `PsFrameBatch`**——与 Rust `append_frame`
  入参同构，落库零转换；
- **可选项用 `struct_size` 判定**：ABI 可向后扩展。

### 6.3 两条注册路径

```
内部扩展：NG_REGISTER_PARSER(XxxParser)  ─┐
                                          ├─▶ ParserRegistry（进程内唯一）
外部插件：目录扫描 → dlopen → ABI 校验 ──┘
```

外部插件校验项：`abi_version` / `struct_size` / 必需槽位非空 / `id` 不冲突。
任一不通过只跳过该插件并记报告，不影响其他解析器，更不影响内置格式。

## 7. GUI 外壳：只做绑定，不做抽象

### 7.1 架构判断（这条是本轮最重要的修正）

**外壳层不应存在 "DockManager" / "RibbonManager" 这类东西。**

理由：ADS 已提供 `ads::CDockManager`，SARibbon 已提供 `SARibbonBar`。
再包一层同名类是**零价值中转**——它不承载任何领域概念，只转发调用。
老代码的 shell 层有 9 个 Manager、2973 行，其中 `DockManager`(256) 与
`RibbonManager`(283) 干的是同一件事：把贡献数据铺进第三方组件。

ng 的做法：**一个绑定器，替代两个管理器。**

```
core::UiSlotRegistry（纯数据：有哪些页签/分组/面板/动作）
        │
        ▼
gui::ShellBinder（唯一知道 SARibbon / ADS 的地方）
   ribbon.page  → SARibbonCategory
   ribbon.group → SARibbonPanel
   action       → QAction → SARibbonToolButton
   dock.*       → ads::CDockWidget
   canvas.*     → 中央 dock
```

一个类、一处规则、可整体替换第三方库。ng 的增量价值在 `core` 的插槽数据模型，
不在外壳的类数量。

### 7.2 组件选型

| 能力 | 选型 | 许可 | 位置 |
|---|---|---|---|
| Ribbon | SARibbon 2.9.6 | MIT | `src/3rdparty/saribbon`（vendored） |
| Docking | Qt-ADS | LGPL-2.1（共享库链接） | `src/3rdparty/ads`（vendored） |
| 画布 | 自研 QPainter | — | 参考老代码 `PlottingCanvas` 实现 |

三者已在仓库内 vendored，**不引新依赖、不联网**。

### 7.3 视图层：可插拔，而非模式枚举

老代码 `PlottingCanvas` 用一个 `PlotMode` 枚举承载 12 种绘图模式
（TimeSeries/Xy/Scatter/Bar/Histogram/Spectrum/Horizon/Compass/Gauge/Traj3D/GpsMap/Surface3D），
paintEvent 里按模式分支——加一种模式就要改这个 3800 行的类。

ng 的做法：**视图是一种贡献类型（`view`）**，每种视图一个独立类，注册到视图注册表。
- 加新视图 = 新增一个类 + 注册，**不改任何既有视图**；
- 视图只消费 kernel 的 LOD 输出，不持数据；
- 老代码的 12 种模式作为**首批迁移目标**，逐个拆成独立类。

这与 §2「新增能力不改既有代码」是同一条约束的延伸。

## 8. 分层与依赖

```
app/         入口：CLI（纯 Core）· GUI（+ Widgets）· 脚本
  ↓
gui/         GUI 外壳：ShellBinder · 部件工厂 · 视图（Qt Widgets，仅 GUI 进程）
  ↓
host/        装载循环 · 意图执行器 · 注册表（模块表 / 包表 / 解析器表）
  ├── modules/     器官：data / plot / analysis / script
  ├── extensions/  业务包：common / autopilot / automotive / robotics / gnss
  └── parsers/     解析器 SPI 与注册表
  ↓
core/        Dataset 门面 · 仓库 · 意图 · 插槽表 · 贡献抽象（无 QWidget）
  ↓
kernel       Rust：typed 表 · LOD · 缓存 · 算子（只经 ps_kernel.h）
```

依赖严格向下。**core 无 Qt Widgets、无第三方 GUI 库**——这是 headless 纯净的保证，
也是 CLI 与 GUI 行为一致的前提。

## 9. 与老代码的关系

| 维度 | 关系 |
|---|---|
| **需求与领域知识** | 直接继承（飞控/汽车/机器人/卫导的格式与语义） |
| **实现代码** | 实现时参考老代码（协议解析算法、画布绘制、ADS/SARibbon 用法） |
| **类清单与分层** | **不照抄**。老代码的分层是演进产物，ng 按本总纲重新划分 |
| **数据契约** | 复用 `ps_extension_api.h` 的 `PsFrameBatch` / `PsHostApi` 布局（C ABI，稳定） |

具体地，以下几处**明确不照抄**：

| 老代码 | ng 的做法 | 理由 |
|---|---|---|
| 9 个 Manager 的 shell 层 | 1 个 `ShellBinder` | Manager 满天飞是演进产物，不是设计 |
| `IComponent::contribute(Registry&, HostContext&)` 一把梭 | 贡献拆成**纯数据**（core）+ 部件工厂（gui） | 老版贡献里嵌 `QWidget*`，headless 不可用 |
| `DockContribution` 携带 `std::function<QWidget*()>` | 插槽纯数据 + 部件类型字符串 | 同上 |
| 12 模式合一 canvas | 视图注册表，每种视图独立类 | 加模式不改既有代码 |
| `IMessageParser`（C++ 虚函数） | C ABI 的 `PsParserVtable` | 虚函数不能跨 dll，违背"不限语言" |

## 10. 落地顺序

| # | 事项 | 依赖 |
|---|---|---|
| 1 | 解析器 SPI：`ps_parser_spi.h` + `ParserRegistry` + 宿主喂数据流程 | — |
| 2 | 现有 CSV / ULog 改造为 SPI 实现（验证 SPI 够用） | 1 |
| 3 | 外部插件扫描：dlopen + ABI 校验 | 1 |
| 4 | GUI 外壳重做：`ShellBinder` 取代自研 RibbonBar + DockManager | — |
| 5 | 视图注册表 + 首批视图（从老代码 PlottingCanvas 拆） | 4 |
| 6 | 其余格式按 SPI 平移进对应领域包 | 2 |

## 11. 明确不做

- 不自研 ribbon / docking（用 SARibbon / ADS）；
- 不用 C++ 虚函数做扩展边界（跨不了 dll）；
- 不让解析器直接写存储（它只做转换）；
- 不在 core 引入 QWidget（headless 纯净）；
- 不做数据库后端（当前不需要，但数据层为此预留）。
