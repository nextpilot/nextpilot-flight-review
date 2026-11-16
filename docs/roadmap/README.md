# 路线图与方案讨论

本目录放**未实施方案、讨论稿、决策记录和历史基线**。已实现的能力写进 [`../manual/`](../manual/README.md)，可直接使用的成熟开发指南写进 [`../develop/`](../develop/README.md)。

| 文档 | 状态 | 内容 |
|---|---|---|
| [主题与浅色模式](theme.md) | 已实施（实现记录） | 深/浅主题 token、Ribbon/画布/Dock 配色、验收与截图 |
| [设置层：强类型 key 与变更通知](settings.md) | 已实施（2026-11-16） | 实施记录与关键决策；使用指南见 [`../develop/settings.md`](../develop/settings.md) |
| [扩展 toolbox 化与 autopilot 方案](toolbox-architecture.md) | 讨论中，未实施 | 四业务工具箱、`datasources` 目录、GNSS/MAVLink 描述驱动与协议调试、dynamic 清理、飞控 review 与报告边界 |
| [脚本自动化](script-automation.md) | 规划中，暂不实施 | 脚本从派生计算升级为对象模型 + 命令总线 + 事务 |
| [历史架构基线与决策 v1.0](architecture-v1.md) | 历史记录 | 旧版架构决策、目录基线与实施记录；当前边界以 `AGENTS.md` 为准 |
| [旧组件/扩展模型 v1](extension-model-v1.md) | 历史记录 | 已被当前 [`../develop/extensions.md`](../develop/extensions.md) 取代的旧模型 |
| [扩展点决策 v1](extension-points.md) | 历史决策 | 组件扩展点、实施顺序与不做的部分 |
| [PJ4 对齐与源码核对](pj4-alignment.md) | 参考基线 | PlotJuggler 能力矩阵、源码事实与路线判断 |

## 当前优先级

1. 扩展 toolbox 化：四业务域、`datasources` 目录、CSV 归 common、dynamic 退出官方格式。
2. 飞控 autopilot：ULog / apbin / tlog 统一文件入口，默认曲线与报告。
3. 脚本自动化 P1：对象句柄与绘图控制，仍走命令总线。
4. 格式补齐：Parquet、更多 MCAP 索引能力。

新的方案先在 `roadmap/` 写清目标、边界、验收和风险，确认后再进 `develop/` 或代码。
