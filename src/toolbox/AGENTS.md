# AGENTS.md · toolbox/（L4 领域工具箱）

> 目录归属权威见 [`../LAYERS.md`](../LAYERS.md)。

## 本目录是什么

**工具箱（Toolbox，内部扩展）——业务的承载者**：common / autopilot / automotive / robotics / gnss。内容单元是**扩展**：一份纯数据清单（uiActions/panels/scripts/dataAssets/docs/examples/reports + 解析器；代码载体为 `Pack` 结构），`mount()` 进 ToolboxHub（即 `AppContext::toolbox()`），按 provider 原子装卸。静态自注册（PS_REGISTER_PACK）但**运行时允许加载/卸载**。Plugin（外部扩展，第三方经 C ABI 扫描注册）与本目录同属扩展、无本质区别，其实装属 M3（装配入口在 `application/host` 的 Toolbox/PluginScanner）。术语定标见 `../LAYERS.md` 词表。

## 允许放

- 扩展的领域贡献数据（各 Contribution 结构的填充，清单载体为 `Pack`）
- 消息解析器（包内**唯一的行为式条目**，所有权归注册表）
- 领域专属面板实现（gui 侧注册进 PanelFactory，形态由扩展决定）

## 禁止放

- **装配/registry 目录**——装配是组合根能力（Toolbox/PluginScanner 在 `application/host`），禁止在 toolbox/ 下重建任何装配、注册、扫描类目录
- 跨包业务逻辑、其他包的私有头（同层横向，硬线）
- 通用机制（通用格式解析归 common 包，通用算法归 libs）
- 对 application/gui/、application/app/ 的依赖

## 领域面板怎么写（契约见 `../LAYERS.md`「UI 归属与装载契约」）

领域专属面板实现放本包 `gui/` 子目录，单独 `<x>_gui` target（**只被 GUI 可执行文件链接**，CLI 二进制里物理不存在）：面板 TU 里 `PS_REGISTER_PANEL(type, ...)` 自注册进 PanelFactory；包清单里以 PanelSpec 声明形态，GUI 经统一的 `requestPanel` 按需创建。工厂签名带 `AppContext + IIntentExecutor`——面板取数走意图层，禁止直捅 Dataset 内部。headless 包 target 严禁链 Qt Widgets。

## 动手前自查

1. 新增一个领域包 = 新建目录 + 各 TU 静态自注册（PS_REGISTER_PACK）+ 根 CMakeLists 一行——宿主与装配层**零改动**。如果你的方案需要改 host/app 才能挂新包，设计错了
2. 这条知识是"某领域特有"吗？不是（CSV、通用协议等所有领域共用）→ 归 common 包
3. 包之间要共享代码？→ 下沉 libs 或并包，禁止 include 对方
