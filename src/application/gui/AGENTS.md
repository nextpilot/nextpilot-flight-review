# AGENTS.md · gui/（L5 外壳与视图）

> 目录归属权威见 [`../LAYERS.md`](../LAYERS.md)。

## 本目录是什么

**Qt Widgets 装配层**：shell/（MainWindow、Ribbon/Dock 容器、外壳装配、IUiContext 实现）、views/（绘图视图与功能视图）、其余共享 UI 基础设施。注意：模块 `*_gui` 与 `uischema_widgets`/`fastplot` 同样构造控件，本层不是唯一控件来源。

## 允许放

- 窗口/停靠/页签/主题/参数表单渲染（uischema_widgets 消费方）
- IUiContext 与 PanelFactory 的实现（模块经它们放置面板，GUI 反向不知业务）
- 各功能视图：读注册表数据、发意图，不自持业务状态

## 禁止放

- 业务格式解析、数据源生命周期、领域判断逻辑（那些在 modules/toolbox/libs）
- 对 shell/ 的依赖（gui 由其自身入口构造 App，方向只能 shell→gui 装配，不许反向）
- 把运行态藏进窗口对象（状态归 AppContext/SettingsStore，GUI 只展示与发意图）
- 绕过 IUiContext/PanelFactory 直接 new 模块的面板（面板类型经工厂表解析）

## 外壳在装载链里的位置（契约见 `../LAYERS.md`「UI 归属与装载契约」）

装载链：GUI 可执行文件链接各模块/扩展的 `*_gui` OBJECT 库 → 进程启动静态注册对象构造、PanelFactory 自动填好 → `host.activate()` → 模块 `onAttach` 请求面板 → **`GuiContext` 路由 → `PanelFactory::create` → `ShellBinder` 放置**。外壳只做路由与放置，**不认识任何业务面板**——新增功能视图靠工厂自注册，shell/ 零改动；发现 shell/ 要 include 某个业务面板头，就是装配违规。

## Activity Bar 与 Side Bar

- Activity Bar 固定 48px，只放数据、脚本、分析、诊断；绘图不进 Activity Bar。
- 模块用 `contributes().activities` 声明入口（宿主经装配层 ContributionsInstaller 灌回，裁定 52442006 Step 3）：id、标签、图标、排序、对应 ribbon 页、左栏 section 面板类型、可选中央面板。
- 点击 activity 由 ShellBinder 切换 Activity 选中态、左栏 section 与 ribbon 页；中央面板请求交回外壳，按面板类型走 PanelFactory。
- 左侧视图各自是独立 dock（数据源、脚本文件…），可在 Primary Side Bar、Secondary Side Bar、Panel 之间拖动并浮动；Activity Bar 接收视图拖入后生成图标并从左槽聚焦。
- 左栏 section 与旧页签的区别：同栏面板不再互斥成 tab，由 activity 决定可见性。
- `Ctrl+B` 只切整栏显隐。badge 走 `setActivityBadge`。配置键：`ui.activityBar.order`、`ui.activityBar.visible`、`ui.sideBar.visible`、`ui.sideBar.width`。

## 动手前自查

1. 这段代码能 headless 跑吗？能 → 它可能不该在 gui（去 libs/modules）；不能 → 必须只活在本层
2. 新视图要取数：走意图层（与脚本/CLI 同一路径），禁止直捅 Dataset 内部或 Rust FFI
3. 领域专属面板的"形态"归扩展（PanelSpec 声明 + gui 侧注册实现），通用视图层禁止长成领域杂物箱
