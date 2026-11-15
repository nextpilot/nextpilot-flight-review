# PlotScope 组件扩展点决策 v1

日期：2026-11-14  
状态：已确认，待实施  
范围：app 宿主、内置组件、外部扩展、Ribbon/Dock/Dialog 扩展点

## 结论

采用 VSCode 式“核心外壳 + 贡献点”模型，但不引入完整扩展宿主。

- 核心外壳由 app 拥有：`MainWindow`、Ribbon 容器、Dock 容器、UiDialogEngine。
- 功能入口由组件/扩展通过 manifest 贡献，内置组件与外部扩展共用同一份 contribution schema。
- 组件与扩展本质同模型，区别只在加载方式：
  - 内置组件随 app 编译，启动即加载，可直接使用 C++/Qt；
  - 外部扩展动态加载，走 ABI，不跨边界返回 `QWidget*`。
- `ComponentRegistry` 收敛为 `ContributionIndex`，由 manifest 自动构建；组件不再手写 `addRibbon/addAction/addProtocol`。
- `MainWindow` 只渲染 contribution、分发 command，不再认识具体业务按钮。

## 扩展点

固定以下宿主扩展点：

- `ribbon`：页、面板、按钮、菜单、画廊
- `dock`：位置、标题、默认可见性
- `dialog`：`.ui + JSON`
- `command`：命令 ID 与处理函数
- `loader`：文件/流加载器
- `protocol`：协议 kind、schema、validate
- `op`：通用算子
- `curve`：曲线类型
- `panel`：属性/辅助面板

复杂 UI 不塞任意 JSON：绘图页图型画廊、数据页下拉等使用宿主控件类型（如 `plotGallery`、`combo`）。内置组件可提供 widget factory；外部扩展只能声明类型和 JSON。

## 不做的部分

- 不把 `MainWindow` 本身扩展化。
- 不引入 Node.js 扩展宿主。
- 不一开始做完整懒激活；先支持 `onStartup` 与命令触发。
- 不允许组件直接向主窗口塞任意 `QWidget`。

## 实施顺序

### 第一阶段：完成协议编辑器

先完成协议编辑器的功能与模型收敛，再动全局组件重构。

完成条件：

1. `ProtocolDocument` 成为协议唯一类型化模型；编辑器读写、校验、保存都经它，不再各自操作裸 `QJsonObject`。
2. 编辑器现有能力不回退：顺序段、类型表、字符串、series/selector、CRC `over`、timestamp、校验、另存、新建。
3. `SequentialCodec` 与编辑器共用同一模型、类型表和校验。
4. `test_extension_file_formats`、`test_extension_runtime` 通过；协议编辑器截图更新。
5. 协议 JSON 的键名/默认值只维护一处。

### 第二阶段：组件扩展点重构

1. 命令 ID 化：文件、数据、绘图、脚本、视图入口先从 `MainWindow` lambda 迁到 `CommandManager`。**已完成基础入口**；数据页“来源”仍由扩展列表动态生成。
2. 简单页迁移：文件、数据、脚本、视图改为 Ribbon 贡献。**已完成 文件 / 脚本 / 视图**（`ShellComponent`，多面板）；数据页因含来源画廊暂留宿主。
3. 绘图页单独做宿主 `plotGallery`，再声明挂载。**已完成**：`plotGallery / layoutPicker / viewToggles` 均为宿主内建控件，由 `ShellComponent` 声明挂载。
4. Dock 改为 `DockContribution`，宿主统一建容器。**已完成**：`ShellComponent` 声明六个内置 Dock，`createDocks()` 按 order 遍历贡献。
5. 删除 `MainWindow` 中剩余的 `addCategoryPage` / `addDockWidget` 硬编码。**已完成**：六页 Ribbon 与六个 Dock 均由贡献生成。
6. 内置组件与外部扩展统一从 manifest 构建 `ContributionIndex`。**未完成**：当前仍手写 `addRibbon/addAction`，Ribbon 页已支持多面板与命令 ID。

## 当前状态

- `ComponentRegistry`、`CustomProtocolComponent`、`ExtensionComponent`、`ShellComponent` 已存在。
- 文件、脚本、视图页由 `ShellComponent` 贡献；协议页由 `CustomProtocolComponent` 贡献。
- 数据页的来源画廊（`sourceGallery`）与绘图页画廊/布局/显示控件也已由 `ShellComponent` 声明挂载；具体控件仍由宿主构建。
- 内置 Dock 已贡献化，`buildDockWidget(id)` 仅作为宿主内建面板兜底。
- 内外组件模型见 `PlotScope-组件与扩展扩展模型-v1.md`：内部组件走 C++ API 自建控件，外部扩展走 JSON manifest 由 app 建控件。
- `ProtocolDocument` 类型与顺序段编辑器已统一消费该模型。
- 本文件只记录决策与顺序，不代表组件扩展点重构已完成。
