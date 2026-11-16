# 主题与浅色模式

- 状态：已实施（2026-11-16）
- 相关代码：`src/application/shell/ThemeManager.*`、`SettingsDialog.cpp`、`MainWindow.*`、`RibbonManager.*`、`src/application/modules/plotting/canvas/PlottingCanvas.cpp`、`src/application/modules/plotting/ribbon/PlottingRibbon.cpp`、`src/application/modules/datasource/dock/DataSourceDock.cpp`
- 截图：`../screenshots/PlotScope-Theme-Dark.png`、`../screenshots/PlotScope-Theme-Light.png`

## 结果

默认仍是原有深色界面；设置 → 通用 → 外观可切到浅色，切换立即生效，选择写入 `NextPilot/PlotScope` 的 `theme`，重启后保持。

| 区域 | 改动 |
|---|---|
| 全局 QSS / palette | `ThemeManager` 建立深/浅两套语义 token，统一生成样式表与 palette |
| 设置页 | 通用页增加“外观：深色/浅色”，Apply/OK 时刷新主题并 `sync()` |
| Ribbon | `RibbonManager::applyTheme()` 深色用 `RibbonThemeDark`，浅色用 `RibbonThemeOffice2021Blue`，再叠加 token 映射后的自定义 QSS |
| 画布 | 背景、网格、文字、次文字取 token；曲线色板取 `seriesColor()`；游标、测量框、提示框颜色改用 `color()` / `alphaColor()` |
| 数据源 Dock | 启动时统一转换局部深色 QSS；运行中新加的数据源行在创建后刷新样式 |
| 扩展/其他对话框 | 使用宿主 palette；模块自有 QSS 通过 `mapStyleSheet()` 或 `refreshWidgetStyles()` 跟随主题 |

## Token

`ThemeManager::color(token)` 返回当前主题颜色，`alphaColor(token, alpha)` 返回带透明度颜色，`seriesColor(index)` 返回曲线色板。

| token | Dark | Light |
|---|---|---|
| `bg` | `#0d1117` | `#f4f6f8` |
| `dialog` | `#0f1419` | `#ffffff` |
| `panel` | `#161b20` | `#ffffff` |
| `panelAlt` | `#1a2127` | `#eef1f4` |
| `input` | `#0b0f13` | `#ffffff` |
| `border` | `#232b33` | `#d5dce3` |
| `line` | `#2d3841` | `#b9c4cf` |
| `text` | `#c9d1d9` | `#1f2933` |
| `textDim` | `#8494a0` | `#5b6875` |
| `textMuted` | `#6e7681` | `#64748b` |
| `accent` | `#f2b134` | `#b45309` |
| `hover` | `#1b232a` | `#e8edf2` |
| `selection` | `#1a2127` | `#dbeafe` |
| `canvasBg` | `#10161c` | `#ffffff` |
| `grid` | `#1c2430` | `#dfe5ea` |
| `status` | `#161b20` | `#ffffff` |
| `danger` | `#f28b82` | `#c62828` |
| `success` | `#7ee787` | `#1b7f3a` |
| `info` | `#39c5cf` | `#0e7490` |

默认曲线色板：

- Dark：`#4ea1ff #ffb454 #7ee787 #d2a8ff #ff7b72 #39c5cf`
- Light：`#1f6feb #b26a00 #1b7f3a #7b3fbf #c62828 #0e7490`

## 切换链路

1. 启动：`ThemeManager::applyTo()` 读取 `theme`，默认 `dark`，装好全局 QSS 与 palette。
2. 窗口构造末尾：`ThemeManager::refreshWidgetStyles(this)` 把启动前已经创建的局部深色 QSS 转成当前主题。
3. 设置页 Apply/OK：`ThemeManager::setMode()` → `MainWindow::applyTheme()` → `RibbonManager::applyTheme()` + 全局样式刷新，并写 `theme` 后 `sync()`。
4. 运行中新控件：创建后调用 `ThemeManager::refreshWidgetStyles(widget)`；数据源行已按此处理。

`refreshWidgetStyles` 把控件原始深色 QSS 存在 `psDarkStyle`，切回深色时恢复原样；Ribbon 标了 `psThemeManaged`，避免被二次转换。

## 验收

- [x] 浅色下主界面、Ribbon、画布、数据源 Dock、属性/对象树、日志区均为浅色。
- [x] 深色默认外观保持不变。
- [x] 设置页切浅色后不重启立即生效，`theme=light` 已写入配置。
- [x] 以 `theme=light` 启动，重启保持浅色。
- [x] 浅色下加载 `tests/fixture/demo_flight.csv`，6 通道正常绘制、曲线可读。
- [x] 浅色下空数据源正常显示。
- [x] `ctest` 19/19 通过。

验证命令：

```bash
cmake --build build -j4
LD_LIBRARY_PATH="$PWD/deps/Qt/6.2.4/gcc_64/lib" ctest --test-dir build --output-on-failure
```

浅色启动配置：

```ini
[General]
theme=light
restoreLayout=false
```

## 边界

- 不做跟随系统、高对比度、护眼等额外主题模式。
- 主题不拆成插件；扩展使用宿主 token / 宿主 QSS。
- 模块里已有的深色局部 QSS 保留为深色基线，由 `ThemeManager` 统一映射；新增代码应优先直接取 token，不要新增固定深色值。
