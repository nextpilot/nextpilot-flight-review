# 路线图与方案讨论

本目录放**未实施方案、讨论稿、决策记录和历史基线**。已实现的能力写进 [`../manual/`](../manual/README.md)，可直接使用的成熟开发指南写进 [`../develop/`](../develop/README.md)。

| 文档 | 状态 | 内容 |
|---|---|---|
| [主题与浅色模式](theme.md) | 已实施（实现记录） | 深/浅主题 token、Ribbon/画布/Dock 配色、验收与截图 |
| [设置层：强类型 key 与变更通知](settings.md) | 已实施（2026-11-16） | `SettingsManager` 的类型约束、变更信号、文件 watcher/注册表轮询 |
| [脚本自动化](script-automation.md) | 规划中，暂不实施 | 脚本从派生计算升级为对象模型 + 命令总线 + 事务 |
| [历史架构基线与决策 v1.0](architecture-v1.md) | 历史记录 | 旧版架构决策、目录基线与实施记录；当前边界以 `AGENTS.md` 为准 |
| [旧组件/扩展模型 v1](extension-model-v1.md) | 历史记录 | 已被当前 [`../develop/extensions.md`](../develop/extensions.md) 取代的旧模型 |
| [扩展点决策 v1](extension-points.md) | 历史决策 | 组件扩展点、实施顺序与不做的部分 |
| [PJ4 对齐与源码核对](pj4-alignment.md) | 参考基线 | PlotJuggler 能力矩阵、源码事实与路线判断 |

## 当前优先级

1. 脚本自动化 P1：对象句柄与绘图控制，仍走命令总线。
2. 格式补齐：Parquet、MAVLink tlog 原生解析、更多 MCAP 索引能力。

新的方案先在 `roadmap/` 写清目标、边界、验收和风险，确认后再进 `develop/` 或代码。
