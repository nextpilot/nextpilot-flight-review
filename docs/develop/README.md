# PlotScope 开发文档

本目录只放**已成熟、可以直接照着做**的开发指南。未定稿方案、讨论稿、决策记录和历史架构基线去 [`../roadmap/`](../roadmap/README.md)；仓库级硬边界见根目录 [`../../AGENTS.md`](../../AGENTS.md)。

用户使用说明见 [`../manual/`](../manual/README.md)。

| 文档 | 内容 |
|---|---|
| [当前架构与装配](architecture.md) | 运行时组成、目录职责、启动顺序、数据流、扩展装配 |
| [编译与构建](building.md) | 依赖、Qt/Lua 固定、CMake 配置、构建、运行 |
| [开发约定](development.md) | 分层规则、目录归属、命令/Dock/Ribbon/设置贡献、扩展开发 |
| [设置服务](settings.md) | key 清单、typed 读写、变更通知、跨进程同步、测试 |
| [测试](testing.md) | 单元测试、布局恢复测试、GUI 冒烟 |
| [组件与扩展模型](extensions.md) | 内置/动态扩展、Registry 贡献点、HostContext、生命周期 |
| [通用帧协议](frame-protocol.md) | 帧协议 schema、表达式、校验、编辑器 |

## 先看哪篇

- 第一次接手代码：根目录 `AGENTS.md` → 当前架构与装配 → 开发约定 → 编译与构建。
- 新增数据格式：组件与扩展模型 → 开发约定 → 测试。
- 新增或修改设置：设置服务 → 开发约定 → 测试。
- 改 shell/模块边界：`AGENTS.md` → 当前架构与装配 → 开发约定。
- 做能力对标或方案讨论：去 [`../roadmap/`](../roadmap/README.md)，不要写进本目录。
