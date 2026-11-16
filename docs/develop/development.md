# 开发约定

## 基本规则

PlotScope 按**功能模块**组织，不按 `ui/`、`src/` 这类技术类型分层。依赖方向固定：

```text
extensions（具体业务） → modules（通用能力） → shell（最小宿主）
```

箭头指向被依赖者。`shell` 不能反向 include 或 link `datasource`、`plotting`、`extensions`，也不持有数据源/画布/业务状态。

| 层 | 放什么 | 不能放什么 |
|---|---|---|
| `src/application/shell` | `MainWindow`、页签容器、Ribbon/Dock 骨架、命令、会话、日志、设置、扩展管理、主题入口 | 具体数据源、画布、业务格式、业务状态 |
| `src/application/modules/runtime` | 组件注册、扩展生命周期、通用贡献接口、动态扩展扫描/ABI | 具体业务格式 |
| `src/application/modules/datasource` | 数据源抽象、加载器注册、实时源、缓存、数据源 Dock | 画布、绘图页 |
| `src/application/modules/plotting` | 画布、绘图页/切分、轴/曲线、属性面板、脚本画布 | 具体文件格式解析 |
| `src/extensions/builtin` | 内置业务组件：GNSS、协议、设备调试、变换、视频、脚本 | 被 shell/modules 反向 include |
| `src/extensions/dynamic` | 动态扩展：各格式加载器、MAVLink/仿真数据源、SDK | 同上 |

## 目录定位

新增代码先看功能归属，再决定放哪：

- 数据源生命周期、加载器接口、数据源 Dock：`modules/datasource/{manager,dock,dialog,ribbon,services}`
- 画布、绘图页、切分布局、绘图 Ribbon：`modules/plotting/{canvas,tabs,manager,ribbon}`
- 组件/扩展协议、Registry、HostServices：`modules/runtime`
- 只和宿主有关：`shell`
- 具体协议/行业逻辑：`extensions`

不要为了“看起来整齐”把业务代码塞进 shell，也不要在 `modules` 里写具体格式分支。

## 命令

命令统一走 `CommandManager`：

```cpp
mw.commands().add(QStringLiteral("data.openPath"), [mw](const CommandContext &ctx) {
    // ...
});
```

- 命令 id 用 `域.动作`，例如 `file.open`、`layout.save`、`plot.new`。
- 显示文案、图标、快捷键、可勾选状态通过 `CommandSpec` 描述，Ribbon 只消费命令元数据。
- 命令实现放在对应模块；shell 只保留宿主级命令（文件、布局、设置、关于、截图等）。
- 模块不要直接 `new QAction` 绕过命令表；需要 Ribbon 按钮时注册 `RibbonItem` 并指向命令 id。

## Dock / Ribbon / 设置贡献

模块通过 `Registry` 贡献 UI，不直接操作 `MainWindow`：

- **Dock**：`DockContribution`，声明 `id/title/area/visible/order/icon/create`。
- **Ribbon**：`RibbonItem`，声明所属页、面板、命令 id、参数、图标、大小。
- **设置**：`SettingsContribution`，声明标题、图标、创建页面和 `apply` 回调。
- **窗口钩子**：需要创建页签或恢复状态的模块用窗口钩子，不把业务逻辑写进 shell。

`shell` 只负责把贡献表变成 Ribbon、Dock 和设置页；具体控件由模块创建。

## 扩展

扩展分两类：

- **内置扩展**：编译进应用，走 C++ 组件接口，可以直接创建 Qt 控件。
- **动态扩展**：独立动态库，导出 `PsExtensionDescriptor`，由 `ExtensionRuntime` 扫描、预检、加载、ABI 校验。

新增格式加载器时：

1. 在 `src/extensions/dynamic/data_loader/<format>` 实现 loader。
2. 用 `DataSourceRegistry` 注册格式、扩展名、探测函数和加载函数。
3. 不修改 `DataSourceDock` 的对话框分支；加载器由 schema 驱动。
4. 在 `tests/` 补格式夹具和解析断言。

扩展启停由 `ExtensionManager` 统一管理，禁用列表写配置；启动时跳过。详细接口见 [扩展模型](extensions.md) 与 [扩展点决策](extension-points.md)。

## 代码风格

- C++17，Qt 6 Widgets；避免引入新的 UI 框架。
- 注释只写必要约束和原因，不写“这里创建了一个……”这类过程说明。
- 头文件尽量前向声明，`cpp` 只 include 实际使用的头。
- 新增公共 API 先考虑是否真的通用；只在一个模块用的接口留在该模块。
- 路径、配置目录、日志目录统一走 `ShellPaths`；设置统一走 `SettingsManager`，不要在模块里直接 `QSettings`。

## 提交前

- `cmake --build build -j4` 通过。
- `ctest --test-dir build --output-on-failure` 通过。
- 改 UI 时做一次 GUI 冒烟并确认主界面没有布局/图标回归。
- 文档：用户可见行为写进 `docs/manual/`，架构变化写进 `docs/develop/`，未实施方案写进 `docs/roadmap/`。
