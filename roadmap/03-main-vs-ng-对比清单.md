# PlotScope：main 分支 vs ng 数据源 / 绘图 全维度差距清单

> 依据：`main` 分支与本地 `nextgen-architecture`（HEAD `0349da6`）**实际源码**逐条比对，不是回忆版。
> 图例：✅ 有且可用　⚠️ 有但弱/半成品/仅入口　❌ 无
> 证据以「文件」列在每节末尾，可 `git show main:<path>` 直接核对。

## 0 一句话结论

**ng 在架构、绘图交互、状态模型上明显领先；main 在数据源工程能力（实时源、时间对齐、多表快照、参数表、脚本控制台、设置对话框）上明显更完整。**

所以不是"选一边"，而是：**以 ng 为基线，把 main 的数据源能力按清单平移过来**。如果你现在的首要目标是"能连真机采集、能对齐多源、能出报告数据"，那 main 是唯一能立刻干活的；如果目标是"长期可扩展的绘图 IDE"，ng 是唯一在正确方向上的。

---

## 1 骨架总览

| 维度 | main | ng |
|---|---|---|
| Ribbon 页数 | 6：文件 / 数据 / 绘图 / 协议 / 分析 / 视图 | 8：开始 / 文件 / 数据 / 绘图 / 脚本 / 协议 / 分析 / 视图 |
| 页/组/按钮来源 | 各模块 `registry.addRibbon(RibbonContribution{page,order,panels})`，`panels{RibbonItem{action,label,icon,large,arg}}` | `SkeletonSlots.cpp` 声明页/组（slot），模块 `ui.addAction(UiAction{id,label,area,intentOp,icon,galleryGroup,visual,shortcut,order,large,checkable})` 绑进组 |
| 动作执行 | `CommandManager` + `ActionContext`，模块注册 `addAction(id, fn)` | `IIntentExecutor`：按钮 → `intentOp` → 模块 `IIntentHandler`（GUI/CLI/脚本同一条路径） |
| 图标 | `:/icons/*.svg` 按名引用 | 24+3 个新图标 + `visual` 缩略图种类声明 |
| 主题 | SARibbonOffice2021Blue + 一段 `#141a1f` 琥珀色深色 QSS（`RibbonManager::applyTheme`） | `ThemeManager` token 化主题，`Ctrl+T` 运行时切换 |
| 停靠 | `dock.src`（数据源）/`dock.protocol`（协议检查）/`dock.tree`（对象树）/`dock.console`（脚本控制台）/`dock.analysis` | `dock.left`（数据集）/`dock.right`（属性）/`dock.bottom`（日志） |
| 快捷键 | **零**（全仓无 `setShortcut`） | `Ctrl+O` 打开、`Ctrl+S` 保存布局、`Ctrl+,` 设置、`Ctrl+T` 主题、画布 `Ctrl+Z` 视图撤销 |
| 测试 | 手工 | `ctest` 24 项（含 ribbon 页序/组归属/图库条数/快捷键/勾选态断言） |

证据：`src/application/shell/RibbonManager.cpp`、`src/application/modules/*/…/Ribbon.cpp`；`ng/host/src/SkeletonSlots.cpp`、`ng/gui/shell/MainWindow.cpp`、`ng/gui/shell/ShellBinder.cpp`、`ng/tests/test_gui.cpp`。

---

## 2 Ribbon 逐页逐组对照

### 2.1 文件页（两边同构）

| 组 | main | ng |
|---|---|---|
| 文件 | 打开（大）/ 导入布局 / 设置 | 打开文件（大，Ctrl+O）/ 导入布局 / 设置（Ctrl+,） |
| 导出 | 保存布局（大）/ 导出设置 / 导出数据 | 保存布局（大，Ctrl+S）/ 导出设置 / 导出数据 |
| 截图 | 截图（大） | 截图（大） |
| 帮助 | 关于 | 关于 |

差异：**ng 补了快捷键，按钮/组/顺序与 main 一致**。ng 多一页"开始"，把"打开数据"（Ctrl+O）提到最高频位置。

### 2.2 数据页 —— 差距最大的一页

| 组 | main | ng |
|---|---|---|
| 常用 | 快速打开 / 最近文件 / 导出快照 / 重新加载 | 快速打开 / 最近 / 重载 / 导出 / 清缓存（≥5，`data.action.*`） |
| 来源 | gallery 三分类：**常用 / 文件格式（14 种）/ 实时源**，每种格式一个带 `file_*.svg` 的按钮 | gallery ≥3：csv / ulog / custom-protocol（格式由扩展驱动进 gallery） |
| 调试 | 协议检查（`protocol.inspect`）→ 打开协议检查 dock | 空组（`ribbon.group.data.debug` 已声明但无动作） |
| 导出快照 | ✅ 多表快照（`data.exportSnapshot` + 树右键） | ❌ 只有单表 CSV |
| 实时源入口 | ✅ 独立 gallery 分类 + 完整运行时 | ⚠️ 空态页有"连接实时源"按钮，点了提示"当前版本的实时源采集尚未接入" |

**main 的 14 种格式**：ulog / ulg / apbin / dataflash / blf / asc / mcap / mdf / rosbag / nmea / ubx / json / csv / tsv / binary。
**ng 内置**：csv / ulg / log / bin / mcap / tlog；NMEA、MAVLink、汽车总线等走 `ng/extensions/{gnss,autopilot,automotive,robotics,common}`。

> 结论：**格式广度 main 赢（尤其 mdf / rosbag / blf / dataflash 这类车载-航空工业容器，ng 完全没有）**；ng 赢在"扩展式解析器注册"，加格式不用改主仓。

### 2.3 绘图页 —— 组数 5 vs 7

| 组 | main | ng |
|---|---|---|
| 页面 | 新建页（大） | 新建页（大）+ **删除页（大）** |
| 布局 | `LayoutPickGrid` 8×8 悬停选格 → `LayoutPopup`；`layoutThumb(2,1)` 32×32 琥珀线框，label 初始"2×1"、选中后变"r×c"；小按钮：添加 / 删除 / 左右 / 上下 | 2×1 缩略大按钮（动态回写 r×c）+ 添加 / 删除 / 左右 / 上下（2×2 交错排布） |
| 显示 | 网格 / 边框 / 图例（3 个 checkable 小按钮，`setCanvasViewFlag`） | 重绘（大）/ 网格（大，**下拉：X 网格线 · Y 网格线 · 全部关闭**）/ 边框 / 图例 |
| 画布操作 | 移动 / 放大 / 缩小（checkable）+ 自适应（`canvasAutoY`） | —— 并入"交互"组 |
| 交互 | ❌ 无此组（AB / 标记藏在画布悬浮栏和右键） | 标记（大，全画布同一时刻打竖线）/ AB（大，三角尺）/+ 放大·缩小·移动·恢复 2×2 |
| 导出 | ❌ 无此组（在画布右键） | 截图 / PNG / CSV |
| 图表类型 | gallery 12 种，4 组：常用 / 曲线 / 地图 3D / 仪表；`setMaximumWidth(460)` | gallery 12 种，4 组：常用 / 曲线 / 地图3D / 仪表；纯数据声明（可 headless 测） |
| 曲线与轴 | ❌ 无此组 | ✅ 曲线批量操作 + 轴属性入口（ng 追加） |

> **这是 ng 最大的 UI 优势**：把 main 藏在右键/悬浮栏里的 AB、书签、导出、标记全部提升成 ribbon 一级入口，并且把 8×8 选格换成"缩略图大按钮+动态 label"（一次点击信息量更大）。
> **已改（2026-11-21）**：main 的 8×8 悬停选格已移植进 ng（`GridPickPopup`，替代原 `QInputDialog` 手输"行×列"），并补了 main 缺的三件事：弹层显示当前规格、hover 实时行列数字、Esc 取消提示常驻；上限从 6×6 提到 8×8。实测对比见下方 §5.1。

### 2.4 分析页

| | main | ng |
|---|---|---|
| 形态 | `dock.analysis.%1` 每个分析应用一个独立 dock + gallery（全部应用 / 最近使用 / 收藏），"收藏到分析应用库"/"取消收藏"两态按钮，空态有占位项 | `ribbon.group.analysis.apps` gallery + 意图 `analysis.app.default / report / review`、`analysis.favorite`、`analysis.run` |
| 差距 | main 的"应用即 dock"是真能同时开多个分析面板；ng 目前是"应用库 + 运行意图"，dock 化未做 | ng 的"飞控复盘/报告/审阅"三档预设是 main 没有的产品化分层 |

### 2.5 协议页

| | main | ng |
|---|---|---|
| 按钮 | 新建协议（大）/ 打开编辑 / 校验协议 | 协议编辑器 / 协议检查器（各一个入口，按 id 特判弹窗） |
| 引擎 | `protocol.validate / blank / createEditor`（`ProtocolEditorDialog`），顺序段协议 + 自定义协议 + `.psproto` | `ProtocolModule` + `ProtocolTemplateStore`（模板 list/save/validate/preview），v3 schema、bin/text 双解析器 |
| 差距 | main 协议引擎是 v1/v2 语义；**ng 的 v3 schema + bin/text 双路 + 模板库明显更成熟**；但 main 的"打开编辑"直接进编辑器，比 ng 少一跳 | |

### 2.6 脚本页 / 视图页

| | main | ng |
|---|---|---|
| 脚本 | **无脚本页**，只有底部 `dock.console`「脚本控制台」（Lua 5.4、`ScratchPlot` 临时画图、`LuaAppApi`/`LuaCalcApi`、脚本库只 require 目录下 `.lua`） | 有"脚本"页，**实跑确认有「运行脚本 / 运行」组**（此前"空壳"的结论已作废）；无控制台 UI |
| 视图 | `dock.tree`（对象树）+ 各 dock 显隐（`view.panels`/`view.appearance` 在 ng） | `view.toggleDock.left/right/bottom` + 切换主题（Ctrl+T） |
| 差距 | **main 的脚本控制台是真能用的 REPL+画图；ng 只有"运行脚本"入口，无 REPL/控制台** | |

---

## 3 数据源能力逐项差距

| # | 能力 | main | ng | 说明 |
|---|---|---|---|---|
| 1 | 导入对话框 | ✅ `DataSourceDialog`（格式选择 + 选项 + 预览） | ✅ `ImportDialog`（格式 + 选项 + 预览） | 基本等价 |
| 2 | 多源并存/管理 | ✅ 加载 / 移除 / 重命名 / 重载 / 配置 | ✅ 同（树右键） | ng 有"设为活动数据源"，main 无 |
| 3 | 树层级 | 源 → 表 → 字段 | 数据集 → 表 → 字段 | 同构 |
| 4 | 字段搜索 | "搜索字段 / 参数 / 消息" | 搜索框（字段） | main 搜索范围更宽（含消息/参数） |
| 5 | **实时源** | ✅ 连接 / 暂停 / 继续 / 重连 / 速率 / 记录；`LiveDataSourceDriver` + 共享 transport 解析器 + `DeviceDebugDialog` | ❌ 未接入（空态按钮弹提示） | **决定性差距** |
| 6 | **录制** | ✅ 实时 sink 录制（mcap 写、`.psraw` 捕获格式） | ⚠️ `ng/recording`（Recorder / McapCodec / RawCodec / RecordingBackend）库已存在，但没有跟实时源接线 | ng 是"有引擎没油门" |
| 7 | **时间对齐** | ✅ 设为时间基准 / 对齐起点 / 对齐终点 / 对齐中心 / 重置偏移 | ❌ 无（只有单曲线 offset 平移） | **多源分析刚需，ng 完全缺** |
| 8 | 派生通道（算子） | ✅ `TransformDialog`：算子 + 输入通道 + 输出主题/通道 + **实时预览** + 参数；算子含四元数→RPY、展开 ±π 跳变、角度制、单通道滤波 | ✅ `TransformEditorDialog`：算子 / 表达式 / Lua 三类 | 形态差异：main 有预览曲线，ng 是"提交即生效" |
| 9 | 派生通道（表达式/Lua） | ❌ 无（只有算子对话框） | ✅ 表达式 + 内嵌 Lua | ng 赢 |
| 10 | 有状态过滤器 | ❌ 无 | ✅ 滑动平均 / 滑动方差 / RMS / 微分 / 积分 / 绝对值 / 归一化 / 缩放偏移 | ng 赢（滚动窗口类是主独有能力） |
| 11 | 变换写回语义 | 派生通道入数据集 | **只读叠加，不写回数据集** | ng 的设计更干净，但用户想"导出派生列"就得先 CSV |
| 12 | 参数表 P/Q | ✅ 独立参数表 + 参数缓存命中（仅 ULog） | ⚠️ 树上有"参数 0 / 参数 %1"节点，无 P/Q 表与专项缓存 | main 赢 |
| 13 | 解析缓存 | ✅ 命中率统计 + 清空 | ✅ 清除全部解析缓存（无命中率显示） | main 更可观测 |
| 14 | 采样上限 | ✅ 设置里"CSV 导出采样上限 / 派生通道重算上限（0=不限）" | ❌ 无 UI 设置 | |
| 15 | 树右键 | 画到当前画布 / 叠加全部同名 / **对比同名通道** / 选择通道… / **展开数据列** / 导出数据快照 / 移除 / 重命名 / 重载 / 配置 | 画到当前画布 / 添加变换 / 删除派生 / 导出 CSV / **状态视图** / 设为活动数据源 / 重命名 / 移除 / 配置 / 重载 | main 独有：叠加全部同名、对比同名、选择通道、展开数据列、多表快照；ng 独有：状态视图（bool 色带）、设为活动源 |
| 16 | 多表快照导出 | ✅ 快照对话框（多表/多格式） | ❌ 单表 CSV | |
| 17 | 调试终端 | ✅ `DeviceDebugDialog`（实时帧/发送命令） | ❌ | |
| 18 | 协议检查器 | ✅ 独立 dock（`dock.protocol`） | ✅ 对话框（协议页入口） | dock 常驻 vs 弹窗 |
| 19 | 容器格式 | ✅ mdf / rosbag / blf / dataflash / apbin / asc | ❌ 无 | 工业/车载客户会卡这里 |
| 20 | 实时多路 | ✅ 共享 transport 解析器（一个 transport 喂多个解析器） | ❌ | |

证据：`src/application/modules/datasource/{widget/ribbon/DataSourceRibbon.cpp, widget/dock/DataSourceDock.cpp, session/DataSourceCommands.cpp, widget/dialog/{DataSourceDialog,DeviceDebugDialog,TransformDialog}.cpp, manager/LiveDataSourceDriver.cpp}`；`ng/modules/data/{DataModule.cpp,DataManagerPanel.cpp}`、`ng/gui/shell/TransformEditorDialog.cpp`、`ng/recording/*`。

---

## 4 绘图能力逐项差距

| # | 能力 | main | ng | 说明 |
|---|---|---|---|---|
| 1 | 页签 | `＋` 作为最后一页签（tooltip"新建页"）、`setTabsClosable(true)`、双击改名 | 新建页 / 删除页 / 复制页 / `＋` / 双击改名 / 整页复制 | ng 多"复制页"，main 多一个常驻"＋"页签 |
| 2 | 页内布局 | 网格 r×c（8×8 选格）+ 画布间拆分 + 最大化/还原 | 网格 r×c（输入框）+ 拆分 + 最大化/还原 | main 选格交互更强 |
| 3 | **画布拖拽重排** | ✅ DnD（`application/x-plotsc…`，跨画布搬曲线/画布） | ❌ 无 `dragEnterEvent`（只有树→画布单向拖入） | main 赢 |
| 4 | 图型 | 12 种，含 **surface3d**，无 waterfall | 12 种，含 **waterfall**，无 surface3d | 一对一对冲，不是"谁多" |
| 5 | 图型参数 | 拖入时校验：XY 需 2 通道、GPS 需 lat/lon、3D 轨迹 ≥2 通道、3D 瀑布=多条可见曲线、3D 曲面 | 拖入时校验：3D 轨迹需 x/y、经纬轨迹需 lat/lon、罗盘 yaw/heading、姿态仪 roll/pitch、直方图 value、频谱 value | 语义都覆盖，main 多"3D 曲面" |
| 6 | 双轴 | ✅ 绑定左/右轴（右键） | ✅ 曲线右键 + 轴属性对话框 | 同 |
| 7 | 轴属性 | ⚠️ 只有"重置 X 轴 / 重置缩放 / 重置整个视图" | ✅ 左 Y / 右 Y / X 轴属性对话框 + **Y 变换**（每轴 CapAxis） | ng 赢 |
| 8 | 网格粒度 | 全局一档（网格 on/off） | ✅ X 网格线 / Y 网格线 分轴开关 + 虚线样式 + 右键 `plot.grid.query` 同步勾选态 | ng 赢 |
| 9 | 图例 | 显示/隐藏 | ✅ 折叠、字号缩放、单项显隐（`plot.legend.{collapse,fontZoom,toggleItem}`） | ng 赢 |
| 10 | 曲线属性 | 右键：颜色 / 线宽 / 轴 / 显隐 / 删除 | 右键 + **属性面板**（按选中对象给曲线/轴/画布属性） | ng 赢（面板化） |
| 11 | 工具 | 手型 / 放大镜 / 缩小镜 + 滚轮 + 框选 + 自适应 | 同 + 轴区手势 + 键盘导航 + hover 工具栏 + 双击标题改名 | ng 赢 |
| 12 | AB 测量 | ✅ 悬浮栏 + 右键（Δt / Δy / 清除） | ✅ ribbon 大按钮 + 右键（Δx 提示 / Δy / 清除） | ng 提到一级入口 |
| 13 | 固定读数 | ✅ | ✅ | 同 |
| 14 | 时间书签 | ✅ 添加 / 重命名 / 删除（右键 + 悬浮栏） | ✅ 添加 / 重命名 / 列表 / 拖动（ribbon + 右键） | 同 |
| 15 | **标记引擎** | ❌（无全画布标记） | ✅ `data.marker`：按时间点全画布打竖线、多画布同步、清除 | ng 独有 |
| 16 | 导出 | ✅ 画布右键 导出 PNG / CSV | ✅ ribbon 导出组 + 右键 + 截图 | 同，ng 更好找 |
| 17 | 状态模型 | 布局保存/导入（文件级 JSON）+ `pageState/buildPage` | ✅ `plot.state.save / restore / clear / set` 状态配方 + 文件级 layout.save/import | ng 赢（页面状态可编程） |
| 18 | 视图撤销 | ❌ | ✅ `Ctrl+Z` → `plot.view.undo` | ng 赢 |
| 19 | 设置 | ✅ `PlottingSettingsPage`：CSV 采样上限 / 新建画布默认网格 / 默认图例 / 时间轴格式（相对秒·时分秒·UTC 秒）/ 派生重算上限 / 清解析缓存 | ❌ **无设置对话框**（只有 `settings.get/set/list/flush` 意图层） | **main 赢，ng 是明显空缺** |
| 20 | 曲线与轴批量组 | ❌ | ✅ ribbon 组 | ng 追加 |

证据：`src/application/modules/plotting/widget/{ribbon/PlottingRibbon.cpp, canvas/PlottingCanvas.cpp, tabs/PlottingTabs.cpp, settings/PlottingSettings.cpp, session/PlottingModule.cpp}`；`ng/gui/views/plot/{CanvasWorkspace,PlotCanvas}.cpp`、`ng/modules/plot/PlotModule.cpp`、`ng/host/src/SkeletonSlots.cpp`。

---

## 5 菜单与右键

| 位置 | main | ng |
|---|---|---|
| 画布右键 | 颜色/线宽 → 绑定左/右轴 → 显隐曲线 → 删除曲线 → 图例 → 网格 → 添加曲线… → 添加时间书签… → 重命名/删除书签 → 固定读数 → 清除 A/B → 导出 PNG… / 导出 CSV… → 重置 X 轴 / 重置缩放 / 重置整个视图 → 左右/上下拆分 → 最大化/还原 → 删除此画布 | 曲线（颜色/线宽/轴/显隐/删除）→ 画布操作（左右/上下/最大化/关闭）→ 显示（图例/网格）→ **坐标轴属性**（左Y/右Y/X轴/ Y 变换）→ 视图状态（撤销 Ctrl+Z）→ AB 测量（Δx/Δy/清除）→ 固定读数 → 时间书签（添加/重命名） |
| 数据树右键 | 见 §3 第 15 行 | 见 §3 第 15 行 |
| 页签右键 | 重命名页签 | 重命名 / 复制 / 关闭 |
| 菜单栏 | ❌ 无（全靠 ribbon） | ⚠️ `menu.plot` 槽已声明（曲线上/下移、删除等），实际为右键菜单提供者（`m_actionMenuProvider`） |

差异要点：**main 的右键菜单更重（22 项，含两个导出和三个重置）；ng 用分组标题压成 8 组，并新增轴属性对话框与视图状态**。main 独有"重置 X / 重置缩放 / 重置整个视图"三个安全出口，ng 目前只有撤销。

---

## 6 交互细节差距

| 项 | main | ng |
|---|---|---|
| 通道拖入 | ✅ 带图型语义校验 | ✅ |
| 画布↔画布拖拽 | ✅ | ❌ |
| 滚轮缩放 / 框选缩放 | ✅ | ✅ |
| 拖平移 / 手型工具 | ✅ | ✅ |
| 游标读数 | ✅ | ✅ |
| 轴区拖动改范围 | ⚠️ 未确认 | ✅ |
| 键盘导航（方向键移动、PageUp/Down） | ❌ | ✅ |
| 双击画布标题改名 | ❌（只有页签改名） | ✅ |
| 画布标题 hover 悬浮工具条 | ✅（新增/左右/上下/最大化/重排/AB/删除） | ✅（左右/上下/全屏/书签/AB/删除） |
| 全画布标记同步 | ❌ | ✅ |
| 视图撤销 | ❌ | ✅ Ctrl+Z |
| 快捷键 | 无 | Ctrl+O / Ctrl+S / Ctrl+, / Ctrl+T / Ctrl+Z |

---

## 7 UI 形态差距（会被用户直接看见的）

1. **页签数量与秩序**：main 6 页（业务页靠注册顺序，`order` 字段基本空置）；ng 8 页且 order 显式升序（开始→文件→数据→绘图→脚本→协议→分析→视图）。
2. **Ribbon 两行模式填充方向**：SARibbon 按**列**填（先上后下再下一列），ng 为 2×2 小按钮交错写 order 才排成"上排 放大·缩小 / 下排 移动·恢复"。main 因按钮少未暴露这个问题。
3. **图标与视觉**：main 全部沿用旧 `file_*.svg`/`newpage`/`open`/`validate`，18×18（26×26 large）；ng 补齐 24+3 个图标且 `visual` 声明让 gallery 出缩略图。
4. **dock 标题**：main 用默认 ADS 标题（灰底）；ng 给 dock 标题加图标 + tab 控件级透明（`dock->tabWidget()` 单独设 QSS，因为 ADS 自带 stylesheet 压过全局 QSS）。
5. **右上角**：main 保留 SARibbon 折叠 chevron（在 `UseRibbonFrame` 下与关闭按钮重叠）；ng 关掉 chevron（`showMinimumModeButton(false)`），保留双击页签折叠。
6. **深色主题实现路线**：main = 一整段写死色值 QSS；ng = ThemeManager token，可运行时切换（Ctrl+T）。
7. **属性面板深度**：main `PropertyManager` + 对象树 dock（树形对象 → 属性表单）；ng `PropertyPanel` 单面板 + `"%1"暂无属性提供者` 占位（按选中对象动态取提供者）。

---

## 8 差距汇总

### A. ng 缺、main 有（若要"能干活"，这些必须补）

| 优先 | 项 | 为什么卡 |
|---|---|---|
| P0 | 实时源运行时（连接/暂停/继续/重连/速率） | 没有它 PlotScope 只能读文件，不能接真机 |
| P0 | 实时录制接线（Recorder→McapCodec/RawCodec 已在库，缺 UI+驱动） | 采集即录是飞控现场基本要求 |
| P0 | 时间对齐（基准/起点/终点/中心/偏移） | 多源对照的物理前提 |
| P1 | 多表快照导出（`data.exportSnapshot`） | 出报告/交付数据的唯一出口 |
| P1 | 参数表 P/Q + 参数缓存命中 + 解析缓存命中率 | ULog 调参分析刚需 |
| P1 | 脚本控制台 dock（REPL + ScratchPlot） | ng 脚本页目前是空壳 |
| P1 | 设置对话框（采样上限 / 时间轴格式 / 默认网格图例 / 派生重算上限） | ng 只有 settings 意图，无 UI |
| P2 | 工业容器格式（mdf / rosbag / blf / dataflash / apbin） | 客户面决定 |
| P2 | 叠加全部同名 / 对比同名通道 / 展开数据列 | 多源比对效率 |
| P2 | 画布↔画布拖拽重排 | 手工布局效率 |
| P2 | 设备调试终端、共享 transport | 现场排障 |
| P3 | surface3d 图型 | 3D 曲面 vs 瀑布，各有用途 |
| P3 | 右键"重置 X / 重置缩放 / 重置整个视图" | ng 只有撤销兜底 |

### B. main 缺、ng 有（ng 已领先，别回退）

- 意图层统一（同一个 `intentOp` 被 GUI/CLI/脚本共用）与 `IIntentHandler` 契约
- 8 页/组/动作的**数据化声明**（`SkeletonSlots` + `UiAction`，headless 可测）与 24 项 ctest 断言
- 表达式 + 内嵌 Lua 变换、有状态过滤器（滑窗/RMS/微分/积分/缩放）
- 全画布标记引擎、视图撤销 Ctrl+Z、键盘导航、轴属性对话框、Y 变换
- 图例折叠/字号/单项显隐、X-Y 分轴网格、`plot.grid.query` 勾选态同步
- 状态配方 `plot.state.*`、复制页、删除页大按钮、ribbon 导出组、曲线与轴组
- ThemeManager token 化主题 + Ctrl+T

---

## 9 决策建议（三条路，按你的目标选）

**路线 1 · 以 ng 为基线（推荐）**
把 §8-A 当成季度任务清单，P0 三项（实时源、录制、时间对齐）先做；其余按客户面逐步。理由：ng 的架构（intent + slot + registry）已经能承载"多加一个格式/多加一个图型不改主窗口"，而 main 每加一个能力都要动 `RibbonManager` / `MainWindow` / 各模块 ribbon 三处。

**路线 2 · 以 main 为基线**
只有一种情况值得：你近期必须交付"现场采集 + 多源对齐 + 报告数据"，且不允许任何功能回退。代价是你要在 main 上重建整批 UI 改造（页签化、m×n 页内布局、axes 悬浮工具栏、网格下拉、布局缩略大按钮、标记/AB 大按钮、dock 标题图标、24 个图标、token 主题）——工作量比重写数据源那三项还大。

**路线 3 · 双轨（main 当特性源，ng 当主线）**
main 冻结为"能力参照实现"，ng 逐个模块"搬一个、测一个、推一个"。这条最省心：数据源能力从 main 平移（有现成实现可抄），绘图继续只走 ng。

我的判断：**路线 3 就是路线 1 的落地方式**——你现在问"采用哪一边"，答案其实是「**绘图采用 ng，数据源采用 main 的实现、搬到 ng 名下**」。

---

### 附：本次比对未做逐项实机验证的两点

1. main `PlottingCanvas.cpp` 中出现"视频"字样，用途未核实（疑似预留或某对话框文案残留），不计入能力表。
2. ng `ribbon.group.data.debug` 与 `ribbon.group.script.*` 两组已声明但无动作——空组当前不渲染，属"预留插槽"，不是 bug。
