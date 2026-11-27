# PlotScope 开发文档

本目录只放**已成熟、可以直接照着做**的开发指南。仓库级硬边界与协作要求见根目录 [`AGENTS.md`](../../AGENTS.md)；架构决策的完整论证见 [`src/ARCHITECTURE.md`](../../src/ARCHITECTURE.md)。

用户使用说明见 [`../manual/`](../manual/README.md)；长期路线见 [`../roadmap/README.md`](../roadmap/README.md)，具体任务在 gitee issue 跟踪，不在仓库写 plan 文件。

| 文档                                        | 内容                                                                |
| ------------------------------------------- | ------------------------------------------------------------------- |
| [架构概览](architecture.md)                 | 分层结构、核心抽象、数据流、扩展装配（精简版）                      |
| [编译与构建](building.md)                   | 依赖、Qt/Lua 固定、CMake 配置、构建、运行                           |
| [构建加速](build-acceleration.md)           | 依赖镜像（nphelper/gitee）、ccache、测试目标裁剪、排查              |
| [推送前门禁](push-gate.md)                  | 三档本地检查、与 CI job 的对应、重排/绕过禁令、合并前口径           |
| [测试](testing.md)                          | 单元测试、GUI 冒烟、契约测试                                        |
| [协议规范](../../src/libs/protocol/SPEC.md) | psproto v3 语义权威（速览见同目录 README）                          |
| [uischema 语法](uischema-syntax.md)         | 属性/表单 schema 的字段语法与渲染约定                               |
| [持久化与会话恢复](persistence.md)          | 三分法判据、状态 hub 契约、三份文件格式、恢复档位、与意图层分工     |
| [绘图后端对比](plot-backends-benchmark.md)  | 自绘 vs QCustomPlot/Qwt 等后端的性能与功能结论（fastplot 选型依据） |

## 先看哪篇

- 第一次接手代码：根目录 `AGENTS.md` → [架构概览](architecture.md) → [编译与构建](building.md) → [测试](testing.md)。
- 改分层/新增目录：`src/LAYERS.md`（目录定位唯一权威，新增代码先在这里判归属）。
- 新增数据格式：[架构概览](architecture.md) 解析器一节 → `src/LAYERS.md` → [测试](testing.md)。

## 任务跟踪

任务不写仓库 md，一律走 gitee issue：里程碑与 issue 列表见仓库首页 Issues 区。任务规范与开发流程（Plan 内容 + GitHub Flow：先建 issue → 特性分支 → 回填 → PR → 合并后关 issue）统一见根目录 `.claude/skills/ai-develop-workflow`。
