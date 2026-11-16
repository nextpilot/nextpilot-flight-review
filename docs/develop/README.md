# PlotScope 开发文档

本目录面向开发和评审人员。用户使用说明见 [`../manual/`](../manual/README.md)。

| 文档 | 内容 |
|---|---|
| [架构设计](architecture.md) | 分层、数据模型、工作区、扩展 ABI、PJ4 对标与实施记录 |
| [编译与构建](building.md) | 依赖、Qt/Lua 固定、CMake 配置、构建、运行 |
| [开发约定](development.md) | 分层规则、目录归属、命令/Dock/Ribbon/设置贡献、扩展开发 |
| [测试](testing.md) | 单元测试、布局恢复测试、GUI 冒烟 |
| [扩展模型](extensions.md) | 组件贡献、Registry、HostServices、Dock/Ribbon/Settings 扩展点 |
| [扩展点决策](extension-points.md) | 已确定的扩展点和不做的部分 |
| [通用帧协议](frame-protocol.md) | 帧协议 schema、表达式、校验、编辑器 |
| [PJ4 对齐](pj4-alignment.md) | PlotJuggler 能力矩阵、源码核对与验收口径 |

## 先看哪篇

- 第一次接手代码：架构设计 → 开发约定 → 编译与构建。
- 新增数据格式：扩展模型 → 开发约定 → 测试。
- 改 shell/模块边界：架构设计 → 开发约定。
- 做能力对标：PJ4 对齐 → 架构设计。
