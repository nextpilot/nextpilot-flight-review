# 路线图与方案讨论

本目录放**未实施方案、讨论稿、决策记录和历史基线**。已实现的能力写进 [`../manual/`](../manual/README.md)，可直接使用的成熟开发指南写进 [`../develop/`](../develop/README.md)。

| 文档 | 状态 | 内容 |
|---|---|---|
| [Dataset / DataSource v2](dataset-datasource-v2.md) | M0 开工（2026-11-17） | DataSource 聚合根、typed Table/Column、UTC 微秒主键、frame_batch、ring 窗口与 PlotSeries |
| [主题与浅色模式](theme.md) | 已实施（实现记录） | 深/浅主题 token、Ribbon/画布/Dock 配色、验收与截图 |
| [画布交互重构](canvas-interaction.md) | 已实施（2026-11-16） | 左键选择/书签/测量、中键平移、右键菜单/框选、轴区缩放、光标联动与固定读数 |
| [设置层：强类型 key 与变更通知](settings.md) | 已实施（2026-11-16） | 实施记录与关键决策；使用指南见 [`../develop/settings.md`](../develop/settings.md) |
| [扩展 toolbox 化与 autopilot 方案](toolbox-architecture.md) | 部分实施：四领域组件与源码已拆，分析与报告未实施 | 四业务工具箱、`datasources` 目录、GNSS/MAVLink 描述驱动与协议调试、dynamic 清理、飞控 review 与报告边界 |
| [脚本子系统（知识注入 + 自动化 + 判据）](script-automation.md)（[飞书](https://my.feishu.cn/file/PPNObKmVuouL51xBLvNc8nC9nAc)） | 实施中（S2 完成，S3 / S3b 待开工） | 一个引擎、三组 API 面（数据 / 绘图 / 判据）：脚本是知识注入通道，工具箱是出厂知识；算子 `create/calculate/reset` 同时服务实时推流与批量回放；判据必须多信号 + 状态 + 时序，`finding` 挂 dataset 不挂曲线；三级持久化（配置自动重建、脚本可调、呈现必须重跑）；marker 收为 `ps.claim/mark` 输出形态 |
| [Shell 架构解耦方案 v2](shell-architecture-v2.md) | 方案待实施（2026-11-17） | `Core` 管进程级服务、`MainWindow` 只做 Shell；Phase A 先拆 Property/Settings/Session/Log 反向依赖，Phase B 做 Core/Shell 分层，后续再上 Document/Workspace |
| [历史架构基线与决策 v1.0](architecture-v1.md) | 历史记录 | 旧版架构决策、目录基线与实施记录；当前边界以 `AGENTS.md` 为准 |
| [旧组件/扩展模型 v1](extension-model-v1.md) | 历史记录 | 已被当前 [`../develop/extensions.md`](../develop/extensions.md) 取代的旧模型 |
| [扩展点决策 v1](extension-points.md) | 历史决策 | 组件扩展点、实施顺序与不做的部分 |
| [PJ4 对齐与源码核对](pj4-alignment.md) | 参考基线 | PlotJuggler 能力矩阵、源码事实与路线判断 |

## 当前优先级

0. Core/Shell 架构调整：先拆 Property/Settings/Session/Log 到 `MainWindow` 的反向依赖，再把进程级服务上移到 `Core`，见 [`shell-architecture-v2.md`](shell-architecture-v2.md)。
1. 扩展 toolbox 化：四业务域、`datasources` 目录、CSV 归 common、dynamic 退出官方格式。
2. 飞控 autopilot：ULog / apbin / tlog 统一文件入口，默认曲线与报告。
3. 脚本子系统：控制台（REPL + 计算库 + 临时图）先行，再上自动化（Job + 事件触发 + 批处理 + 录制），最后提升为正式曲线（切片与开工前置见方案 §8）。
4. 格式补齐：Parquet、更多 MCAP 索引能力。
5. 单独评估：UI 撤销栈，与脚本模块解耦。

新的方案先在 `roadmap/` 写清目标、边界、验收和风险，确认后再进 `develop/` 或代码。
