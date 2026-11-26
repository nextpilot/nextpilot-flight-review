# 持久化与会话恢复

本文说明"什么东西存在哪、什么时候读写、谁负责触发"。判据先于实现：先看要存的东西属于哪一类，再决定它该落在哪一层，不要反过来——先找地方塞，再补理由。

## 一、三分法：先分类，再选位置

| 类别     | 判据                                                   | 落点                                                     |
| -------- | ------------------------------------------------------ | -------------------------------------------------------- |
| **资产** | 知识、模板、配方：同一份内容被多处复用，换机器也要一致 | 仓库或安装目录的文件（`.psproto`、预设模板、解析器定义） |
| **状态** | 一次工作现场：随时会变、关掉就要续上                   | 作用域存储（`SettingsStore` 键值）或结构化状态 hub       |
| **喜好** | 用户偏好：跨会话唯一一份，跟具体工作区无关             | `SettingsStore` 的 `app/` 段                             |

判错的典型后果：把工作现场写进 `app/` 段，用户换工作区后旧布局还粘着；把偏好写进文档段，同一台机器上两个工作区各有各的主题。

## 二、PersistentStateHub 契约

`context::PersistentStateHub` 只做**结构化状态的登记、收集与回填**，不持有文件路径——读写由外壳负责。

```cpp
context::PersistentStateEntry entry;
entry.id = "plot.document";
entry.scope = PersistentStateEntry::Document;  // Document 或 Session
entry.version = 1;
entry.bestEffort = true;
entry.serialize = [self]() -> QJsonObject { ... };
entry.restore = [self](const QJsonObject &doc) { ... };
appCtx.states().registerEntry(std::move(entry));
```

规则：

- **登记序即恢复序**。有依赖关系的条目按依赖顺序登记（数据源先于绘图，绘图先于会话视图态），恢复不再单独排程。
- **作用域两段**。`Document` 进配置段（可分享给别人），`Session` 进会话段（只对本机有意义）。同一份内容不要两段都写。
- **best-effort 默认开**。单条 restore 抛异常被吞掉，不影响其余条目；设成 `false` 表示"这条失败就该整体失败"，只用于硬约束。
- **collect 单条序列化失败只跳过该条**，不中断整体收集——退出路径不该被一条坏数据卡住。
- **条目自述、hub 不解释**。hub 不认识任何绘图或数据语义，新能力加条目即可，不改 hub。
  - **唯一例外：可选 `revision` 短路**（第1仗引入）。这是通用的"避免重复计算"机制，不含绘图/数据语义：条目可选提供 `std::function<quint64()> revision`，版本未变时 hub 复用上次序列化结果。它只省掉 serialize 的重算，**仍写入全量文档**——跳过写入会让状态从快照消失。留空则每次重算（默认，兼容所有现有条目）。
  - **接入约束**：`serialize()` 的输出必须只依赖 `revision` 覆盖的内容。若条目还有版本未覆盖的状态也在 serialize 里，必须把它们纳入 revision，否则会写出过期快照。当前所有条目均未设 revision（plot.document 尚无可靠版本号源，接入前需先建立变更计数），故此例外目前只是能力预留。

登记时机：构造期（控件/模块一建好就登记）。恢复时机：shell 显式触发（见下"时机分离"），不要塞进构造函数。

## 三、三份文件

### `settings.json`（SettingsStore 键值）

相对键 + 三个作用域（App / Workspace / Document）。标量偏好、最近列表、时间格式走这里。

纪律：**每个上界面的键必须有真实消费方**。没有消费方的键不写，避免设置文件里堆"改了没反应"的开关。

### `workspaces/<name>.psw`（工作区预设）

一份按名字存的工作现场，可拷给别人：

```jsonc
{
  "version": 1,
  "kind": "preset",
  "name": "...",
  "savedAt": "ISO8601",
  "sourceBinding": "bound", // bound | generic
  "sources": [/* 源条目原文 */],
  "plots": {/* PlotDocumentSpec，含 datasetOrigin */},
}
```

- `bound`：源条目全量，含 `parserParams` 原文，打开时按原文重建解析配置。
- `generic`：只留 `origin` / `kind` / `formatHint` / `parserIds`，**清空 `parserParams`**。用于把预设交给装不了同名解析器的机器——保留参数而没有解析器，等于给恢复流程埋一颗必炸的雷。
- 条目级可选键 `originResolved`：相对 `.psw` 所在目录记录的来源路径。解析顺序是"`origin` 可用就直接用，否则按 `originResolved` 相对解析，多个候选时用 `contentSha256` 校验同名源"。
- `version > 1` 拒绝打开并说明会丢失什么，不静默降级——静默降级会让用户以为布局完好，实际少了一半。

### `session/last-session`（会话快照）

退出时写、启动时读，两段结构：

```jsonc
{ "version": 1, "kind": "session", "savedAt": "ISO8601",
  "config":  { "sources": [ /* ws/data/sources 原文 */ ], "plots": { /* plot.document */ } },
  "session": { "canvas": {...}, "editor": {...}, "dataPanel": {...} } }
```

- `config` 段是恢复数据现场的唯一依据。
- `session` 段是各面板自述的运行态，**未知字段一律忽略**：模块增删不该让恢复失效。
- 写用 `QSaveFile`：先写临时文件再原子替换，崩溃不会留下半截快照。

## 四、时机分离

| 时机                   | 动作                                              |
| ---------------------- | ------------------------------------------------- |
| 构造期                 | `registerEntry`（只登记，不读不写）               |
| 启动（shell 显式触发） | 读快照 → 回填 config → `states().restore(hubDoc)` |
| 运行中                 | 各控件自己改内存状态，不主动落盘                  |
| 退出（`closeEvent`）   | `states().collect()` → 原子写快照                 |

启动恢复分三档（`app/startup.recovery`，默认 `layout`）：

- `off`：不恢复。
- `layout`：回填源条目 + 重建画布，**不读数据**。曲线留空态并挂入 pending binder（晚到绑定，对标 PJ4 PendingDisplayBinder）：数据源一登记（`sourceAdded`/`datasetAdded` 信号）就按 `datasetOrigin` 自动接回曲线、回放存盘视口。每个 pending 条目在 `SourceHub` 上持有按 origin 计数的 demand 引用（`demandOrigin/releaseOrigin`）——源被移除后需求仍在，同 origin 的源再次登记即自动接回；恢复前由 `preflightDocumentSpec` 报告 origin/channel 可满足性。
- `full`：`layout` 的全部步骤 + 执行 `data.workspace.restore` 摄取。

**数据集替换与重绑**（对标 PJ4 `rebindCurveKeys`/`planDatasetChoice`）：`reload` 用稳定 datasetId（曲线不失联）；`data.remove` 后重载会换 id——画布曲线不随之删除（kept 语义），`sourceAdded`/`datasetAdded` 信号触发按 `table.column` 三路解析自动接回（旧 id 仍有效→不动；唯一数据集携带→静默重绑；多个携带→取登记序第一个并报告）。同一 origin 带多个数据集（扇出/重复加载）时，`chooseDatasetFor` 按画布通道可满足性选最佳拟合，并列取登记序第一个并写诊断。

**流式源在任何档位都只保留条目，不自动连接**。连流会卡住启动等网络，而"上次开着"不等于"这次授权重连"。

## 五、与意图层的分工

- `settings.get` / `settings.set`：管**运行时单值**（用户改了立刻可读）。
- `PersistentStateHub`：管**生命周期**（退出收集、启动回填）。
- **同一键禁止两头写**。已经进 hub 的条目不要再往 `settings.json` 写一份，否则出现两个真相来源，谁后写谁赢，问题极难复现。

CLI 与 GUI 共用同一套持久化路径：`settings.json` 需要 `flush()` 才落盘，CLI 侧由 `SettingsFlushGuard` 在退出前统一 flush（`SettingsStore` 只改内存）。新增 CLI 入口时不要漏掉这一步。

## 六、插件与流配置

插件配置、流地址这类内容**存模板原文，不在保存时展开**。展开后的值绑定了当前环境，拷到别处必错；原文可以在打开时按当前环境重新解释。

流式源的连接参数属于"要用户放行"的范畴：恢复只重建条目，连接动作永远由用户触发（或显式脚本调用）。
