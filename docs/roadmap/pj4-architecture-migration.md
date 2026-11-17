# PJ4 架构迁移方案

> 状态：B1–B4 已实施（2026-11-18）。B2/B3 完成 core/widget 物理拆分，`DataSourceController/Service`、`PlottingController/Service` 注册到 `ServiceRegistry`，core 目录不含 Widgets/主窗口依赖；B4 完成 SettingsStore/LogService/SessionState 下沉、ExtensionHost 下沉 runtime、CommandBus 最小契约和模块/内置 CMake option。B5 默认构建与 `ctest 42/42` 通过；13 组可删除性矩阵全部构建通过并完成 offscreen 启动冒烟。
>
> 范围：把 PlotScope 从“MainWindow 兼做窗口壳和业务服务中心”调整为“shell 只组装、modules 自贡献、extensions 可裁剪、runtime 无 Widgets”的结构。先改依赖和所有权，不改 UI 行为、不改命令 ID、不一次性搬完所有代码。
>
> 关系：本方案接替 [`shell-architecture-v2.md`](shell-architecture-v2.md) 的 Phase B。该文 Phase A（shell 内部断环）已实施；Phase B 的 `Core/Workspace/ProjectContext` 不再作为目标，统一按本文的 `ps_runtime + ModuleContext` 落地。
>
> 参考：[`pj4-alignment.md`](pj4-alignment.md) 是能力矩阵；本文只讲架构迁移。

---

## 1. 目标与非目标

### 1.1 目标

1. `MainWindow` 不再写业务：只建壳、建 `AppSession`、激活组件。
2. `modules` 不再拿 `MainWindow*`，改拿 `ModuleContext&` 和公开服务接口。
3. `modules` 之间不互相 include、不互相 link，跨模块只走 `ServiceRegistry`。
4. 每个 module / extension 可以单独关闭；删掉一个，进程仍能启动，其余功能降级运行。
5. `runtime` 不链接 `Qt6::Widgets`；数据层继续由 `ps_kernel` 承担。
6. 设置统一走 `SettingsStore`，业务代码不直接 `QSettings`。

### 1.2 非目标

- 不照搬 PJ4 的 13 个模块；只借它的分层和依赖方向。
- 不现在引入 `AppContext`。PJ4 自己也是 `MainWindow` 持有 `AppSession`，当前产品 `MainWindow` 寿命 ≈ 进程寿命，先不造第二层生命周期。
- 不做多窗口、关窗不退出、无界面 CLI 的进程级重构；这些等真实需求出现再上移 `AppSession`。
- 不重写画布、数据源、解析器；只拆接口和所有权。
- 不为了目录整齐大规模搬文件；文件跟着接口走。

---

## 2. 当前问题

### 2.1 MainWindow 仍是业务中心

`MainWindow` 持有 `ExtensionManager`、`CommandManager`、`SessionManager`、`LogManager`、`DockManager`、`PropertyManager`、`RibbonManager`，模块通过 `windowHooks` 拿 `MainWindow*` 后反向调用这些 Manager。

结果：

```text
modules -> shell -> MainWindow -> 各 Manager
```

这和 `AGENTS.md` 的 `extensions -> modules -> shell` 方向相反。

### 2.2 模块 API 里到处是 `MainWindow*`

当前事实：

- `Registry::windowHooks` 是 `std::function<void(QWidget*)>`；`MainWindow` 调 `hook(this)`。
- `DataSourceModule`、`PlottingModule` 在 hook 里 `qobject_cast<MainWindow*>`。
- `DataSourceSession`、`PlottingSession` 构造函数是 `MainWindow *mw`，成员 `m_mw`。
- `DataSourceRibbon` 用 `mw->findChild<QDockWidget*>("dock.src")`、`mw->resizeDocks`。
- `AnalysisRibbon`、`PlottingRibbon` 用 `mw->commands()`、`mw->addDockWidget()`、`mw->screen()`。
- `SettingsContribution::create(QWidget*)`、`DockContribution::create(QWidget*)` 里 cast 成 `MainWindow*`。
- `ModuleBridge.h` 大量自由函数以 `MainWindow*` 为第一参数。

`CommandContext` 只有 `{ QWidget* parent, QString arg }`，命令层相对干净，可作为改造起点。

### 2.3 CMake 硬链接，模块删不掉

`plotscope_app` 无条件链接：

```text
ps_application_shell
ps_application_datasource
ps_application_plotting
ps_application_analysis
ps_application_scripting
ps_application_protocol
ps_builtin_components
```

`src/application/modules/CMakeLists.txt` 也无条件 `add_subdirectory`。  
所以“删掉一个模块”目前不是功能降级，而是构建失败。

### 2.4 命名混用

当前同时存在 `HostContext`、`ModuleBridge`、`DataSourceSession`、`PlottingSession`、各种 `Manager`，同一职责有多个名字。落地时按本文第 5 节统一。

---

## 3. PJ4 的事实基线

PJ4 的关键不是模块数量，是四条硬方向：

1. `pj_app` 只是可执行壳：`MainWindow`、菜单、对话框、服务接线；不放业务逻辑。
2. `pj_runtime` 是服务层，禁止 `Qt6::Widgets`；会话、目录、回放、扩展、派生服务在这里。
3. `pj_datastore` 是纯 C++ 列存，不依赖 Qt、不依赖插件；负责数据、对象、增量变换。
4. 外部插件走 C ABI + service registry，不拿 `MainWindow*`；内部 widget family 拿 runtime 服务引用和 `IDataWidget` 契约。

PJ4 桌面版的 `MainWindow` 仍然持有 `AppSession`：

```cpp
std::unique_ptr<AppSession> session_;
```

所以“进程比 UI 长”不是 PJ4 当前形态；它只是把服务放进 UI-free 的 `pj_runtime`，真需要时再把 `AppSession` 上移。

PlotScope 的映射：

| PJ4 | PlotScope |
|---|---|
| `pj_datastore` | `ps_kernel` + 各模块 `core` 的数据模型 |
| `pj_runtime` | 新增 `ps_runtime`，无 Widgets |
| `pj_app` | `shell` + `main.cpp`，只组装 |
| `pj_plotting` | `modules/plotting/core + widget` |
| `pj_widgets` | `application/shared` 可复用控件 |
| `plotjuggler_sdk` | `ps_host` + `extensions/dynamic/sdk` |

---

## 4. 目标结构

```text
src/application/
├─ main.cpp                    # 只组装
├─ runtime/                    # ps_runtime：无 Qt Widgets
│  ├─ AppSession
│  ├─ ModuleContext
│  ├─ ServiceRegistry
│  ├─ CommandBus
│  ├─ SettingsStore
│  ├─ LogService
│  └─ ExtensionHost
├─ shell/                      # 窗口级 UI
│  ├─ MainWindow
│  ├─ DockManager
│  ├─ RibbonManager
│  ├─ PropertyManager
│  ├─ ThemeManager
│  ├─ SettingsDialog
│  └─ LogDock
├─ modules/
│  ├─ datasource/
│  │  ├─ core/                 # 注册、加载、解析、通道、书签，无 QWidget
│  │  └─ widget/               # Dock、对话框、Ribbon
│  ├─ plotting/
│  │  ├─ core/                 # 页面/曲线模型、LOD 请求，无 QWidget
│  │  └─ widget/               # Canvas、页面、属性、Ribbon
│  ├─ analysis/
│  │  ├─ core/                 # AnalysisRegistry、报告模型
│  │  └─ widget/               # 分析 Dock/Ribbon
│  ├─ protocol/
│  └─ scripting/
├─ kernel/                     # Rust datastore
├─ shared/                     # 可复用 UI 控件、filter UI
└─ extensions/
   ├─ builtin/{autopilot,gnss,automotive,robotics,common}
   └─ dynamic/sdk
```

### 4.1 目标 target 图

```text
ps_kernel                         Rust，C ABI
ps_host                           扩展 ABI、Registry、IComponent、动态扩展，无 Widgets
ps_runtime                        服务层，Qt Core only，无 Widgets

ps_module_datasource_core         -> runtime/host/kernel
ps_module_datasource_widget       -> datasource_core + runtime + Qt Widgets
ps_module_plotting_core           -> runtime/host/kernel
ps_module_plotting_widget         -> plotting_core + runtime + Qt Widgets
ps_module_analysis_core           -> runtime/host
ps_module_analysis_widget         -> analysis_core + runtime + Qt Widgets

ps_application_shell              -> runtime/host + Qt Widgets
plotscope_app                     -> shell + 启用的 modules + 启用的 extensions
```

初期不必一次拆出全部 `core/widget` target。先保持 `ps_application_datasource`、`ps_application_plotting` 两个 OBJECT target，内部按 `core/`、`widget/` 分目录；等接口稳定后再拆 target。

### 4.2 所有权

```text
MainWindow
 ├─ AppSession                 进程级服务集合
 ├─ ModuleContext              递给模块的依赖视图
 ├─ ExtensionHost              激活组件
 └─ Shell UI                   Dock/Ribbon/Property/Theme

ModuleContext
 ├─ AppSession&
 ├─ ServiceRegistry&
 ├─ CommandBus&
 ├─ SettingsStore&
 ├─ LogService&
 └─ UiHost*                    只有 UI 模块用
```

`AppSession` 不拥有具体业务模块的 Controller。模块激活时自己创建 Controller，并注册到 `ServiceRegistry`；模块不存在时服务为空，消费方降级。

---

## 5. 命名约定

| 名字 | 职责 | 不该做什么 |
|---|---|---|
| `Manager` | 管一个子系统的生命周期和资源 | 不要变成业务大杂烩 |
| `Service` | 对外提供能力，通常有接口 | 不拥有窗口、不写 UI |
| `Controller` | 协调一个业务流程，持有流程状态 | 不直接 `new QWidget` |
| `Session` | 一次有边界的工作期状态 | 不要用来命名长期子系统 |
| `Context` | 依赖注入的窄视图 | 不拥有重状态 |
| `Bus` | 命令/事件路由 | 不写业务逻辑 |
| `Store` | 只存状态 | 不协调流程 |
| `Registry` | 登记和查找 | 不执行 |
| `Host` | 给客人提供环境 | 不要每个业务都叫 Host |

### 5.1 本次统一

| 现在 | 目标 |
|---|---|
| `HostContext`（C++ 模块用） | `ModuleContext` |
| `UiHost` | 保留，作为 `ModuleContext::ui` |
| `ExtensionHost` | 保留，不传给模块 |
| `HostApi` / `ps_extension_api.h` | 保留，只给 dynamic C ABI |
| `ps_host` | target 名不改；文档注明“扩展契约库”，不是 MainWindow |
| `DataSourceSession` | `DataSourceController` |
| `PlottingSession` | `PlottingController` |
| `SettingsManager`（无 UI 部分） | `SettingsStore` |
| `LogManager` | 拆 `LogService` + `LogDock` |
| `DockManager` / `RibbonManager` | 保留，不再造 `DockHost` / `RibbonHost` |

原则：一个概念只留一个名字；不要同时保留 `HostContext` 和 `ModuleContext` 两套入口。

---

## 6. 模块与扩展的可删除性

“删除一个模块不影响运行”分两层：

### 6.1 构建期删除

每个 module / extension 一个 CMake option：

```cmake
option(PS_MODULE_DATASOURCE "数据源模块" ON)
option(PS_MODULE_PLOTTING   "绘图模块"   ON)
option(PS_MODULE_ANALYSIS   "分析模块"   ON)

if(PS_MODULE_DATASOURCE)
  add_subdirectory(datasource)
  list(APPEND PS_MODULE_TARGETS ps_application_datasource)
endif()
```

`plotscope_app` 只链接启用列表：

```cmake
target_link_libraries(plotscope_app PRIVATE
  ps_application_shell
  ${PS_MODULE_TARGETS}
)
```

不要再写死 `ps_application_datasource` / `ps_application_plotting`。

### 6.2 运行期降级

模块服务为空时，消费方必须检查并降级：

```cpp
auto *ds = ctx.services.find<DataSourceService>();
if (!ds) {
    return; // 数据源模块未安装：绘图进入空状态
}
```

| 删掉的模块 | 运行结果 |
|---|---|
| datasource | 绘图空状态，不能打开数据，不崩 |
| plotting | 数据源树正常，没有画布，不崩 |
| analysis | 分析 Ribbon 空，不崩 |
| autopilot 扩展 | 文件对话框不显示 ULog/apbin，不崩 |
| gnss 扩展 | 不显示 NMEA/UBX/OEM7，不崩 |
| common 扩展 | 不显示 CSV/JSON/binary，不崩 |

### 6.3 extension 同样处理

```cmake
option(PS_BUILTIN_AUTOPILOT ON)
option(PS_BUILTIN_GNSS      ON)
option(PS_BUILTIN_AUTOMOTIVE ON)
option(PS_BUILTIN_ROBOTICS  ON)
```

`ps_builtin_components` 只聚合启用项；`plotscope_app` 不直接引用 `autopilot`、`gnss` 的类名。组件通过 `IComponent::contribute` 注册数据源和 parser；target 不存在时注册表里就没有这些项。

---

## 7. 实施阶段

### B0：冻结边界与文档

1. 本文件进 `docs/roadmap/README.md`。
2. 落地第一批代码前，先更新 `AGENTS.md`：
   - `modules` 不得 include `shell` 私有头；
   - `runtime` 不得链接 `Qt6::Widgets`；
   - `shell` 不引用具体模块类名；
   - 模块/扩展 target 必须可单独关闭。
3. 标注 `shell-architecture-v2.md` 的 Phase B 由本文件接替。

验收：文档中的 target 图、依赖矩阵、可删除性规则一致。

### B1：`ps_runtime` + `ModuleContext` + `ServiceRegistry`

这是最小改动，不移动业务代码。

新增：

```text
src/application/runtime/
├─ AppSession.h/.cpp
├─ ModuleContext.h
├─ ServiceRegistry.h/.cpp
├─ CommandBus.h
├─ SettingsStore.h
├─ LogService.h
└─ UiHost.h
```

改动：

1. `Registry::windowHooks` 从 `std::function<void(QWidget*)>` 改为 `std::function<void(ModuleContext&)>`。
2. `DataSourceModule`、`PlottingModule` 的 hook 不再 `qobject_cast<MainWindow*>`。
3. `ModuleBridge.h` 去掉 `MainWindow*` 参数，改成 `ModuleContext&` 或 `ServiceRegistry&`。
4. `DataSourceRibbon`、`PlottingRibbon`、`AnalysisRibbon` 通过 `ModuleContext` 调 `commands()`、`docks()`、`screen()`，不再直接拿 MainWindow。
5. `SettingsContribution::create`、`DockContribution::create` 不再 cast `MainWindow*`，改为 `UiHost&`。
6. `MainWindow` 只构造 `ModuleContext` 并传给 `ExtensionHost`。

验收：

```bash
rg 'MainWindow' src/application/modules src/extensions/builtin
```

目标：模块和内置扩展里不再出现 `MainWindow*`、`qobject_cast<MainWindow*>`、`#include "MainWindow.h"`。

### B2：拆 `datasource`

把 `DataSourceSession` 拆成：

```text
modules/datasource/core/
├─ DataSourceController        # 原 Session 的数据部分，无 QWidget
├─ DataSourceService           # 对外只读接口
├─ MessageParserRegistry
├─ TimeBookmarkStore
└─ SourceConfigStore

modules/datasource/widget/
├─ DataSourceDock
├─ DataSourcePanel
├─ ProtocolInspectorDock
├─ DataSourceDialog
└─ DataSourceRibbon
```

要求：

- `DataSourceController` 不继承 `QWidget`，构造函数为 `DataSourceController(ModuleContext&)`。
- 不再用 `findChild("ps.dataSourceSession")` 找隐藏单实例；Controller 由模块组件创建并注册到 `ServiceRegistry`。
- `DataSourceDock` 只做视图，数据查询走 `DataSourceService`。
- `DataSourceRibbon` 不再 `mw->findChild<QDockWidget*>("dock.src")`，Dock 操作走 `UiHost`。

验收：datasource 的 `core/` 目录不出现 `QWidget`、`MainWindow`、`QSettings`。

### B3：拆 `plotting`

```text
modules/plotting/core/
├─ PlottingController          # 页面/绘制流程，无 QWidget
├─ PlotPageModel
├─ PlotSeriesModel
└─ PlottingService

modules/plotting/widget/
├─ PlottingCanvas
├─ PlottingPage
├─ PlottingTabs
├─ PlottingSettingsPage
└─ PlottingRibbon
```

要求：

- `PlottingController` 不继承 `QWidget`，不再 `m_mw->pages()/properties()/commands()/logs()`。
- 页面模型只存 ID/状态，不存 `QWidget*`；`PlottingPage` 由 widget 层按模型创建。
- `PlottingRibbon` 通过 `ModuleContext.commands()`、`UiHost.screen()` 操作。

验收：plotting 的 `core/` 目录不出现 `QWidget`、`MainWindow`。

### B4：shell 服务归位与 extension option 化

| 现在 | 目标 |
|---|---|
| `SettingsManager` 的持久化部分 | `runtime/SettingsStore` |
| `SettingsDialog` | `shell/SettingsDialog` |
| `LogManager` | `runtime/LogService` + `shell/LogDock` |
| `SessionManager` 的最近文件/设置 | `runtime` |
| `SessionManager` 的窗口几何/布局 | `shell` |
| `CommandManager` | `shell`，实现 `CommandBus` |
| `ThemeManager` | `shell`，UI 模块通过 `UiHost.theme()` 取 token |
| `ExtensionManager` | `runtime/ExtensionHost` |
| `DockManager` / `RibbonManager` / `PropertyManager` | `shell` |

同时给 module / builtin extension 加 CMake option，`plotscope_app` 改为链接启用列表。

验收：`ps_application_shell` 不链接 datasource/plotting；`ps_runtime` 不链接 `Qt6::Widgets`。

### B5：可删除性验收

新增构建矩阵：

1. 只开 `shell + runtime`：app 能启动，空窗口可用。
2. 只开 `datasource`：能打开文件，无画布。
3. 只开 `plotting`：空画布可用，无数据源。
4. 只开 `autopilot`：ULog/apbin/tlog 出现在文件对话框。
5. 任意一个 module / extension 关闭：构建通过，启动不崩。

---

## 8. CMake 边界

### 8.1 允许的依赖

| 使用方 | 允许依赖 | 禁止依赖 |
|---|---|---|
| `ps_runtime` | `ps_host`、`ps_kernel` C ABI、Qt Core | `Qt6::Widgets`、shell、具体业务模块 |
| `ps_application_shell` | `ps_runtime`、`ps_host`、Qt Widgets、SARibbon、ADS | datasource/plotting/analysis 私有头、具体业务类名 |
| `modules/*/core` | `ps_runtime`、`ps_host`、`ps_kernel`、Qt Core | `Qt6::Widgets`、shell、其他模块私有头 |
| `modules/*/widget` | 本模块 core、`ps_runtime`、`ps_host`、Qt Widgets、SARibbon、ADS | shell 私有头、其他模块私有头 |
| `extensions/builtin` | `ps_runtime`、`ps_host`、modules 公开头、Qt | shell 私有头、`MainWindow*` |
| `extensions/dynamic` | 公开 SDK、C ABI | shell/modules 私有头、Qt Widgets |

### 8.2 检查命令

```bash
# 模块和内置扩展不得出现 MainWindow
rg 'MainWindow' src/application/modules src/extensions/builtin

# 模块不得 include shell
rg '#include .*shell' src/application/modules

# runtime 不得链接 Widgets
rg 'Qt6::Widgets' src/application/runtime/CMakeLists.txt

# shell 不得链接业务模块
rg 'ps_application_(datasource|plotting|analysis|scripting|protocol)' \
   src/application/shell/CMakeLists.txt
```

---

## 8.3 实施结果（2026-11-18）

| 项 | 结果 |
|---|---|
| datasource core | `core/DataSourceController`、`DataSourceService`、`core/parser/*`；`DataSourceModel` 不再持有 `QWidget* row`，行控件留在 widget/session |
| datasource widget | `widget/session|dock|dialog|ribbon`；Session 创建 Controller/Service 并注册，析构清注册 |
| plotting core | `PlottingController`（页面 ID/标题/状态）、`PlottingService`；不持有 Widget |
| plotting widget | `widget/session|settings|tabs|canvas|ribbon`；页签创建/关闭/切换同步 Controller |
| B4 | `runtime/SessionState` 管最近文件/layoutPath；`runtime/ExtensionHost`；`runtime/CommandBus` 契约；shell 只管窗口几何与 UI |
| 默认构建 | `plotscope_app`、`plot_bench`、测试全部 Built |
| 测试 | `ctest 42/42` 通过，新增 `controller_state` |
| 矩阵 | 13 组全部通过（exit=124 进入事件循环）：5 个 module 逐个关闭、4 个 builtin 逐个关闭、全关只留 shell+runtime、仅 datasource、仅 plotting、仅 autopilot |
| 依赖收敛 | datasource 关闭时自动裁掉 common/GNSS/automotive/robotics/protocol；common 关闭时自动裁掉依赖 loader_common 的扩展 |
| Dock 只读查询 | Dock 的源数量/当前源/源列表查询走 `DataSourceService`，写操作仍由 Context 回调转到 Session |

`DataSourceDock` 的只读查询已走 `DataSourceService`；加载/连接/删除等写操作仍通过 `Context` 回调转到 Session。把整张回调表收口成 Service + Command 还需要单独一轮，不影响本轮可删除性验收。

## 9. 验收

```bash
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

追加：

1. `B1` 后：模块无 `MainWindow` 引用，GUI 冒烟通过。
2. `B2` 后：数据源打开、实时源、书签、Dock 行为不变。
3. `B3` 后：画布、页签、属性、Ribbon 行为不变。
4. `B4` 后：设置、日志、布局保存行为不变。
5. `B5` 后：逐模块关闭的构建矩阵全部通过。

---

## 10. 风险与对策

| 风险 | 对策 |
|---|---|
| `DataSourceSession` 代码量大，拆分回归面大 | B1 先只换接口；B2 只拆数据/视图，不改解析逻辑 |
| `ModuleBridge` 被多处调用 | 先保留函数名，只换参数类型；再逐个改成服务查询 |
| 模块删除后 UI 残留 | 所有 UI 都由贡献表创建；服务为空时贡献直接不注册 |
| `SettingsManager` 迁移影响面大 | 先保留 `SettingsManager` 外观，内部委托 `SettingsStore`；稳定后再删壳 |
| 命名反复 | 以本文第 5 节为唯一命名表；新代码不再引入 `HostContext` 第二套 |

---

## 11. 不建议现在做

- 不造 `AppContext`；`MainWindow` 继续持有 `AppSession`。
- 不拆 PJ4 式 13 个模块；先做 6 层。
- 不一次性把所有 `DataSourceSession` / `PlottingSession` 改成 Controller。
- 不改 UI 行为、命令 ID、布局格式。
- 不为目录好看搬文件；先改接口和依赖。

---

## 12. 第一批提交建议

只做 B1，不改业务行为：

1. 新增 `ps_runtime` 和 `ModuleContext` / `ServiceRegistry`。
2. `Registry::windowHooks` 改签名。
3. `DataSourceModule` / `PlottingModule` 去掉 `MainWindow*` cast。
4. `ModuleBridge` 参数从 `MainWindow*` 改为 `ModuleContext&`。
5. 编译 + `ctest` + GUI 冒烟。

这批提交完成后，`MainWindow` 不再是模块的依赖入口；后续拆 datasource/plotting 才不会被接口和搬迁同时拖住。
