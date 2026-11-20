# 路线图与方案讨论

本目录放**未实施方案、讨论稿、决策记录和历史基线**。已实现的能力写进 [`../manual/`](../manual/README.md)，可直接使用的成熟开发指南写进 [`../develop/`](../develop/README.md)。

> **当前总体状态（2026-11-20）**：本目录所列方案**已全部实施完毕**（ng 新架构 212/212 任务收口），仅 `toolbox-architecture.md` 中"领域报告成品"与"外部协议调试面板"两项作为领域扩展预留，随领域包推进。新的方案先在 `roadmap/` 写清目标、边界、验收和风险，确认后再进 `develop/` 或代码。

| 文档 | 状态 | 内容 |
|---|---|---|
| [解析器插件模型 v2](parser-spi-v2.md) | ✅ 已实施（2026-11-20） | 解析器自描述接口（名称/领域/传输/参数 schema）+ 字符步进基类 `parseChar`，对话框直接消费描述信息；统一为 `IMessageParser`+`configure` 双路径 |
| [psproto v2](psproto-v2.md) | ✅ 已实施（2026-11-20） | .psproto 模板协议库：段模型/流式解析/ASCII 与混合/摘要与脚本/搬迁记录（§14–§15）；**v3 重做已由另一工作线完成并合入**；协议本体见 [`ng/libs/protocol/SPEC.md`](../../ng/libs/protocol/SPEC.md) |
| [fastplot 独立绘图库](fastplot.md) | ✅ 已实施（2026-11-20） | `ng/libs/fastplot`：Figure/Axes/Artist 分层 + MATLAB 风格 API，零拷贝 + 像素列 LOD + 图层缓存；状态视图/Marker/图例/对齐等已在其上落地 |
| [Dataset / DataSource v2](dataset-datasource-v2.md) | ✅ 已实施（2026-11-20） | DataSource 聚合根、typed Table/Column、UTC 微秒主键、frame_batch、ring 窗口；已下沉 `ng/libs/dataset`（Rust 内核 + RAII 门面） |
| [主题与浅色模式](theme.md) | ✅ 已实施（实现记录） | 深/浅主题 token、Ribbon/画布/Dock 配色、验收与截图 |
| [画布交互重构](canvas-interaction.md) | ✅ 已实施（2026-11-16） | 左键选择/书签/测量、中键平移、右键菜单/框选、轴区缩放、光标联动与固定读数 |
| [设置层：强类型 key 与变更通知](settings.md) | ✅ 已实施（2026-11-16） | 实施记录与关键决策；使用指南见 [`../develop/settings.md`](../develop/settings.md) |
| [扩展 toolbox 化与 autopilot 方案](toolbox-architecture.md) | ✅ 主体已实施；领域报告/调试面板随领域包预留 | 四业务工具箱、`extensions/<域>/` 目录、CSV 归 common、ULog 归 autopilot、dynamic 退出官方格式；CLI `--list-formats` 按领域分组 |
| [脚本子系统（知识注入 + 自动化 + 判据）](script-automation.md)（[飞书](https://my.feishu.cn/file/PPNObKmVuouL51xBLvNc8nC9nAc)） | ✅ 已实施（A–E 全落地，2026-11-20） | Lua `app` 全局对象、有状态过滤器引擎 + 12 内置过滤器库 + 管道接入、Marker 规则引擎（when DSL）、AI/MCP 工具层意图 API（§1–§21 为逐轮实施记录） |
| [Shell 架构解耦方案 v2](shell-architecture-v2.md) | ✅ Phase A 已实施；Phase B 由 PJ4 架构迁移完成 | Phase A 拆 Property/Settings/Session/Log 反向依赖；原 Phase B 的 `Core/Workspace/ProjectContext` 不再作为目标 |
| [PJ4 架构迁移方案](pj4-architecture-migration.md) | ✅ 已实施（B 系列全部落地，2026-11-20） | B1 Result 下沉、B2 dataset 抽库、B3 registry、B4 intent、B5 uischema、B6 解析器合并/命名、B7 namespace 收口 + core 消亡、B8 Transform；守卫固化于 `ng/cmake/LibBoundaryGuard.cmake` |
| [器官补齐架构设计](modules-organ-completion.md) | ✅ 已实施（2026-11-20） | source/dataset 拆分、模块边界 ≠ UI 边界、命名裁决全部落地；report 保留扩展点/契约/意图常量 |
| [dataset 抽库设计](dataset-lib-extraction.md) | ✅ 已实施（2026-11-20） | `Dataset`/`DataRepository` 下沉 `ng/libs/dataset`；独立链接验收 `ng_test_dataset_standalone` |
| [机制抽库设计](mechanism-libs-extraction.md) | ✅ 已实施（2026-11-20） | recording 归属、泛型 `Registry<T>`、`ng_intent`、`ng_uischema`（SchemaEngine 统一渲染器）、`IMessageParser` 正名全部落地 |
| [Core 重构总方案（开工版）](core-refactor-master-plan.md) | ✅ 已全部落地（2026-11-19） | core 目录/target 删除，全仓零 `core::` 残留；libs 铁律 + configure 期依赖守卫 |
| [历史架构基线与决策 v1.0](architecture-v1.md) | 历史记录 | 旧版架构决策、目录基线与实施记录；当前边界以 `AGENTS.md` 为准 |
| [旧组件/扩展模型 v1](extension-model-v1.md) | 历史记录 | 已被当前 [`../develop/extensions.md`](../develop/extensions.md) 取代的旧模型 |
| [扩展点决策 v1](extension-points.md) | 历史决策 | 组件扩展点、实施顺序与不做的部分 |
| [PJ4 对齐与源码核对](pj4-alignment.md) | 参考基线 | PlotJuggler 能力矩阵、源码事实与路线判断 |

## 当前优先级

**无进行中项**——本目录方案已全部实施。后续候选方向（未立项，做时先在此写方案）：

1. 内核 string ABI：string 状态通道读取（状态视图当前对 string 列返回 `etype`）。
2. MCP server 协议外壳：AI 工具层意图 API（`ai.tools.list` / `ai.tool.call`）已就绪，只差标准 MCP 协议壳。
3. 真实 live 数据引擎：ring 缓冲 + 推送运行时（live 暂停接口已预留，见 roadmap §19）。
4. 领域驱动补齐：automotive/gnss/robotics 驱动与领域面板（知识/呈现两层结构已就位）。
5. 2D/3D 视图、市场/对比视图（大工程，独立方案）。
