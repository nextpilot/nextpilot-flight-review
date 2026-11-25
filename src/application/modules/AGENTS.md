# AGENTS.md · modules/（L4 模块）

> 目录归属权威见 [`../LAYERS.md`](../LAYERS.md)。

## 本目录是什么

**模块（Module）——app 的基底功能，没有业务知识**：data / plot / script / protocol / analysis / settings / report。每个实现 `libs/workbench` 的 IWorkbenchContribution，在各自 TU 静态自注册（PS_REGISTER_MODULE），随 app 编译、宿主按依赖拓扑装载。**编译期固定：只能编译时增删，无运行时卸载概念**（术语定标见 `../LAYERS.md` 词表）；业务知识归扩展（工具箱/插件，见 `../../toolbox/`）。

## 允许放

- 实现 IWorkbenchContribution 的模块代码（initialize 声明能力/意图处理器、onAttach 建面板）
- 本模块的意图处理器、面板工厂、专属视图逻辑（UI 经 IUiContext 请求，不直接建窗）
- 本模块目录内的子结构（子目录私有，不外泄）

## 禁止放

- **include 其他模块或扩展的任何头**（同层横向，硬线）——模块间只经 host 的注册与意图协作
- include `../gui/`、`../shell/` 的任何头（UI 经 IUiContext；面板经 PanelFactoryHub 按类型请求）
- 运行态自持（数据/设置一律经 `ctx.app()`；发现自己在模块里 new DatasetHub 就是违规）
- 新建"共享工具"目录（跨模块共用 → 下方 libs，禁止在 modules/ 里造公共层）
- 通用机制（算法、格式、契约）——放 libs 才能被复用

## UI 素材与编译分离（契约见 `../LAYERS.md`「UI 归属与装载契约」）

**素材物理聚合，编译期分离**：本功能的全部素材——模块逻辑、意图处理器、面板、对话框、模型——物理上都放本目录；CMake 拆两个 target：

- `ps_module_<x>`：headless 部分，只链 Qt Core，CLI/GUI 都装载
- `ps_module_<x>_gui`：Qt Widgets 部分，**只被 GUI 可执行文件链接**（OBJECT 库，静态自注册依赖 TU 构造函数），CLI 二进制里物理不存在

写 UI 走两条通道：① override `contributes()` 返回声明式数据（Ribbon 页/组/动作，宿主统一灌回）；② 面板实现 `PS_REGISTER_PANEL(type, ...)` 自注册进 PanelFactoryHub，模块用 `ui()->requestPanel(Region, type)` 按枚举区域请求。**headless target 严禁链 Qt Widgets 或 include 本模块 `*_gui` 头**。

## 动手前自查

1. 新意图 op：别的模块也会处理它吗？会 → 归 host 系统级处理器；只本模块 → 放这里
2. 需要一个新面板：经 IUiContext.requestPanel 走工厂表，不直接操作 MainWindow
3. 想链接另一个模块？停下来，用意图或 host 注册机制重新设计
