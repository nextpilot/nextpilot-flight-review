# 路线图与方案讨论

本目录放**未实施方案、讨论稿、决策记录和历史基线**。已实现的能力写进 [`../manual/`](../manual/README.md)，可直接使用的成熟开发指南写进 [`../develop/`](../develop/README.md)。

| 文档 | 状态 | 内容 |
|---|---|---|
| [解析器插件模型 v2](parser-spi-v2.md) | 方案待开工（2026-11-18） | 解析器自描述接口（名称/领域/传输/参数 schema）+ 字符步进基类 `parseChar`，对话框直接消费描述信息 |
| [自定义协议搬迁](protocol-migration.md) | 方案待开工（2026-11-18） | .psproto 变长变体协议：模型/解码/驱动/编辑器分 L1–L4 迁入 ng；头部全搬、变体定长变长都支持、CRC 校验一起搬 |
| [fastplot 独立绘图库](fastplot.md) | P1 开工（2026-11-18） | 把老 plotting 的绘图能力抽成 `ng/libs/fastplot`；与 Qwt 同构的接口、零拷贝 + 像素列 LOD + 图层缓存三条硬约束、P1~P5 分批与验收 |
| [Dataset / DataSource v2](dataset-datasource-v2.md) | M0 开工（2026-11-17） | DataSource 聚合根、typed Table/Column、UTC 微秒主键、frame_batch、ring 窗口与 PlotSeries |
| [主题与浅色模式](theme.md) | 已实施（实现记录） | 深/浅主题 token、Ribbon/画布/Dock 配色、验收与截图 |
| [画布交互重构](canvas-interaction.md) | 已实施（2026-11-16） | 左键选择/书签/测量、中键平移、右键菜单/框选、轴区缩放、光标联动与固定读数 |
| [设置层：强类型 key 与变更通知](settings.md) | 已实施（2026-11-16） | 实施记录与关键决策；使用指南见 [`../develop/settings.md`](../develop/settings.md) |
| [扩展 toolbox 化与 autopilot 方案](toolbox-architecture.md) | 部分实施：四领域组件与源码已拆，分析与报告未实施 | 四业务工具箱、`datasources` 目录、GNSS/MAVLink 描述驱动与协议调试、dynamic 清理、飞控 review 与报告边界 |
| [脚本子系统（知识注入 + 自动化 + 判据）](script-automation.md)（[飞书](https://my.feishu.cn/file/PPNObKmVuouL51xBLvNc8nC9nAc)） | 实施中（S2 完成，S3 / S3b 待开工） | 一个引擎、三组 API 面（数据 / 绘图 / 判据）：脚本是知识注入通道，工具箱是出厂知识；算子 `create/calculate/reset` 同时服务实时推流与批量回放；判据必须多信号 + 状态 + 时序，`finding` 挂 dataset 不挂曲线；三级持久化（配置自动重建、脚本可调、呈现必须重跑）；marker 收为 `ps.claim/mark` 输出形态 |
| [Shell 架构解耦方案 v2](shell-architecture-v2.md) | Phase A 已实施；Phase B 由 [PJ4 架构迁移](pj4-architecture-migration.md) 接替 | Phase A 拆 Property/Settings/Session/Log 反向依赖；原 Phase B 的 `Core/Workspace/ProjectContext` 不再作为目标 |
| [PJ4 架构迁移方案](pj4-architecture-migration.md) | 部分实施：B1 完成；B4 部分完成；B5 四个矩阵通过；B2/B3 待实施 | `MainWindow` 只组装、`modules` 自贡献、`extensions` 可裁剪；已落地 `ps_runtime + ModuleContext + ServiceRegistry`、runtime Bridge 空实现、SettingsStore/LogService 与模块构建矩阵 |
| [历史架构基线与决策 v1.0](architecture-v1.md) | 历史记录 | 旧版架构决策、目录基线与实施记录；当前边界以 `AGENTS.md` 为准 |
| [旧组件/扩展模型 v1](extension-model-v1.md) | 历史记录 | 已被当前 [`../develop/extensions.md`](../develop/extensions.md) 取代的旧模型 |
| [扩展点决策 v1](extension-points.md) | 历史决策 | 组件扩展点、实施顺序与不做的部分 |
| [PJ4 对齐与源码核对](pj4-alignment.md) | 参考基线 | PlotJuggler 能力矩阵、源码事实与路线判断 |

## 当前优先级

0. **ng 新骨架（当务之急）**：`ng/libs/fastplot` 抽取绘图库（P1 开工），随后按批把画布、领域面板搬到 ng；老 `src/` 只读参考，不删不改。见 [`fastplot.md`](fastplot.md)。
1. PJ4 架构迁移：`ps_runtime + ModuleContext + ServiceRegistry` 与模块构建矩阵已落地；下一步拆 datasource/plotting 的 core/widget，见 [`pj4-architecture-migration.md`](pj4-architecture-migration.md)。
2. 扩展 toolbox 化：四业务域、`datasources` 目录、CSV 归 common、dynamic 退出官方格式。
3. 飞控 autopilot：ULog / apbin / tlog 统一文件入口，默认曲线与报告。
4. 脚本子系统：控制台（REPL + 计算库 + 临时图）先行，再上自动化（Job + 事件触发 + 批处理 + 录制），最后提升为正式曲线（切片与开工前置见方案 §8）。
5. 格式补齐：Parquet、更多 MCAP 索引能力。
6. 单独评估：UI 撤销栈，与脚本模块解耦。

新的方案先在 `roadmap/` 写清目标、边界、验收和风险，确认后再进 `develop/` 或代码。
| [psproto v2](psproto-v2.md) | **讨论定稿，待验收**（2026-11-19） | 表达力统计、类型规则、§12 定稿决策与开工前验收清单、**§13 交接清单（A/B/C 文档消项 + D 业务问题）**；协议本体见 [SPEC.md](../ng/libs/protocol/SPEC.md) |
| [psproto S0 施工方案](psproto-s0-plan.md) | **部分被 v2 §12 取代，待修订**（2026-11-19） | 早期起步稿：时间路径 B 方案、size 改字节已废止（时间改走 A typed）；崩溃修复/fuzz/回滚仍成立；**§7 分批表作废，以 v2 §5 为准**，施工前先消 [v2 §13 交接清单](psproto-v2.md) |
| [psproto v2 规范（协议本体）](../libs/protocol/SPEC.md) | **已定稿**（2026-11-19） | 段键表、emit 三态、digest/inject、位域、ASCII、覆盖矩阵 8/8、12 项交付标准 |
| [器官补齐架构设计](modules-organ-completion.md) | **设计定稿，待拍板**（2026-11-19） | module 边界裁决（不拆 data、真欠 report）、`mod.report` 设计（软依赖 analysis）、能力契约、4 批落地建议；拍板后交实现 |
