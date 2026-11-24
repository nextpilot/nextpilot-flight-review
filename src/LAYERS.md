# 层契约

本文件是 `src/` 各目录定位与依赖的唯一权威。新增代码先在这里判归属，再动手。与任何文档冲突时以本文件为准。

## 物理布局（权威映射，新增代码先对表）

| 物理目录 | 层 | 内容 |
|---|---|---|
| `application/app/` | L6（入口）/ L3（`ps_app`） | 组合根：`core/` App 装配 · `cli/` 批处理入口 · `mcp/` MCP 桥 · `diag/` 诊断快照；`ps_app` 静态库把三者收成一份源码，无 Widgets、不链模块 |
| `application/gui/shell/` | L5 | 外壳装配：MainWindow、ShellBinder、GuiContext、共享对话框 |
| `application/gui/` | L5 | GUI 外壳 + 功能视图（views/ 等） |
| `application/host/` | L3 | 运行时编排 |
| `application/modules/` | L4 | 模块（机制提供者）；每个模块内 headless 平铺 + `gui/` 呈现层子目录 |
| `libs/` | L0+L1 | 机制能力库（含 `libs/recording/` 录制实现） |
| `toolbox/` | L4 | 工具箱（内部扩展，领域知识） |
| `kernel/` | —— | Rust 数据平面（同进程，只经 C ABI） |
| `3rdparty/` | —— | 第三方源码（vendored，SARibbon/ADS 等） |

正文各处 `app`（L6 组合根）、`gui/shell`（L5 外壳装配）、`gui`、`host`、`modules` 等短名按上表读取。**`app` 与 `gui/shell` 不同名不同义**：`app` 是进程级组合根，`gui/shell` 是窗口外壳装配。kernel/libs/toolbox 禁止 include `application/`，由 `LibBoundaryGuard` 在 configure 期强制。

## 术语定标（全库唯一词表，文档与代码注释不得另造同义词）

| 术语 | 名称 | 内容 | 注册方式 | 生命周期 |
|---|---|---|---|---|
| **Module** | 模块（`modules/`） | app 的**基底功能**，**没有业务知识** | 静态自注册，**无法卸载** | **编译期固定**（只能编译时增删） |
| **Toolbox** | 工具箱（内部扩展，`toolbox/`） | **业务的承载者**——原厂集成的一般 Qt 开发 | 静态自注册，**运行时允许加载/卸载**（mount/unmount） | 运行时 |
| **Plugin** | 插件（外部扩展，`plugins/`） | **业务的承载者**——第三方开发，经 C ABI 接口 | **扫描注册**，运行时允许加载/卸载 | 运行时（M3 实装，当前仅骨架） |

Toolbox 与 Plugin **统称扩展**，两者**没有本质区别**，只差来源（原厂 vs 第三方）与通道（静态自注册 vs C ABI 扫描）。内容单元统一为**扩展**：一份纯数据清单 + 解析器（代码载体为 `Pack` 结构），`mount()` 进工具箱、按 provider 原子撤销。

## 命名定标（"Registry" 一词三层禁用规则）

| 词 | 归属 | 例子 |
|---|---|---|
| **Registry*** 前缀/后缀 | **机制层专用**：树、节点、货架基类、存储原语 | `RegistryTree`、`RegistryNode`、`TypedRegistry`、`Registry<T>`、`RegistryKind` |
| ***Hub** 后缀 | **业务保管类**：肚子里有一组条目、全系统唯一访问口 | `DatasetHub`、`SourceHub`、`DerivedSeriesHub`、`InteractionHub`、`UiSlotHub`、`ToolboxHub`、`ParserHub`、`RecordingFormatHub`、`RecordingBackendHub`、`TransformHub`、`PanelFactoryHub`、`BuiltinModuleHub` |
| **Node** | 节点身份，由**继承**表达，不进名字 | `DatasetHub : RegistryNode`（Hub 说职责，继承说身份） |

判定句：**Hub 回答"你保管什么、谁经过你"，继承 `RegistryNode/TypedRegistry` 回答"你挂在哪"。新建业务保管类一律 `<域>Hub`，禁止 Registry/Store/Repo/List/Manager 等同义后缀；机制层（libs/registry 树族）禁止 Hub 后缀。**

**静态表一律走 `StaticHub<Entry, Key>` 门面模板**（`libs/registry/include/StaticAutoRegistry.h`）：编译期可枚举的静态自注册表（PanelFactoryHub / BuiltinModuleHub / RecordingFormatHub / RecordingBackendHub 等）只写 Entry 结构 + `using <域>Hub = StaticHub<<域>Entry, Key>` 别名，插入/查找/遍历/计数由模板统一提供；**禁止再手写 QHash+order 私有存储或重复的 XXXHub 类**。运行期增删的表（DatasetHub 等）不走此模板，仍用运行期句柄机制。

## 依赖方向（严格向下，禁止逆向）

```
L0  foundation              Result / 事件 / 常量（INTERFACE，无状态）
     ↑
L1  dataset  registry  intent  uischema  protocol  fastplot    机制能力库
    context  module          运行态容器 · 模块契约（均居 libs/ 物理目录）
     ↑
L2  ——（已消亡）core 过渡层已整体删除，编号保留以免历史引用断裂——
     ↑
L3  host                   运行时编排：模块生命周期 + 意图路由 + 交互总线
     ↑
L4  modules  toolbox        模块（机制提供者）· 扩展（领域业务）
     ↑
L5  gui                    GUI 外壳 + 功能视图
     ↑
L6  app                    组合根：创建并装配 AppContext + RuntimeHost + 模块
```

箭头向上表示"可以被谁依赖"。依赖只能从上往下（上层依赖下层），**禁止下层依赖上层、禁止同层横向依赖**。

`libs/` 是一个物理目录，内含 L0+L1 多个机制库；`core` 是过渡层，B7 后目录删除、内容归入 L1 与 L3。

---

## 各层定位卡

### L0 `libs/foundation`

| 项 | 内容 |
|---|---|
| 定位 | 最底层的纯契约：值类型、Result、事件、公共常量 |
| 放什么 | 无状态的值语义类型、模板、别名 |
| 禁放什么 | 任何有状态对象、Qt Widgets、业务逻辑 |
| 可依赖 | Qt::Core、系统库 |
| 禁依赖 | 除 Qt/系统外的一切（含其他 L1 库） |

### L1 `libs/{dataset,registry,intent,parser,protocol,uischema,fastplot,context,module,recording}`

| 项 | 内容 |
|---|---|
| 定位 | 可复用的机制能力库——纯函数/类型/常量定义，谁用谁在上层实例化 |
| 放什么 | 数据结构与算法、契约接口定义、纯 metadata、静态自注册对象（OBJECT 库） |
| 禁放什么 | 全局活实例、单例、服务定位器、跨调用存活对象、对上层的依赖 |
| 可依赖 | L0、Qt、系统库、同层内明确申明的库（见依赖矩阵） |
| 禁依赖 | L2+ 任何层 |
| 铁律 | 遵守 `src/AGENTS.md` §5.1 libs 铁律；namespace 用所属库名，不得用 `core` |

各子库定位：

| 库 | 定位 |
|---|---|
| `dataset` | 数据集与仓库句柄（数据本体在 Rust kernel） |
| `registry` | 最小 CRUD 注册表核 + 自注册助手 |
| `intent` | 意向类型与执行契约、录制器 |
| `parser` | 消息解析契约：IMessageParser / IByteSource / ParserHub / SPI 头 |
| `protocol` | psproto 文档模型、编译、编解码、脚本后端 |
| `uischema` | 字段契约树 + SchemaEngine + 唯一 Qt 渲染器（`uischema_widgets` 是渲染子库） |
| `fastplot` | 通用绘图能力库（将来独立为 Qwt-like 库） |
| `context` | AppContext 运行态容器 + 设置 + 摄取 + 交互注册（唯一实例在 app，构造注入） |
| `module` | 模块契约：IModule / ModuleInfo / IUiContext（纯接口面，运行时在 host） |
| `recording` | 流录制机制：Recorder / RecordingFormat / RecordingBackend / Sink / Mcap+Raw 编解码 |

### L2（已消亡）

core 过渡层已整体删除，目录不存在。任何新增代码**禁止重建 core 目录或以"过渡暂存"为由塞入新层**；归属判不清时按"依赖更低层能完成任务就放低层"处理。编号 L2 保留仅为避免历史文档引用断裂。

### L3 `host`

| 项 | 内容 |
|---|---|
| 定位 | 运行时编排层——模块生命周期、意图路由、交互扩展点 |
| 放什么 | 模块装载/激活/卸载、意图执行器、系统级命令、交互扩展点的注册/派发转发（`InteractionHub` 的编排入口） |
| 禁放什么 | 具体业务格式、具体画布/面板、任何 `QWidget*` 的跨层返回 |
| 可依赖 | L0、L1、Qt::Core |
| 禁依赖 | L4+ 任何层 |
| 判定 | 协调"谁来做"属于这里；"做什么业务"属于 L4 |

> `InteractionHub`（viewAction/property/transform 五类注册与派发）是 headless 容器，已落地：类型定义在 L1 `context`，**实例由 `AppContext` owns、经 `interactions()` 取用**，`RuntimeHost` 提供注册转发。module 走 `host()`，GUI/测试走 `app().interactions()`。

### L4 `modules`（模块）/ `toolbox`（工具箱）

| 项 | 内容 |
|---|---|
| 定位 | 模块（基底功能）与工具箱（内部扩展，业务的承载者）——见顶部术语定标 |
| `modules` 放什么 | 编译期增删的功能模块：data / plot / script / protocol / analysis / settings / report；**无业务知识**，静态自注册、无法卸载、无运行时卸载概念 |
| `toolbox` 放什么 | 内部扩展（工具箱）：common / autopilot / automotive / robotics / gnss——扩展（纯数据清单 + 解析器），运行时 mount/unmount（装配入口 Toolbox/PluginScanner 在 host，**禁止**在 toolbox/ 下建装配或 registry 目录） |
| 禁放什么 | 彼此的实现细节、对方的私有头、具体格式分支跨包渗透 |
| 可依赖 | L0–L3、Qt（GUI 模块可依赖 Widgets）、本模块内的子目录 |
| 禁依赖 | **其他 module / 其他 extension**（同层横向，硬线）；L5 gui；L6 app |
| 隔离 | 模块/扩展之间只经 L3 host 的注册与意图协作，不直接 include |

### L5 `gui`

| 项 | 内容 |
|---|---|
| 定位 | GUI 外壳 + 功能视图——唯一的 Widgets 装配层 |
| 放什么 | MainWindow、Ribbon/Dock 容器、主题、参数表单渲染、视图工厂、各功能视图 |
| 禁放什么 | 业务格式解析、数据源生命周期、领域判断逻辑 |
| 可依赖 | L0–L4、Qt Widgets/Gui/Core |
| 禁依赖 | L6 app |
| 结构 | `shell/` 外壳装配 · `views/` 绘图视图 · 其余为共享 UI 基础设施 |

### L6 `app`

| 项 | 内容 |
|---|---|
| 定位 | 组合根——创建唯一实例、注册模块与扩展、装配启动 |
| 放什么 | `App`（持有 AppContext + RuntimeHost）、main 入口、启动顺序、命令行解析 |
| 禁放什么 | 业务逻辑、格式分支、具体 UI 构建 |
| 可依赖 | 全部下层 |
| 禁依赖 | 无（最顶层） |

---

## UI 归属与装载契约（模块/扩展如何写 UI，AI 必读）

**素材物理聚合，编译期分离**：一个功能（模块或扩展）的**全部素材**——headless 逻辑、意图处理器、专属面板、对话框、模型——物理上都放在自己的目录里（`application/modules/<x>/`、`toolbox/<x>/`），作为一个完整单元开发与查找；靠 CMake 拆成两个 target 实现 GUI/CLI 分离：

| target | 内容 | 链接面 |
|---|---|---|
| `<x>`（如 `ps_module_data`） | headless 部分，只链 Qt Core | CLI 与 GUI 都装载 |
| `<x>_gui`（如 `ps_module_data_gui`） | Qt Widgets 部分 | **只被 GUI 可执行文件链接**，CLI 二进制里物理不存在 |

**全局共享的放全局层**：意图契约 `libs/intent`、注册机制 `libs/registry`、运行态 `context`、系统面意图处理器 `host`、窗口骨架与外壳装配 `gui/shell/`（= `application/gui/shell/`）。功能私有的不外泄，全局的不许抄进功能目录。

**写 UI 的两条通道**（模块/扩展永不直接碰 MainWindow）：

1. **声明式**（IUiContext 契约，`libs/module`）：`onAttach` 里 `ui()->addAction(...) / declareSlot(...)`——只声明动作与面板位，零 Widgets 依赖
2. **工厂式**（PanelFactory，`libs/registry`）：视图 TU 里 `PS_REGISTER_PANEL(type, ...)` 静态自注册工厂函数；运行期 `ui()->requestPanel(area, type)` 按字符串创建、经 ShellBinder 放置。工厂签名带 `AppContext + IIntentExecutor`——**视图取数与 CLI/脚本走同一条意图路径**，没有第二条取数通道

**装载链**（谁装载）：GUI 可执行文件链接各 `*_gui` OBJECT 库 → 进程启动时静态注册对象构造、工厂表自动填好 → `host.activate()` → 模块 `onAttach` 请求面板/动作 → `GuiContext` 路由 → `PanelFactory::create` → `ShellBinder` 放置（带去重复用）。CLI 只链 headless 目标，UI 通道整体缺席。

**禁止**：

- headless target 链 Qt Widgets、include 任何 `*_gui` 头
- 绕过工厂表互相 include、直接 `new` 其他功能的面板（面板类型一律经工厂表解析）
- 业务面板放 `gui/`（领域面板形态归扩展在包内 `gui/` 子目录注册实现）
- 模块/扩展持有窗口对象、把运行态藏进 Widget

> 存量特例（禁止新增模仿，处置待裁定）：`ps_module_data_gui` 链 `ps_gui_theme`/`ps_gui_transform`（gui/ 下横向基础设施库）并 include `modules/plot/gui` 拖拽协议头——按矩阵 L4→L5 本应禁止，属历史过渡，方案（划 GUI 基础设施子层放行 或 迁 libs）待拍板。

## 支撑目录

| 目录 | 定位 | 约束 |
|---|---|---|
| `kernel/` | Rust 数据平面 | 数据本体、LOD、缓存、算子；只经 `include/ps_kernel.h` 一个 C ABI 头进入，无 Qt |
| `3rdparty/` | 第三方源码 | vendored 引入（SARibbon/ADS/Lua 等）；不修改其内部实现，不向外扩散其头文件 |
| `cmake/` | 构建期守卫脚本 | 只做静态可判定的边界检查；规则只加严不放宽 |
| `tests/` | 测试 target | 可依赖任意层做验证；不被任何生产 target 依赖 |

---

## 依赖矩阵（可依赖 = ✓，禁止 = ✗）

| from \ to | L0 | L1 | L3 host | L4 mod/ext | L5 gui | L6 app |
|---|:--:|:--:|:--:|:--:|:--:|:--:|
| L1 libs | ✓ | 同层受限 | ✗ | ✗ | ✗ | ✗ |
| L3 host | ✓ | ✓ | ✓ | ✗ | ✗ | ✗ |
| L4 mod/ext | ✓ | ✓ | ✓ | ✗ 横向 | ✗ | ✗ |
| L5 gui | ✓ | ✓ | ✓ | ✓ | ✓ | ✗ |
| L6 app | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |

L1 同层依赖按库定位单向：uischema_widgets → uischema、dataset → foundation 等，不允许反向。

---

## 新增代码判定顺序

1. 是纯值语义/契约、无状态？→ L0 `foundation` 或 L1 对应契约库（模块接口 → `module`）
2. 是可复用机制、调用方实例化即可？→ L1 对应库
3. 是进程级运行态或可序列化偏好？→ L1 `context`
4. 是协调"谁来做"/生命周期/路由？→ L3 `host`
5. 是可装卸能力或领域知识？→ L4 `modules` / `toolbox`
6. 是界面装配或视图？→ L5 `gui`
7. 是创建实例、装配启动？→ L6 `app`

判不清时按"依赖更低层能完成任务就放低层"处理。**禁止重建 core 或任何"暂存层"。**

---

## 强制机制

- **构建期守卫** `cmake/LayerContractGuard.cmake` 在 configure 期扫描 target 链接，跨层倒挂直接 `FATAL_ERROR`，编译未开始即红。
- 正则不可靠的项（服务定位器取用、namespace 语义、业务逻辑渗透）靠 code review + 架构测试 `ps_test_architecture`。
- 守卫规则**只许加严，不许为绕过加豁免**；确需跨界的，先改本契约再改守卫。
