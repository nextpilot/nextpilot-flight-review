# 主题与浅色模式

- 状态：计划中，未实施
- 目标版本：下一轮专题
- 相关代码：`src/application/shell/ThemeManager.*`、`src/application/modules/plotting/canvas/PlottingCanvas.cpp`、`src/application/modules/datasource/dock/DataSourceDock.cpp`

## 现状

当前只有深色界面。`ThemeManager` 管 shell 的 QSS、调色板和 Ribbon 主题，但主题没有覆盖到所有绘图/数据源控件：

- `PlottingCanvas` 里 `colorBg()`、`colorGrid()`、`colorText()`、`colorDim()` 以及大量 `QColor("#...")` 是硬编码深色。
- `DataSourceDock` 的树、工具条、空态、状态条有几十处硬编码 `#0d1117`、`#161b20`、`#c9d1d9`。
- 右侧属性面板、对象树、日志 Dock、部分对话框也有自己的深色样式。

实验结果：只切换 shell QSS 会出现“白壳 + 黑画布 + 黑数据源面板”的半浅半深界面，不能作为可用主题。该实现已回退，不保留半成品。

## 方案

### 1. 建立主题 token

在 `ThemeManager` 里定义一套语义 token，而不是让模块各自写十六进制颜色：

| token | 用途 |
|---|---|
| `bg` | 主背景 |
| `panel` | 面板/工具条背景 |
| `panelAlt` | 交替行、悬停 |
| `border` | 分隔线、边框 |
| `text` | 主文字 |
| `textDim` | 次级文字 |
| `accent` | 选中、游标、强调 |
| `grid` | 画布网格 |
| `series[6]` | 曲线默认色板 |

提供 `ThemeManager::color(token)` 和 `ThemeManager::seriesColor(index)`，模块不再直接写固定色值。

### 2. 模块改造

- `PlottingCanvas`：背景、网格、坐标轴、文字、游标、选择框、提示框全部走 token；曲线默认色板走 `series`。
- `DataSourceDock`：树、工具条、按钮、空态、状态条样式由 token 生成，删除硬编码 QSS。
- 属性面板、对象树、日志 Dock、设置/关于对话框：统一用 shell 提供的全局 QSS 和 palette，不再各自覆盖。
- Ribbon：深色/浅色分别使用对应的 SARibbon 内置主题 + token 生成的 QSS。

### 3. 切换与持久化

- 设置 → 通用 → 外观，提供“深色 / 浅色”下拉。
- 切换后立即刷新全局 QSS、palette、Ribbon 和已打开的画布/Dock，不要求重启。
- 选择写入 `NextPilot/PlotScope` 的 `theme` 键；启动时读取。

### 4. 验收

- 浅色下主界面没有仍为深色的画布、面板、Dock 或对话框。
- 曲线、网格、游标、图例在浅色背景上可读；6 条默认曲线颜色可区分。
- 深/浅切换后不重启，已打开画布立即更新。
- 重启后保持上次选择。
- `ctest` 全过；GUI 冒烟覆盖 CSV 6 通道和空数据源两种状态。

## 非目标

- 不做高对比度、护眼、跟随系统自动切换等额外主题模式。
- 不为单个扩展提供独立换肤 API；扩展使用宿主 token。
- 不把主题做成插件。

## 风险

- 画布绘制里颜色和图表语义耦合较多，改 token 时容易漏掉右键菜单、选择框、提示框等临时绘制路径。
- 数据源 Dock 的 QSS 分散在多个控件创建函数里，需要逐处替换并做浅色截图核对。
- SARibbon 自带主题与自定义 QSS 可能冲突，需要固定一套覆盖顺序。
