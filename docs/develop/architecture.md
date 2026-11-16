# 当前架构与装配

> 本文件是可直接使用的开发入门，不是历史决策记录。仓库级硬边界见 [`../../AGENTS.md`](../../AGENTS.md)；历史架构基线与决策在 [`../roadmap/architecture-v1.md`](../roadmap/architecture-v1.md)。

## 运行时组成

```text
plotscope_app
  ├─ ps_application_shell       宿主：窗口、命令、Dock/Ribbon、会话、设置、日志、扩展管理
  ├─ ps_application_datasource  数据源能力：注册、加载、实时源、缓存、数据源 Dock
  ├─ ps_application_plotting    绘图能力：画布、页签/切分、曲线、属性、绘图 Ribbon
  ├─ ps_host                    通用契约：Registry、IComponent、动态扩展、ABI bridge
  ├─ ps_kernel                  Rust 数据内核，C ABI（ps_kernel.h）
  ├─ ps_cache / ps_live         缓存与实时源服务
  └─ ps_builtin_components      内置扩展聚合
```

C++ 侧只通过 `ps_kernel.h` 调 Rust，不 include Rust 内部头。`ps-abi` 是唯一跨语言边界。

## 目录

| 路径 | 职责 |
|---|---|
| `src/application/main.cpp` | 组装：QApplication、扩展扫描、MainWindow、命令行 |
| `src/application/shell` | 最小宿主和管理内核，不含数据源/绘图业务 |
| `src/application/modules/runtime` | 宿主侧通用契约层，不承载业务 |
| `src/application/modules/datasource` | 数据源注册、加载器入口、实时源、缓存、数据源 UI |
| `src/application/modules/plotting` | 画布、绘图页、切分布局、属性面板、绘图 UI |
| `src/application/kernel` | Rust NDF、LOD、算子、对齐、C ABI |
| `src/extensions/builtin` | 内置 C++ 组件，随 app 编译 |
| `src/extensions/dynamic` | 动态扩展，运行时扫描、ABI 校验 |
| `src/application/resources` | 图标、脚本、运行时资源 |

完整依赖矩阵和禁止项以 `AGENTS.md` 为准。

## 启动装配

1. `main.cpp` 创建 `QApplication`，设置应用名/组织/版本，`ThemeManager::applyTo` 安装全局主题。
2. 校验 `ps_abi_version()` 与 `PS_ABI_VERSION`，不匹配直接拒启。
3. `ExtensionManager::scanStandardLocations()` 扫描动态扩展；`DataSourceRegistry::registerLoadedExtensions()` 与 `ps::components::registerDynamicExtensions()` 向注册表登记。
4. 构造 `MainWindow`：注入 `HostContext`，`ExtensionManager::activate()` 按禁用列表激活组件。
5. `MainWindow` 注册宿主命令（`shell::registerCommands`、`DockManager`、`SettingsManager`），创建页签容器，执行组件的 `windowHooks`（绘图模块在这里建立页签/画布）。
6. `RibbonManager` 按贡献表创建 Ribbon；`DockManager` 按贡献表创建 Dock；最后恢复窗口状态和布局。
7. 命令行带文件时，按 `.psl` / 数据文件分别触发 `layout.import` / `data.openPath`。

shell 只负责顺序和容器；业务页签、画布、数据源面板由模块在 hook 或贡献构造器里创建。

## 数据流

```text
文件/实时源
  → DataSourceRegistry / DataSourceSession
  → PsDataset（ps_kernel C ABI）
  → PlottingSession / PlottingCanvas
  → 画布绘制（LOD 由 ps_kernel 提供）
```

- `datasource` 负责把源变成 `PsDataset`，不碰画布。
- `plotting` 负责把通道画到画布，不解析文件。
- 两者需要互相操作时走 `ModuleBridge`，不直接 include 对方模块。

## 扩展装配

- 内置：静态注册到 `ExtensionRegistry`，实现 `IComponent::contribute`，向 `Registry` 添加贡献。
- 动态：`ExtensionRuntime` 扫描目录，校验 `PsExtensionDescriptor`/ABI，登记为组件；UI 只声明 `.ui`、JSON、宿主白名单控件，不跨边界返回 `QWidget*`。
- 扩展启停写入配置，重启生效；当前不支持热卸载。
- 贡献表由 `ExtensionHost` 持有，宿主按 `Registry` 创建 Dock/Ribbon/Settings/Dialog。

## 新增代码放哪

| 要加的东西 | 位置 |
|---|---|
| 文件格式、协议、设备调试 | `src/extensions/builtin` 或 `src/extensions/dynamic` |
| 数据源生命周期、加载器注册、数据源 Dock | `modules/datasource` |
| 画布、坐标轴、曲线、绘图页、绘图 Ribbon | `modules/plotting` |
| 组件契约、Registry、动态扩展 ABI | `modules/runtime` |
| 宿主窗口、命令表、Dock/Ribbon 容器、会话、设置、日志 | `shell` |
| Rust 解析/算子/对齐/LOD | `src/application/kernel` |

判断顺序：先问“这是业务还是宿主能力”，再问“属于哪个功能模块”，最后才决定目录。不要为了整齐把业务塞进 shell，也不要在 modules 里写具体格式分支。

## 验证

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$PWD/deps/Qt/6.2.4/gcc_64"
cmake --build build -j4
LD_LIBRARY_PATH="$PWD/deps/Qt/6.2.4/gcc_64/lib" ctest --test-dir build --output-on-failure
```

改 UI 追加一次 `xvfb-run` GUI 冒烟；改扩展追加对应格式夹具和解析断言。具体命令见 [`building.md`](building.md) 与 [`testing.md`](testing.md)。
