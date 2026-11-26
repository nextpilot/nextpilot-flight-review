# AGENTS.md · host/（L3 运行时宿主）

> 目录归属权威见 [`../LAYERS.md`](../LAYERS.md)。

## 本目录是什么

**运行时编排层**：模块装载/激活（RuntimeHost——模块编译期固定，仅启动期可按配置禁用）、意图执行（IIntentExecutor 实现）、扩展装配机制（Toolbox 装配 + PluginScanner 插件扫描，装配=组合根能力）、系统级意图处理器（DataLoadHandler 等）、模块契约消费方。

## 允许放

- 模块生命周期编排（依赖拓扑装载顺序、失败隔离；模块编译期固定，无运行时卸载产品能力）
- 意图路由与执行、**系统面**意图处理器（所有模块与外部入口共用的地基能力，如 data.load/data.series/workspace/record——见下方判据）
- 扩展装配入口（registerBuiltinPacks / scanExternalPlugins）
- 静态注册表上树/装配代码（static 注册表由 host 装配时构造上树）

## 禁止放

- 具体业务格式、领域判断、具体画布/面板实现
- 运行态持有——host 只拿 `AppContext&` 引用，**禁止自建仓库/设置副本**（状态两处分叉 = 上帝类温床）
- 任何对 shell/、gui/、modules/、toolbox/ 的依赖（含 include）
- 纯机制工具的**新增**落地（应进 libs；存量 `XlsxBook` 属历史错位，已记录待迁，禁止模仿它新增同类）
- 模块契约的定义（IModule/ModuleInfo 在 libs/module——host 只消费）

## 意图处理器归属（硬判据，新增 op 必查）

handler 是 IIntentHandler 实现（意图执行），不是 UI action 回调。归属按序判定：

1. **系统面：被所有模块 + 全部外部入口（GUI/CLI/脚本/MCP）消费，且在最小模块集/模块激活前必须可用**（CLI --batch、MCP 驱动不依赖任何可选模块）→ host。放任何单模块都会制造模块横向依赖。
2. **状态归 AppContext 的运行态管道**（摄取、数据账本、录制）→ host 只是执行者，数据面是 `AppContext` 状态的写入口，与状态同寿命。
3. **单模块私有语义** → 该模块。

现状清单（新增 op 时在此登记）：

| 位置                      | 意图族                                                                                                        |
| ------------------------- | ------------------------------------------------------------------------------------------------------------- |
| host：DataLoadHandler     | `data.load / remove / reload / recent / series / state` 等数据面入口                                          |
| host：DataCommandsHandler | `data.connect / export / exportSnapshot / record.* / workspace.* / cache / transform / time.align` 等系统命令 |
| host：AIToolsHandler      | `ai.status / ai.tools.list / ai.tool.call`                                                                    |
| modules/plot              | `plot.*`                                                                                                      |
| modules/script            | `script.*`（含 py.*/batch）                                                                                   |
| modules/settings          | `settings.* / theme.*`                                                                                        |
| modules/analysis          | `analysis.*`                                                                                                  |
| modules/protocol          | `protocol.*`                                                                                                  |
| modules/report            | `report.*`                                                                                                    |

## 动手前自查

1. 这段代码"协调谁来做"还是"自己做业务"？前者留 host，后者去 modules/toolbox
2. 这个意图 CLI --batch / MCP（最小模块集）也要用吗？要 → 系统面，放 host；只有单个模块用 → 放那个模块
3. 新意图 op：先过上面三条判据定归属，并把归属写进提交说明；单模块语义禁止塞进 host（host 是系统面不是杂物箱）
4. DataCommandsHandler 已是最大的 handler——新增 data 类 op 优先考虑独立成文件（如 RecordHandler/WorkspaceHandler），禁止继续往里堆
