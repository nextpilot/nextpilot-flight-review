# PlotScope 架构边界

本文件是仓库级硬约束，不是建议。新增代码、扩展、文档和评审都先按这里判断归属；与 `docs/develop` 或历史架构文档冲突时，以本文件为准。

## 1. 总原则

按**功能模块**组织，不按 `ui/`、`src/` 这类技术类型分层。依赖方向固定为：

```text
extensions（具体业务）
        ↓
modules（通用能力）
        ↓
shell（最小宿主）
```

箭头表示“可以被谁依赖”。反向依赖、反向 include、反向 link 都不允许。

`src/application/modules/runtime` 是宿主侧通用契约层，不是业务模块。它只放组件注册、扩展生命周期、贡献接口、动态扩展扫描和 ABI，不放具体格式、画布、数据源 UI；shell 和 modules 都可以依赖它。

## 2. 目录与所有权

| 目录 | 所有者 | 放什么 | 禁止放什么 |
|---|---|---|---|
| `src/application/shell` | 宿主 | `MainWindow`、页签容器、Ribbon/Dock 管理器、命令、会话、日志、设置、主题入口、扩展管理、`UiDialogEngine` | 数据源、画布、绘图页、具体文件格式、业务状态、业务命令实现 |
| `src/application/modules/runtime` | 宿主契约 | `Registry`、`IComponent`、`HostContext`、动态扩展扫描/ABI、跨模块 bridge | 具体业务格式、业务 Dock/画布、业务命令实现 |
| `src/application/modules/datasource` | 数据源能力 | 数据源注册、加载器入口、实时源、缓存、数据源 Dock/对话框、`data.*` 命令 | 画布、绘图页、属性面板、绘图状态 |
| `src/application/modules/plotting` | 绘图能力 | 画布、绘图页/切分、坐标轴、曲线、属性面板、绘图 Ribbon、`plot.*`/`page.*` 命令 | 具体文件格式解析、数据源生命周期 |
| `src/application/modules/protocol` | 协议能力 | 协议 schema/文档模型、协议编辑器、codec、自定义协议 loader、`protocol.*` 命令与 Ribbon | 画布、数据源会话状态、具体业务格式分支 |
| `src/application/kernel` | Rust 内核 | NDF、LOD、算子、对齐、C ABI | Qt、Widgets、宿主业务 |
| `src/extensions/builtin` | 内置业务 | GNSS、设备调试、变换、视频、脚本等 C++ 组件 | 反向被 shell/modules include；直接依赖 shell |
| `src/extensions/dynamic` | 动态扩展 | 官方格式加载器、MAVLink/仿真数据源、SDK | 依赖 shell/modules 私有头；跨 C ABI 返回 `QWidget*` |
| `src/application/main.cpp` | 组装层 | 创建 `QApplication`、扫描扩展、创建 `MainWindow`、处理命令行 | 业务逻辑、格式分支、业务 UI |

## 3. 允许的依赖

| 使用方 | 允许依赖 | 禁止依赖 |
|---|---|---|
| `shell` | `modules/runtime`、`ps_kernel` 的 C ABI、Qt、SARibbon | `datasource`、`plotting`、`extensions/builtin`、具体业务格式 |
| `modules/runtime` | `ps_kernel` 的 C ABI、Qt Core | 具体业务模块、shell 的 `MainWindow` |
| `modules/datasource` | `shell` 的宿主接口、`modules/runtime`、`ps_kernel` 的 C ABI、cache/live | `modules/plotting` 私有头；绘图状态 |
| `modules/plotting` | `shell` 的宿主接口、`modules/runtime`、`ps_kernel` 的 C ABI | `modules/datasource` 私有头；格式解析 |
| `modules/protocol` | `shell` 的宿主接口、`modules/runtime`、`ps_kernel` 的 C ABI、`ps_loader_common`（加载器公共代码）、Qt | `modules/datasource`/`plotting` 私有头；画布与绘图状态 |
| `extensions/builtin` | `modules/runtime`、modules 的公开头、`ps_kernel` 的 C ABI、Qt | `shell` 的 `MainWindow` 或 shell 私有头 |
| `extensions/dynamic` | 公开 SDK、`ps_extension.h`、`ps_kernel.h`、C ABI | shell/modules 私有头、Qt Widgets、C++ 对象跨边界 |

跨业务模块只走 `modules/runtime/include/ModuleBridge.h` 的公开函数和数据，不直接 include 对方模块。

## 4. shell 的硬边界

shell 只做最小宿主和管理内核：

- 拥有窗口、页签容器、Ribbon/Dock 骨架、命令表、会话、设置、日志、主题、扩展激活和通用 `.ui` 对话框。
- 只注册宿主命令：`app.*`、`file.*`、`layout.*`、`view.*`、`help.*` 和设置入口。
- 不注册、不调用 `data.*`、`plot.*`、`page.*` 等业务命令。
- 不按命令 id 前缀判断业务语义；命令是否改变会话由 `CommandSpec` 元数据声明。
- 不创建业务面板、业务画布、格式解析器；业务 UI 由模块贡献。
- 不直接 `QSettings`；统一走 `SettingsManager`。
- 不直接持有 `DataSourceSession`、`PlottingSession`、`PlottingCanvas` 或扩展业务对象。
- `MainWindow` 只做装配和宿主生命周期，不成为业务方法的转发中心。

## 5. modules 的硬边界

- `datasource` 和 `plotting` 各自拥有自己的状态、命令、Ribbon/Dock 贡献和测试。
- 两个业务模块不互相 include；需要跨模块操作时，只通过 `ModuleBridge` 或 `Registry`。
- 模块不把业务格式写进 shell；新增格式只进 `extensions` 或 datasource 的注册表。
- 模块不直接 `new QAction`、`addCategoryPage`、`addDockWidget` 绕过 `CommandManager` / `RibbonManager` / `DockManager`。
- 模块的命令实现留在模块内；shell 只消费命令元数据。
- 模块的设置页通过 `SettingsContribution` 注册，不直接改设置对话框。

## 6. extensions 的硬边界

### 内置扩展

- 随 app 编译，实现 `ps::components::IComponent`，静态注册到 `ExtensionRegistry`。
- 可以创建 Qt 控件，但只能拿 `HostContext`，不能把 `MainWindow*` 传进组件。
- 只能依赖 modules 的公开头，不依赖 shell 私有头。
- 一个扩展目录一个 target；由 `ps_builtin_components` 聚合，删掉扩展后 app 仍可构建。

### 动态扩展

- 独立动态库，只导出 `PsExtensionDescriptor` 和公开 ABI；manifest、ABI 版本、可选 vtable 由宿主校验。
- 不跨边界返回 `QWidget*`；UI 只能声明 `.ui` / JSON / 宿主白名单控件，由宿主创建。
- 不 include `shell` 或 `modules` 的私有头；只用公开 SDK 和 `ps_kernel.h`。
- 不加载任意 `.so` 路径，不执行 manifest 中的脚本，不信任未校验的 widget/action 名称。
- 动态扩展的启停写入配置，重启生效；当前没有热卸载/热重载。

## 7. 贡献与命令

统一走 `Registry`：

- `addProtocol` / `addDataSource` / `addRibbon` / `addDock` / `addDialog` / `addSettings` / `addAction` / `addScriptRunner` / `addWindowHook`
- 贡献项由创建它的模块/扩展拥有；宿主只负责遍历和创建容器。
- 动态扩展的 UI 由宿主按 manifest 创建，扩展不持有宿主控件生命周期。

命令规则：

- 命令 id 使用 `域.动作`，域归功能所有者：`data.*` 归 datasource，`plot.*` / `page.*` 归 plotting，扩展使用自己的域。
- 命令的显示文案、图标、快捷键、可勾选状态和 dirty 语义由 `CommandSpec` 声明。
- shell 不按 id 前缀推断业务；Ribbon 不硬编码业务按钮。
- 命令实现失败必须可返回失败，不能把“保存失败”当成 clean。

## 8. 生命周期

- 启动时扫描动态扩展，`ExtensionManager::activate` 按禁用列表激活内置/动态组件。
- `Registry` 的生命周期跟随 `ExtensionHost`；当前没有按扩展卸载的能力。
- 动态扩展启用/禁用写配置，提示重启；不要在代码里假装支持热插拔。
- 模块的窗口钩子负责建立自己的页签/面板；shell 不反向调用业务初始化命令。
- 关闭时由宿主处理会话/布局保存，业务状态由对应模块贡献保存逻辑。

## 9. CMake 边界

- `ps_application_shell` 不得 link `ps_application_datasource`、`ps_application_plotting`、`ps_builtin_components`。
- `ps_application_datasource` / `ps_application_plotting` 可以 link shell/runtime/kernel，但不得互相 link。
- `ps_application_protocol` 只由 `plotscope_app` 链接，不进入 shell 或 datasource/plotting 的依赖；协议 loader 仍通过数据源注册表暴露。
- `ps_builtin_components` 只聚合内置扩展，不进入 shell 的依赖。
- `plotscope_app` 是唯一组装 target，链接 shell + modules + builtin；动态扩展在运行时加载。
- 新增 target 必须显式声明 warnings 和 include 边界；shell 也必须纳入 `ps_set_warnings`。

## 10. 文档边界

- 本文件：仓库级硬边界和评审清单。
- `docs/develop/`：已成熟、可以直接照着做的开发指南；不得放草稿、讨论、未定稿方案、决策记录或历史实施记录。
- `docs/roadmap/`：未实施方案、讨论稿、决策记录、历史架构基线。
- `docs/manual/`：面向用户的使用说明。
- 任何架构边界变化先改本文件，再改代码和开发文档；不能只改文档不改边界，也不能只改代码不改文档。

## 11. 当前已知例外（只减不增）

当前无已知例外。新增债务必须先修复，不要往本清单里加。


## 12. 提交前检查

- 新增 include 不跨越本文件的依赖矩阵。
- shell 没有新增业务命令、业务状态、业务面板或具体格式分支。
- 内置扩展没有新增 `MainWindow*` / shell 私有头依赖。
- 动态扩展没有新增 Qt Widgets 或 `QWidget*` 跨边界。
- 新模块/扩展有对应测试；改 UI 做 GUI 冒烟；`ctest` 全过。
- 文档归位正确：成熟指南在 `docs/develop/`，方案/决策在 `docs/roadmap/`。
