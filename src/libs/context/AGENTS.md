# AGENTS.md · libs/context（L1 运行态容器）

> 上级约束见 [`../AGENTS.md`](../AGENTS.md)，权威见 [`../../LAYERS.md`](../../LAYERS.md)。

## 本目录是什么

**进程级运行态的唯一持有者**：`AppContext`（数据集/来源/派生序列/设置/摄取管线/交互注册的门面）+ SettingsStore + IngestPipeline 等状态类。

## 允许放

- 可序列化的用户偏好与共享状态（SettingsStore、WorkspaceDataConfig、ChannelTransformConfig）
- 运行态账本（DerivedSeriesHub 等，经 AppContext 聚合）
- headless-safe 的门面对象（ScriptFacade 这类"翻译成意图"的入口）

## 禁止放

- **任何 QWidget / Qt Widgets 类型**——CLI（headless）与 GUI 共用同一份 AppContext，混入 GUI 类型即破"纯 Core"承诺
- 会话级/窗口级展示态（当前页签、视口缩放、面板显隐——归 gui，不可序列化者不入）
- 编译期静态资产（ParserHub / RecordingFormat 等自注册对象——与进程同生共死，收进来会造成多实例重复初始化）
- 对 host / app / gui / modules / extensions 的任何依赖

## 铁律

1. AppContext 唯一实例在 app 构造；其余各层**构造注入**拿引用，禁止全局取用、禁止自建实例
2. 模块取状态只走 `ctx.app()` 单一路径；禁止再造 `ctx.host().settings()` 类旁路
3. 新增运行态字段先问：可序列化吗？CLI 需要吗？——两个"否"就不许进 AppContext
