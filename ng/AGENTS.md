# ng 现状评审与硬约束

> **本文件是 `ng/ARCHITECTURE.md` 的补充，不是替代。**
> `ARCHITECTURE.md` 回答「**应该怎么设计**」——分层、核心抽象、器官与扩展、意图管道、落地顺序；
> 本文件回答「**现在做到哪了、还差什么、写代码时不许做什么**」——只记录现状判定与边界红线，不重复任何机制描述。
> 两者冲突时：机制描述以 `ARCHITECTURE.md` 为准，进度与边界判定以本文件为准；`ARCHITECTURE.md` 变更后，本文件 §2–§4 需同步核对。

一句话结论：

> **ng 解决了「东西怎么放」，正在补「东西是什么」。机制层面已闭合；
> 内容迁移已启动——plotting 会话层与 protocol（逻辑资产 + 解析器 + 编辑器）已完成首轮，
> 其余格式与视图模式仍在迁移中。**

---

## 0. 协作要求（最高优先级）

> **你是架构师，我是需求方。我提问不一定专业；你要作为架构师有判断能力，
> 不能因为我的错误言论动摇你的架构方向，成为墙头草。**

这条约束凌驾于本文所有具体判定之上，执行准则：

- 需求方的提问用于**澄清意图**，不用于**推翻已论证的架构决策**。
- 提问与架构判断冲突时，给出**有技术依据的判断**并说明为什么不改，而不是顺着提问改口。
- 只有当需求方给出**具体事实或反例**（某场景失效、某需求明确要求相反设计）时，
  才重新评估并调整；语气强弱、重复次数都不是改判依据。
- 反复在同一处被质疑时，回到本文的判据与证据，不来回摇摆、不逐次退让重写结论。
- **举证一针见血：用反例，不堆原则。** 当面质疑一个设计判断时，先给一个
  「按这个方案做，明天具体会卡在哪」的画面（如：格式编进 op → 插件作者 `.so`
  写好了，打开对话框却还是没有那个后缀，插件化到前端就断了），而不是先铺
  「op 是意图、格式是策略」这类抽象话。需求方要的是**看懂哪条路走不通**，
  不是听架构名词；一个好反例胜过三段论证。

---

## 1. 定位与体量

截至 `nextgen-architecture` @ `a5f2887`（行数口径：`wc -l`，含空行）：

| 维度 | 事实 |
|---|---|
工程形态 | 独立第二工程：`ng/CMakeLists.txt` 自 `project(PlotScopeNG)`；仓库顶层 `CMakeLists.txt:40` 只 `add_subdirectory(src)`，**未接 ng** |
体量 | 123 个 `.cpp/.h`、13694 行（不含 build） |
分层 | core 4514/50、gui 2123/17、extensions 1938/22、tests 1650/3、recording 1285/8、modules 1121/12、host 679/8、app 384/3 |
并存关系 | 老 `src/` 原样保留。迁移期间两套同时存在，这是当前最大的结构性风险，见 §4 |

> 修订：`02dafc0` 基准为 122 文件 / 13118 行；此前版本误用「非空行」口径写成 11333，已更正。

---

## 2. 已经成立的机制（可复核，不是声明）

| 条目 | 证据 | 怎么复核 |
|---|---|---|
**概念收敛** | 老 `src/application` 有 26 个 `*Manager/*Service/Host*/…Bridge/*Registry/*Component`、`runtime/host` 16 个头、shell 3846 行；ng 只剩五个核心抽象：`Dataset` / `Intent` / `Contribution` / `Module` / `ToolPack` | grep 上述后缀 |
**单一注册写法** | `NG_REGISTER_{MODULE,PACK,PARSER,PANEL}`，一种写法四个落点，全部是 TU 末尾一行写静态表 | grep 宏使用点 |
**物理解耦** | 每个 module 是 OBJECT 库，只 `target_link_libraries(... ng_host)`（`modules/data/CMakeLists.txt`、`modules/plot/CMakeLists.txt`）；`ng_module_plot` 不 link `ng_module_data`，include 路径不外露 → 想 include 对方头文件直接编译失败 | 看 CMake + grep 跨模块 include（当前 0 处） |
**三方分离** | 器官 `ui.requestPanel(area, panelType)` 只给两个字符串（`IModule.h:52`）；视图 `NG_REGISTER_PANEL` 自注册；外壳只做装配（`GuiContext.cpp:60` → `PanelFactoryRegistry::create` → `ShellBinder.cpp:122` 装进 ADS）。三者互不认识 | 通读三个文件即可确认。壳层全量 `gui/shell` 1141 行（MainWindow.cpp 223 / ShellBinder.cpp 394 / GuiContext.cpp 101 / PanelFactoryRegistry.cpp 47） |
**host 不碰 Widgets** | `ng_host` 是 STATIC 且只 link `ng_core + Qt6::Core`；`IModule.h` 只 `class QWidget;` 前置声明；CLI 可执行不 link gui | `host/CMakeLists.txt`、`app/CMakeLists.txt` |
**交互能力也走接口** | `UiContext::pickFilePath`（`IModule.h:56`，`a5f2887` 新增）：器官要选文件**不许自己开 QFileDialog**，由外壳返回路径，模块拿到后照样发 `data.load` 意图 —— GUI 与 CLI/脚本加载数据仍是同一条路 | 看 `IModule.h` + 外壳实现 |
**插拔被测试钉死** | `tests/test_architecture.cpp` 的 `ProbeModule` / `ProbePack` **只在该测试文件里**定义并自注册；哪天必须改 `App.cpp` 才能加模块，这条测试就红 | 跑契约测试 |
| **骨架槽单一来源** | 骨架槽只在 `UiSlotRegistry::declareSkeletonSlots()` 一处声明，GUI 与 headless 拿同一份；契约测试钉死"槽齐备 + kind/父槽/label 完整 + 无未归属贡献" | 跑契约测试 |
| **领域自带面板** | 形态随领域变时形态归知识包：描述 `IPanelContribution`（纯数据，headless 可见）+ 实现在 `extensions/<域>/gui/`（OBJECT 库，只 GUI 链接），共用 core 的一张工厂表；器官走 `availableDomainPanels(domain)` → `requestDomainPanel(area,id)`。呈现层依赖 core，**不依赖 ng_gui** | 跑 `test_gui` 3c 段 + `ldd` 确认 CLI 无 Qt6Widgets |
| **视图链可用** | `test_gui` 断言 `dataset-tree` / `plot-canvas` 已登记、`dock.left` 面板存在、并能读出 1 dataset / 1 table | 跑 GUI 测试 |
| **视图物理归属** | 三类判据：①功能专属 view 聚合进该功能目录（`modules/data/DataManagerPanel.*`），CMake 拆 `<功能>`/`<功能>_gui` 两 target（前者 CLI 链、后者只 GUI 链）；②通用能力库留 `gui/views/`——`plot-canvas` 将来抽成 Qwt-like 独立绘图库，**不属于任何器官、不进 modules**；③领域面板**永远属于领域**，留 `extensions/<域>/gui/`，不进 modules（器官是机制不懂业务）、不进 gui/views（会成领域杂物箱）。物理聚合但逻辑仍解耦：器官只发字符串 id，不 include 视图头 | `ldd` 确认 CLI 与 headless 测试无 Qt6Widgets、无 `DataManagerPanel.cpp.o`；GUI 测试含之 |
| **data 意图归口宿主** | `data.load` + 派生（`data.reload`/`export`/`recent`/`clearCache`，`DataCommandsHandler`）都是宿主系统级 handler，不归属可卸载模块（要访问 AppContext 的 repo/sources/settings/pipeline）；`DataModule.onAttach` 只声明绑真实 intentOp 的 action。补能力铁律：**只搬底层有 API 支撑的，不造空按钮**。reload 用稳定 id 保持数据集不换 id（订阅者不失联） | 跑 `test_architecture` 3c 段 + `test_gui` action 段；headless 下四意图均可执行 |
| **plot 多曲线叠加** | `PlotCanvas` 持 `QVector<PlotSeries>` 逐条喂 fastplot，`ChannelView::addChannel` 按 `datasetId\|table\|column` 去重，双击/拖拽共用追加路径；缩放/平移按可见窗重取 LOD；`test_plot_io` 钉死 3 曲线不同色/同 key 不新增/单项移除/`series.list` | 跑 `test_plot_io`；或 `plotscope-ng-gui-shot <png> <multi-col.csv>` 截图看 3 色曲线 |
| **protocol 首轮迁移** | `libs/protocol`（`ng_protocol`，纯逻辑，只依赖 Qt6::Core）+ SPI v2 `CustomProtocolParser`（configure 收模板/finish 解码吐 frame_batch）+ 薄 `ProtocolEditorDialog`（新建/打开/保存/校验/试算）；`test_protocol` 钉死 validate/往返/求值/帧解码/SPI 闭环 | 跑 `ng_test_protocol`；或 `plotscope-ng-gui-shot --protocol <png>` 截图 |
| **图库/快捷键是纯数据** | `UiAction` 的 `galleryGroup`/`visual`/`shortcut` 三个字符串字段；`ShellBinder` 按组收进 `SARibbonGallery`、建 QAction 时 setShortcut。plot 12 图型分 4 组、parse 格式驱动进来源库、打开数据 Ctrl+O，全 headless 可读 | 跑 `test_gui` 2a-2/2a-3 段；或 `plotscope-ng-gui-shot --type=gauge\|scatter <png> <csv>` 截图 |
| **双 Y 轴用户可选** | `PlotCanvas` 右键菜单勾选曲线挂右轴（`plot.series.axis`）；`pushToPlot` 对左右轴分别 `setYRange`/`setYRange2`；挂轴分配随视图状态序列化 | 跑 `test_plot_io` 第 7 段 |

---

## 3. 问题清单（按「会不会返工」分级）

### P0 —— 现在不定，后面一定要返工

**P0-1 两条数据通路并存 —— 内置格式已全部归并。**
- 通路 A（旧）：`data.load` → `DataLoadHandler` → `IFormatDriver::load()`，驱动自己读文件、自己建 Dataset。
  **内置格式（ulog / csv）已全部退出这条通路**：驱动只声明 `parserId()`，宿主据此走管道；
  两个驱动的 `load()` 都改为明确报错（不再偷偷走另一条路）。
  该接口保留为"尚未归并的格式"的过渡出口，宿主仅在 `parserId()` 为空时才调用。
- 通路 B（**唯一生产路径**）：`data.load` → `IngestOrchestrator` → `Source` → `IngestPipeline::bindParser/feed/finish` → parser sink → Dataset。
  读字节由宿主统一编排（`IByteSource`），格式解析由 SPI 解析器提供（`ParserRegistry`）。
- 归并后的形态：**每种格式只有一份解析器实现**。
  - ULog 的协议解析在 `UlogStreamParser.cpp` 里，SPI 路径与旧路径共用同一份代码，分叉点只有"批去哪"（`Emitter`）；
  - CSV 的解析在 `CsvParser.cpp`（新写的 SPI 实现），旧的"驱动自己读文件"版本已删除。
- **宿主策略归位**（这三项都不是解析器的事，上一轮归并时一度丢失，已补回）：
  | 策略 | 归属 | 载体 |
  |---|---|---|
  | 表名（CSV 取文件名/intent，ULog 取 topic） | 扩展层**声明** | `IFormatDriver::preferredTableName()` + `IngestPipeline::setTableRename()` |
  | 缓存（源文件 size+mtime 指纹） | 宿主编排 | `IngestOptions::useCache`，缓存路径 `<源文件绝对路径>.<解析器id>.psc` |
  | stable id（录制/回放 id 不漂移） | 宿主编排 | `IngestOptions::stableId` → `IngestPipeline::bindParser(..., preferredDatasetId)` |
- 行为锁：`tests/test_pipeline.cpp` 断言 **ULog 的 SPI 管道与旧 driver 路径产出逐行相同**、
  **CSV 走管道后表名/列值/时间轴与旧语义一致**、**缓存命中产出同名同内容的数据集**、
  **stable id 生效**；`tests/test_architecture.cpp` 断言 ULog/CSV 加载后的数据集名是管道生成的
  `src<源id>_<解析器id>`。
- 处置（后续新格式）：**直接走 SPI，不写 driver 的 load()**。否则每加一个格式要写两份，录制回放只能覆盖其中一条。

**P0-2 属性编辑无归属 —— 契约已定，视图未接。**
老 `PropertyManager` 449 行是「protocol 对话框」和「绘图属性页」**共同偷偷依赖**的能力。
处置：先定契约再搬 —— **属性 schema 由提供者声明，属性表只做视图显示**，两边通过 `Intent(prop.*)` 走同一条路。
**已进展**：`core/PropertySchema.h/.cpp` 已建（纯数据 `PropertySchema/PropObject/PropRow/PropKind` +
`PropIntent` 常量 `prop.schema`/`prop.set`），协议编辑器明确不走此契约（文档级形态不同）。
**仍待做**：通用属性面板 GUI 尚未接；plot 属性页与通用属性表尚未统一到该契约。
注意协议编辑器是「整篇 JSON 文档」工具，与属性面板（少量键值行）形态不同，**不强行纳入** PropertySchema，否则会把文档编辑塞进键值视图。

**P0-3 文档级状态无归宿。**
老 `ModuleBridge` 31 项里有 5 项属于「文档级状态」（时标/书签），ng 的 `Intent`/`Dataset` 里没有明确位置；ng 也**没有 Document / Project 边界**。
处置：四类状态四个归宿，**不新增第五个概念**：

| 状态 | 归属 | 生存期 |
|---|---|---|
时标 / 书签 | 工程文档 | 随工程 |
活动通道 | 会话 | 随会话 |
缩放 / 平移 / 选中 | 视图实例 | 随视图，序列化成 Spec |
dock / ribbon 布局 | 工作区 | 跨会话 |

### P1 —— 名实不符或埋雷

| 编号 | 问题 | 说明 |
|---|---|---|
P1-1 | **外部插件只有入口** | `app/App.cpp` 自注「本轮仅预留入口」，`PluginScanner` 只有注释，无 dlopen 与 ABI 校验。「插件可自定义格式」这条卖点暂时是空的 |
P1-2 | **Intent 无 schema** | `op` 是裸字符串、payload 是 `QVariant`，拼错只能运行时失败，无 op 白名单 |
P1-3 | **`QWidget*` 是唯一耦合后门** | `requestPanel` 返回 `QWidget*`，器官一旦 `qobject_cast` 成具体视图类，就重新认识视图，耦合复活。**测试里可以，生产代码禁止** |
P1-4 | **面板类型字符串静默失败** | 找不到工厂时 `requestPanel` 返回 `nullptr`，无日志无断言；类型名与器官声明之间没有对账机制（目前只有 `test_gui` 里硬写了两个名字） |
P1-5 | **兼容入口未清** | `ModuleContext::data()/host()` 转发仍在，注释要求新代码用 `app()`；兼容入口留着，迟早长出第二条路径 |
P1-6 | **幂等声明会静默丢弃后声明者** | `UiSlotRegistry::declareSlot` 是"先到者为准"。骨架槽曾在 host 层与 GUI 层各声明一次，于是信息更全的 GUI 那份被静默丢弃（label 不准、kind 退化成默认值），headless 还只看到半套槽。**已修**（骨架槽改为一处声明，见 §2 "骨架槽单一来源"）。同类风险仍在：任何新增的幂等声明接口都要问"谁先谁后，后来的会不会被吞" |

### P2 —— 内容单薄，属于进度而非缺陷

- 视图只有 `timeseries` 一种，12 模式未拆（§11 #12）。`a5f2887` 后 `PlotCanvas` 203 行 / `ChannelView` 129 行，仍是单视图
- 器官缺 `mod.source` / `mod.dataset` / `mod.analysis` / `mod.report`（§11 #11）
- 格式实作只有 `csv` / `ulog` / `pspx`（§11 #14）。`csv` 与 `ulog` 均已归并到统一摄取管道，**格式知识各只有一份实现**
- **`UiContext` 的接口在增长**（`a5f2887` 加了 `pickFilePath`）。目前都是外壳能力、不构成概念，但每加一个方法都要回答同一个问题：**为什么它不能走 `Intent`**。答不出来就不该加——这是 `ServiceRegistry` 复活的最短路径

---

## 4. 差距对照

| 目标能力 | 现状 | 差什么 | 卡在哪 |
|---|---|---|---|
多格式解析（7 种：ulog/mavlink/nmea/oem7/asc/blf/mdf4） | 3 种 | 逐格式平移 + 等价性快照 | P0-1 必须先归并通路 |
12 种视图 | 库层 12 种模式已全部归属 fastplot；ng 会话层已完成**多曲线叠加 + 双 Y 轴 + 图型 gallery（12 项）+ 仪表切换** | 游标/书签/多画布/页面拆分；仪表数据绑定（gallery 切换本身已可用） | 多曲线叠加前置项**已完成** |
报告自动化 | 无 | `mod.report` + headless 渲染 | #11 |
protocol 逻辑资产 | `ng_protocol` 纯逻辑库 + SPI v2 解析器 + 薄编辑器均已建，`test_protocol` 全绿 | 校验/试算已可用；位域模型/表达式图形编辑未做（老 1749 行胖 UI 不平移） | 首轮已完成 |
插件生态 | 只有入口 | dlopen + ABI 校验 | P1-1 |
工程文件（存盘/打开） | 无 | Document 边界 | P0-3 |
Lua 停产风险 | 老绑定约 40 个 | 按 Intent op 重写，不做 shim | 需确认在用的脚本数量 |

老代码资产盘点（这是「搬家」的真实分母）：

| 老目录 | 总行 | UI 外壳 | 可搬资产 |
|---|---|---|---|
`modules/datasource` | 11560 | 7117（widget） | 1982（services：录制/抓包/live） |
`modules/plotting` | 7286 | **7081** | **156**（core） |
`modules/protocol` | 5342 | 1857（dialog） | 2407（codec + protocol） |
`modules/scripting` | 3056 | — | 绑定表（需重写） |
`modules/analysis` | 409 | — | 409 |
`application/shell` | 3846 | 3846 | 0 |
`extensions/builtin` | 12888 | — | 约 4500（各 `*Parser.cpp`） |

结论：**这不是「把 4 万多行搬过来」，是把约 8000 行资产种进 4000 行新壳里**。
`plotting` 是唯一「没有资产可搬」的模块——它 98% 是外壳，所以只能重写，不能平移。

### 4.1 fastplot 搬迁快照（2026-11-18，分支 `feat/ng-fastplot`）

- **库层完成**：坐标/数据零拷贝/交互/双轴图例导出/六仪表/散点符号/柱状/直方/频谱，全部在 `ng/libs/fastplot/`，纯 Qt Widgets、不依赖 ng/core；ctest 8/8（Linux offscreen）。
- **老 12 种模式归属**与**待办清单**见 `docs/roadmap/fastplot.md` §13，下次回顾先看那里。
- **多曲线叠加已完成**（2026-11-19）：`PlotCanvas` 多 series + `ChannelView::addChannel` 去重追加 + 图例 + 缩放重取 LOD；`test_plot_io` 与 GUI 截图双重验证。
- **下一步**：图型选择入口、XY 配对、游标/书签/多画布；左右轴归属（`rightAxis` 字段已预留，UI 入口待做）。
- **红线不变**：`src/` 只读不改；库不复制数据；会话胶水（页签/多文档/布局/撤销）留在 ng 模块，不进库。

---

## 5. 写代码时必须守的硬线

1. **不往 core 加概念。** 五个之外的新概念必须论证，且要删掉一个旧的做等价交换。
2. **模块之间不 include。** 构建图已经禁止，不许用相对路径绕过。
3. **不许 `new QWidget` 直挂窗口。** 只能 `requestPanel` 声明需求。
4. **不许 `qobject_cast` 成具体视图类。** 器官只许用通用 `QWidget` 接口。
5. **不许自建注册表。** 一张注册表 + 索引视图，模块私有 registry 一律不给。
6. **OBJECT 库不得改成 STATIC。** 自注册依赖 TU 静态对象的构造函数，链接器会丢弃未引用目标文件 → 功能静默消失。**且 OBJECT 库必须被每个需要它的可执行文件显式链接（测试也算）**——`ng_test_gui` 曾漏链 `ng_ext_automotive_gui`，`CanBrowserPanel.cpp.o` 没进二进制，领域面板注册静默不发生。headless 测试故意不链，这是刻意保持 CLI 纯净。
7. **行数预算：** view ≤300、canvas ≤200、interaction ≤250、spec ≤150。
8. **每迁一块删一个老入口。** `src/` 冻结，只修 bug；留着「先保险」的入口，三个月后一定长回来。
9. **注释写给人看，不写给 AI 看。** 只写三件事：做什么、怎么做、必要的简单原因。具体：
   - 短——多数注释一两行，整段「设计说明书」放进文档，不放进代码；
   - 不讲故事——不写「历史上踩过什么坑」「当初为何否决另一方案」「实测发现」；
   - 不引出处——不写「见 master-plan §7」「AGENTS.md §5」「遵循某某铁律」，代码注释里不需要自证清白；
   - 不用 Markdown——注释里不堆反引号、粗体、列表符号，用普通句子；
   - 名字能说清的就不写——`remove()` 不用注释「移除条目」。

### 5.1 libs 铁律（2026-11-19 新增；终态 = core 消亡）

> 完整裁决见 `docs/roadmap/core-refactor-master-plan.md` §7。一句话：
> **libs 是无隐藏运行时状态的能力提供者——谁要用谁在上层实例化，生命周期归组合根。**

| 类别 | 判定 |
|---|---|
| 函数 / 算法 / 模板、class/struct **定义**（含成员）、常量 / 枚举 / 别名 | ✅ 允许 |
| **static 自注册对象**（OBJECT 库那套 NG_REGISTER_*） | ✅ 允许——唯一的活实例例外 |
| class 的成员对象（如 AppContext 的 `m_repo`，随实例生死） | ✅ 允许——不是独立存活 |
| 命名空间级全局对象实例（跨调用存活的状态） | ❌ 禁止 |
| 单例（Meyers、函数内 static 实例） | ❌ 禁止 |
| 服务定位器式活实例被任意 include 取用（`ctx.data()` 满地抓） | ❌ 禁止 |
| libs 内部 `new` / 持有跨调用存活对象 | ❌ 禁止 |
| libs 依赖上层（core / host / gui / modules / app / extensions） | ❌ 禁止——只能向下依赖 foundation / Qt / 系统库 |
| libs 内 `#include "../"` 逃逸出库目录 | ❌ 禁止——物理隔离是硬线 |

**AppContext 特别规定**：定义在 `libs/context`、唯一实例在组合根 `App`、`host/gui/modules/extensions` 一律**构造注入**拿引用，不许 include 全局 ctx。**不进 app 文件夹**（app 是可执行目标，会被依赖倒挂 + 与 host 成环，master-plan §7.2 有论证）。

**这些边界由构建期守卫强制**：`cmake/LibBoundaryGuard.cmake` 在 configure 期扫描，违反（逃逸 include / 单例 / 上层链接）直接 `FATAL_ERROR`，编译还没开始就红。正则不可靠的三项（普通全局对象、服务定位器取用、static 自注册例外）交给 code review 与 ctest。**守卫只许加严，不许为绕过而加豁免。**

**命名空间纪律**：core 消亡按批次收口，每个 lib 搬迁时连 namespace 一起改（`core` → `context`/`dataset`/…），不留「目录叫 context、namespace 还叫 core」的中间态。B7 前 `core` 残留仅发 WARNING。

---

## 6. 判定：它解决了哪些疑惑

| 老疑惑 | 判定 |
|---|---|
概念太多（host / Service / Manager 满天飞） | ✅ 解了：壳 844 行，无一个「管事情的」类 |
module 没法解耦 | ✅ 解了，而且是焊死的（include 不到，不是「约定不 include」） |
`ModuleBridge` 31 项拆来拆去 | ✅ 解了：动作用 `Intent`、查询用 `DatasetId`、界面用 slot |
注册方式太多（8 扇门） | ✅ 解了：一种写法 × 四个落点 |
器官要有界面、但新增不改别人代码 | ✅ 解了：器官声明入口，视图实现控件 |
用信号槽把 `MainWindow*` 发给模块 | ⚠️ 半边：通知用信号，命令用 Intent，指针不进模块 |
plot 3000+ 行 | ✅ 库层已拆：fastplot 独立库，12 种模式全部有归属；✅ 会话层多曲线叠加/图例/拖拽/导出/双Y轴/图型 gallery/仪表切换已接（UI 体验抄老代码，架构不抄）；⚠️ 游标/书签/拆分画布/仪表数据绑定仍是增量 |
MainWindow 太大 | ✘ 误诊：老 `MainWindow.cpp` 只有 194 行，真痛点是「同一件事 6 种注册写法」 |

---

## 7. 建议优先级

1. **P0-1**：✅ 内置格式已全部归并（ulog / csv），等价性快照已建立。下一步是把其余格式（mavlink/nmea/oem7/asc/blf/mdf4）逐个按同法平移。
2. **P0-2**：定属性契约（schema 归提供者，表归视图）。
3. **P0-3**：四类状态归位，Document 放在 app 层，不进 core 概念表。
4. **P1-3 / P1-4**：补两个守卫 —— 禁止 cast 具体视图类的检查、面板类型名的对账断言。
5. **§11 #14**：格式逐个平移，每迁一个删一个老入口。

---

## 8. 工程分界已同步到根 `AGENTS.md`（已完成）

根 `AGENTS.md` 已新增 **§0「工程分界：`src/` 与 `ng/`」**（最高优先级），明确：
`src/` 老工程冻结、只修 bug，适用根文件 §1–§13；`ng/` 是独立第二工程、活跃开发、新代码一律进这里，
适用 `ng/AGENTS.md` + `ng/ARCHITECTURE.md`。同时在根文件 §10 文档边界、§13 提交前检查各补一条指向 §0。
两套规则各管各的工程，冲突按"归属错误"处理，不再需要第三次解释边界。
