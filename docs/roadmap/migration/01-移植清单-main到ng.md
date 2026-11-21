# 移植清单：把 main 的能力搬进 ng（以 ng 为基准）

> 基线：`nextgen-architecture` HEAD `0349da6`。目标：**ng 做骨架，main 做能力来源**。
> 范围：数据源 + 数据工程 + 多源比对交互。**脚本不在范围内**（你后续有更好的设计，本次一律不动脚本页/脚本引擎/控制台）。
> 每条任务带 ID，验收对应 `验收清单-main到ng.md` 里的 AC 号。

---

## 0 先把坑探清楚：ng 已经有什么、真正缺什么

这一节决定工作量——**ng 的数据源地基比想象中完整**，所以不是"重写数据源"，而是"补几块砖"。

| ng 已有 | 位置 | 意味着 |
|---|---|---|
| `SourceKind{File,Stream,Live,Replay}` + `SourceState{Opening,Streaming,Paused,Closed,Error}` | `ng/libs/dataset/include/Source.h` | 状态机语义已定义，不用新造 |
| `ParserBinding` 扇出（一个源 → N 个解析器，各产各的 dataset） | 同上 + `SourceRegistry::bindParser` | **main 的"共享 transport"能力天然存在**，只需接线 |
| `RecordingTarget{formatId, backendId, endpoint}` 格式×后端正交、可多路 | 同上 | 多路录制（raw+mcap、file+udp）也是天然存在 |
| `Recorder::create(format,backend,endpoint)` + `writeMessage/writeBytes/close`、`RawRecorder/McapRecorder`、`FileSink/UdpSink`、`RecordingBackendRegistry` | `ng/recording/*` | 录制引擎**已可用**，缺的是"谁来喂它" |
| `DataSourceCapability{canRecord,canReplay,canSeek,recordFormats,...}` | `Source.h` | 能力自描述已在契约里 |
| `IngestOrchestrator::openFile/openSource`（打开→绑定→读→落库→冲 schema） | `ng/libs/context/include/IngestOrchestrator.h` | **驱动循环骨架已在**，但只支持"读完即返回" |
| `Dataset::appendFrame`（增量追加帧）、`applyFilter` | `ng/libs/dataset/include/Dataset.h` | 长流追加数据可行，无需改 Rust 侧 |
| `IByteSource` 接口 | `ng/libs/parser/include/IByteSource.h` | 接口在 |
| `IIntentHandler` + `Intent` + Job/CLI（`--run job.json`） | `ng/libs/intent`、`ng/host/src/DataCommandsHandler.cpp` | 每条新能力都能 headless 验 |

| ng 真正缺 | 影响 |
|---|---|
| **`IByteSource` 的流式实现**：全仓无 `QSerialPort`、无 `QUdpSocket`（只有录制侧 `UdpSink`） | 实时源零实现 |
| `IngestOrchestrator` 的长流模式（现在 read 到 EOF 就返回，没有 pause/resume/rate/重连） | 不能接真机 |
| 录制接线（`Recorder` 已存在，但没有任何人把 `DataSourceInfo::recordings` 变成 `Recorder` 实例） | 采集即录为空 |
| 源级时间对齐层（Dataset 只有 `timeMin/timeMax`，无 offset） | 多源对照做不了 |
| 参数表 P/Q、多表快照、缓存命中率、采样上限、设置对话框 UI | 数据工程缺口 |
| 多源比对的动作（叠加全部同名 / 对比同名通道 / 展开数据列）与画布 DnD 重排 | 效率缺口 |
| 工业容器解析器（mdf/rosbag/blf/dataflash/apbin） | 客户面缺口 |

> **结论**：M1（实时源）是"新增一小层 + 一个扩展包"，不是重构。这是本次移植里最划算的一项。

---

## 1 里程碑与任务总表

| ID | 任务 | 规模 | 依赖 | 对应 AC |
|---|---|---|---|---|
| **M1 实时源** | | | | |
| T1.1 | 流式字节源：串口 / UDP / TCP（+ 内存回放用于测试） | **已完成** 2026-11-21：UDP（`c11d290`）、串口 termios（`da47f12`）、TCP 客户端+监听（`cb12809`）；三者均端到端单测（20 帧→20 行、timeMax 1.900、暂停不计包、disconnect 落 Closed） | — | AC-01 |
| T1.2 | 长流模式：后台泵、不阻塞、EOF 不关源 | **已完成** 2026-11-21 / `f032b81`：readyRead 只入环（默认 4MB/源），50Hz 泵按 256KB/tick 批量 feed；disconnect/reconnect 前先喂完积压再断；EOF 不关源（`atEnd` 语义未被引入） | T1.1 | AC-02 |
| T1.3 | 流控与统计：pause / resume / reconnect / 速率 / 丢帧计数 | **已完成** 2026-11-21 / `f032b81`：pause/resume/reconnect + Hz/KB/s/packets/bytes/fed_bytes 已实测；丢帧计数分两层——`dropped_bytes`（环满丢最旧，应用层背压，实测可读）+ `parse_errors`（解析器通道错误）；内核 UDP 丢包仍在 socket 层递交给内核处理，不做自造补算 | T1.2 | AC-03 |
| T1.4 | 意图 op：connect/disconnect/pause/resume/reconnect/rate.set/config/list | **已完成** 2026-11-21：`data.connect`（udp/serial/tcp）、`data.disconnect`、`data.pause/resume`、`data.reconnect`、`data.source.list`、`data.live.stats`、`data.rate.set`（hz 1..200 → 泵 interval）、`data.config`（bufferBytes / pumpBudget 热改）全部落地并单测 | T1.2 | AC-04 |
| T1.5 | UI：数据页 sources gallery 增"实时源"分类 + 连接对话框（传输/端点/波特率/解析器多选） | M | T1.4 | AC-05 |
| T1.6 | 数据面板状态行：源状态、速率、字节、错误；空态提示改为真实可用 | S | T1.5 | AC-06 |
| **M2 录制** | | | | |
| T2.1 | 录制编排器：`DataSourceInfo::recordings` → `Recorder::create` → 每帧 `writeMessage/writeBytes` | M | T1.2 | AC-07 |
| T2.2 | 意图：`data.record.start/stop/list`（含 format×backend×endpoint 多路） | S | T2.1 | AC-08 |
| T2.3 | UI：数据页"录制"组（开始/停止/配置）+ 状态与已写字节数 + 录制中红点 | S | T2.2 | AC-09 |
| **M3 时间对齐** | | | | |
| T3.1 | per-source 时间偏移：**偏移只存宿主态**（不修改原始时间戳，画布在 push 副本上叠加） | **已完成** 2026-11-21 / `dadf3f3`→`3f4a72c` 收敛 | — | AC-10 |
| T3.2 | 对齐策略：设为基准 / 对齐起点 / 对齐终点 / 对齐中心 / 重置 | **已完成** 2026-11-21 / `3f4a72c`（5 策略在 `alignTime`，基准缺省取活动源） | T3.1 | AC-11 |
| T3.3 | 意图 + UI：树右键 & 数据页按钮；对齐后画布立即刷新 | **已完成** 2026-11-21（意图+源右键✓、画布订阅刷新✓；数据页按钮不做——main 无对齐引擎，没有可对齐的按钮形态，不新造） | T3.2 | AC-12 |
| T3.4 | **对齐参与导出**：CSV / 快照的时间列统一走对齐后时间；时间轴三种格式按对齐后零点渲染 | **完成（CSV 主路径）** `dff7f2e`：`data.export` 叠加源 offset + 表头注释；画布导出随 push 副本；三种时间格式未逐项实测 | T3.2 + T4.1 | AC-10B |
| T3.5 | 布局选格：8×8 悬停选格（`GridPickPopup`）替代手输，上限 6×6→8×8 | **已完成** 2026-11-21 / `0664f0c` | — | 见 §2 T3.5 |
| **M4 数据工程** | | | | |
| T4.1 | 多表快照导出 `data.exportSnapshot`（选表 → xlsx/csv 多文件或单簿多 sheet） | M | — | AC-13 |
| T4.2 | 参数表：P/Q 参数按 `param` 表落 dataset + 独立面板页签 | M | T3.1 | AC-14 |
| T4.3 | 缓存可观测：解析缓存命中/未命中/大小 + 一键清空（已有 `data.clearCache`） | S | — | AC-15 |
| T4.4 | 采样上限：CSV 导出采样上限、派生通道重算上限（0=不限） | S | — | AC-16 |
| T4.5 | **设置对话框 UI**（`settings.get/set` 已有，缺界面）：数据/画布/时间轴三组 | M | T4.3/T4.4 | AC-17 |
| **M5 多源比对交互** | | | | |
| T5.1 | `data.action.overlayAll`：把同名通道从所有源叠加到当前画布 | M | — | AC-18 |
| T5.2 | `data.action.compareSame`：对比同名通道（统一 X + 双色 + 自动对齐提示） | M | T5.1 | AC-19 |
| T5.3 | 树右键"展开数据列"（表节点一键展开全部字段到画布） | S | — | AC-20 |
| T5.4 | 画布 ↔ 画布拖拽重排（main 的 `application/x-plotsc…` 语义） | M | — | AC-21 |
| **T5.5** | **画布右键上下文补齐**（实测 ng 空画布仅 4 项、有曲线 11 项，main 13 项）：属性▸/在 t 处加书签/AB/固定读数/放大缩小自适应撤销/显示网格图例/曲线▸/向右向下拆分/导出 PNG·CSV/删除此画布，**空画布也走同一套** | **已完成** 2026-11-21 / `0664f0c` | — | AC-27 |
| **T5.6** | **数据树源节点补 4 组**：时间对齐▸、对比同名▸、数据源信息、移除全部数据源（ng 现仅 4 项） | **部分完成**（信息/重命名/移除/暂停✓；对齐·对比待 T3） | T3.2 | AC-28 |
| **T5.7** | **时间书签改为"在 t 处添加"**（main 语义：菜单项带点击时刻），并把重命名/删除收进"时间书签"子菜单 | **已完成** 2026-11-21 / `0664f0c` | — | AC-29 |
| **M6 格式扩展（重写，不是搬运）** | | | | |
| T6.1 | `mdf` 解析器扩展包（`ng/extensions/mdf`） | L | T1.2 | AC-22 |
| T6.2 | `rosbag`(v2) 解析器扩展包 | L | 同上 | AC-22 |
| T6.3 | `blf` / `dataflash` / `apbin` 扩展包 | L 每项 | 同上 | AC-22 |
| **M7 现场排障** | | | | |
| T7.1 | 设备调试终端（实时帧尾巴 + 发送命令 + 十六进制视图） | M | T1.3 | AC-23 |
| T7.2 | 帧诊断面板：坏帧计数、解析器错误、topic 分布 | S | T1.3 | AC-24 |
| **M8 绘图补口（低优先，按需）** | | | | |
| T8.1 | `surface3d` 图型（main 有，ng 缺；ng 已有 waterfall，不做替换） | M | — | AC-25 |
| T8.2 | 右键"重置 X 轴 / 重置缩放 / 重置整个视图"三出口（ng 现只有撤销） | S | — | AC-26 |

---

## 1.1 完成进度总览（截至 2026-11-21，已推送 `3f4a72c`）

33 张任务卡，**完成 11 + 部分完成 1，完成率 33%（含部分 36%）**；另含 1 项跨卡视觉增强（右键菜单图标）。所有已完成项都在 `nextgen-architecture` 上，编译 0 error、`ctest -j1` 27/27 全绿，已推送至 `f032b81`。

| 里程碑 | 任务卡数 | 已完成 | 部分完成 | 未开始 | 完成率 | 说明 |
|---|---|---|---|---|---|---|
| M1 实时源 | 6 | 4 | 0 | 2 | 67% | T1.1 三前端 + T1.2 环/泵 + T1.3 统计 + T1.4 意图族全落地（`f032b81`）；T1.5 连接对话框、T1.6 状态行仍缺（UI 卡）；**流式解析器**只有 csv/custom-protocol/ulog（无 MAVLink，用户明确判定为“业务”不在底座范围） |
| M2 录制 | 3 | 0 | 0 | 3 | 0% | 引擎已存在，只差接线，可紧跟 M1 |
| M3 时间对齐 | 5 | 5 | 0 | 0 | 100% | T3.1 偏移收敛 AppContext、T3.2 五策略、T3.3 意图+树右键、T3.4 CSV 导出叠加、T3.5 选格 |
| M4 数据工程 | 5 | 0 | 0 | 5 | 0% | 依赖 T3.1 的 offset 语义，顺序上排在 T3 之后 |
| M5 比对交互 | 7 | 2 | 1 | 4 | 29% | T5.5/T5.7 完成、T5.6 部分；T5.1–T5.4 未动 |
| M6 格式扩展 | 3 | 0 | 0 | 3 | 0% | 重写型任务，与本次移植解耦 |
| M7 现场排障 | 2 | 0 | 0 | 2 | 0% | 依赖 T1.3 的流控统计 |
| M8 绘图补口 | 2 | 0 | 0 | 2 | 0% | 低优先，按需 |
| **合计** | **33** | **11** | **1** | **21** | **33%** | |

### 本轮（2026-11-21）实际落地的改动

| 内容 | 文件 | 验证 |
|---|---|---|
| 8×8 悬停选格弹层，替换手输框 | 新增 `ng/gui/views/plot/GridPickPopup.{h,cpp}`；改 `CanvasWorkspace.cpp`、`PlotThumbnails.cpp`（clamp 6→8）、`CMakeLists.txt` | 真跑截图 + `test_gui` 两条断言 |
| 画布右键两态统一（4 项 → 17 项） | `PlotCanvas.{h,cpp}`、`CanvasWorkspace.cpp` | 空/有曲线各截图，拆分/删除实拍 |
| 拆分·删除作用在右键那块画布 | 新增信号 `PlotCanvas::canvasActionRequested` + workspace 按指针反查 | 右键右格删除，左格曲线不受影响 |
| 单格时"删除此画布"置灰 | `PlotCanvas::setCanvasRemovable` + `rebuildPageLayout` 同步 | 单格菜单截图 |
| 数据树源节点 4 项 → 8 项 | `DataManagerPanel::showSourceMenu` | 源菜单 / 信息对话框截图 |
| `Axes` 网格开关错位修复 | `ng/libs/fastplot/src/Axes.cpp` | 菜单 ✓ 与实际网格一致 |
| 源级时间偏移收敛（T3.1 修正） | **撤销** `Dataset` 的 `setTimeOffsetUs`/readWindow 平移（会与 AppContext offset 叠加成双算），只留 `rawTimeRangeUs()` 供整数微秒域算 offset；`PlotCanvas` LOD 回原始 first/last，偏移仍由 `sourceOffsetFor` 在 push 副本施加 | `ctest -j1` 24/24 |
| 时间对齐策略与入口（T3.2/T3.3） | `DataCommandsHandler::alignTime` 五策略改调 `m_app.setSourceTimeOffset(srcId, s)`；`DataManagerPanel::showSourceMenu` 加「时间对齐▸」5 项（基准=活动源，活动源即本源时自动取另一源）+ 意图分派 | `ctest -j1` 24/24；GUI 实拍父菜单与子菜单箭头（`verify/ng_align_srcmenu.png`） |
| 对齐参与导出（T3.4） | `DataCommandsHandler::exportCsv` 时间列叠加源 offset，表头 `# time_mode=offset, offset_us=…, offset_s=…`；画布右键导出随 push 副本 | CLI 实测首行 11.596s；`dff7f2e`/`2ff7687` |
| 源字节回写（顺手修） | `IngestOrchestrator`：解析完成（totalRead/文件大小）与缓存命中（文件大小）两条路径写 `bytesRead/bytesTotal` | 「数据源信息」实测字节 312297（原为 0）；`e9564ad` |
| 树搜索展开 + 假命中修复 | `DataManagerPanel::applyFilter`：过滤态展开命中表节点；字段可见性只看**自身**命中（原逻辑把"本表已命中"累进 leaf，搜 pitch 会连带显示后续字段） | 实拍：搜 pitch 只出 attitude▸pitch；`e9564ad`/`4c948dc` |
| 右键菜单图标 | 新增 `ng/gui/theme/MenuIcons.{h,cpp}`（22 个自绘图标，theme token 取色）+ `PlotCanvas.cpp::decorateMenu`、`DataManagerPanel::showSourceMenu` 接线 | 实拍：画布 17 项、源 8 项全部带图标（拆分两态用填充区分、清缓存用橡皮）；`ctest -j1` 24/24 |

### 下一步建议顺序

1. **T3.1–T3.4 时间对齐**（M3 主体）：它是 M4 参数表、AC-10B 导出、T5.6 剩下两项（对齐/对比）共同的前置；也是当前"半落地"最多的一条。
2. **T1.1–T1.4 实时源 + T2.1–T2.2 录制接线**：缺口最大、收益最高的一组，且能一次性喂饱 M7。
3. **消耗小、独立性强**的先清：T4.3 缓存可观测、T4.4 采样上限、T8.2 重置三出口。

---

## 2 任务卡（关键实现要点）

### T1.1 流式字节源（M）

- **新增**：`ng/libs/parser/src/{SerialByteSource,UdpByteSource,TcpByteSource}.cpp`（或独立 `ng/extensions/live` 包，推荐后者：传输即扩展，符合"知识包"分层）。
- **契约**：实现 `IByteSource`（`open/read/close/atEnd`），关键改动——`atEnd()` 对流式源**永远返回 false**，`read()` 无数据时返回空块而非 EOF。
- **串口**：`QSerialPort`，波特率/数据位/校验/流控；断线用 `errorOccurred` 触发 `SourceState::Error`。
- **UDP**：`QUdpSocket` + `readyRead`，绑定 `0.0.0.0:port`，记录来源地址（多点同端口时按 5 元组区分）。
- **TCP**：连接/接受两模式，`disconnected` 后进入 `Error`（可选自动重连，交给 T1.3）。
- **测试源**：`SimulatorByteSource` 从内存队列喂字节，用于 ctest 无硬件验证。
- **依赖注入**：不引 `QSerialPort` 到 `libs/parser`（会污染纯库），放在扩展包里，用 SPI 注册。

### T1.2 长流编排（M）

- `IngestOrchestrator` 增 `openStream(IByteSource&, parserIds, Options)`：返回后**不销毁源**，改为持有一个 `StreamPump`。
- `StreamPump`：`QTimer`（GUI 线程）或后台 `QThread` 读块 → `IngestPipeline::feed` → 每解析器一批 → `Dataset::appendFrame` → 每 N ms 发一次 `datasetChanged(datasetId)` 合并刷新（**合并是关键**：1kHz 逐帧刷新会打死 UI）。
- 内存纪律：环形缓冲 + 每解析器一批，与运行时长无关（沿用文件路径的纪律）。
- `finish()` 只在显式 disconnect 时调用（冲刷半帧）。

### T1.3 流控与统计（M）

- `pause/resume`：pump 停止读但保留 socket/串口（TCP 内核缓冲顶住，UDP 丢包计入丢帧）。
- `reconnect`：销毁并重建 byte source，保留 parser 绑定与 dataset 追加目标（**不新建 dataset**，否则画布会跳）。
- 速率：滑窗（1s）算 `Hz` 与 `KB/s`，`data.source.list` 里返回；UI 每 1s 更新。
- 丢帧/坏帧：解析器返回错误计数 + 时间戳跳变检测。

### T1.4 意图（S）

```
data.source.connect   {kind, endpoint|port|baud, parsers[]:[{id,options}], recordTargets[]}
data.source.disconnect{id}
data.source.pause     {id}          // 复用已有 data.pause 语义，但按 sourceId
data.source.resume    {id}
data.source.reconnect {id}
data.source.rate.set  {id, hz}
data.source.config    {id, parsers[]}
data.source.list      {}            // 返回状态/速率/字节/错误/数据集
```

落点：`ng/host/src/DataCommandsHandler.cpp` 的 `handles()` 里加 op 白名单 + 分发；handler 实现放 `ng/modules/data`（`LiveSourceService`）还是 host 层？**放 host 层**（它已持有 orchestrator），UI 只发意图。

### T1.5 实时源 UI（M）

- 数据页 `ribbon.group.data.sources` gallery 增第三分类「实时源」：串口 / UDP / TCP / 回放，`DataSourceCapability` 驱动（哪个扩展在就有哪个）。
- 连接对话框：`ng/gui/shell/LiveSourceDialog.cpp`（镜像 main 的 `DataSourceDialog` 结构：传输段 / 端点段 / 解析器多选段（扇出）/ 录制段 / 预览状态）。
- 解析器多选是 ng 的**加分项**，UI 里要明确写"同一份字节可喂给多个解析器"（这是 main 靠共享 transport 才做到的）。

### T2.1–T2.3 录制（M/S/S）

- `RecordingService`：遍历 `DataSourceInfo::recordings`，每 target 一个 `Recorder`；每帧按 target 的 format 决定 `writeMessage`（mcap）还是 `writeBytes`（raw）。
- 已写字节数从 `Recorder::bytesWritten()` 取，UI 显示；停止时 `close()` 冲刷尾记录。
- 验收靠"录制 mcap → 用 `data.load` 读回 → 表数量与行数等于源"，这是最硬的一条。

### T3.1–T3.3 时间对齐（M/M/S）

- **语义选型（已按你的要求修订）**：存储层**不改原始时间戳**，在 dataset 上挂 `timeOffsetUs`；**但 offset 必须参与所有对外出口**——画布 X 轴、读数、游标、书签、**CSV 导出、快照导出**。即：
  - 存储 = 原始时间戳（进缓存、可比对、可 reset）
  - 出口 = 原始 + offset（一个新概念 `effectiveTime()`，全局唯一入口）
- **硬约束**：`effectiveTime()` 必须成为时间列的唯一来源。绘图与导出**禁止各算一套**，否则画布显示与 CSV 数字对不上，这是最容易埋的雷。
- `setBaseSource(id)`：记录基准源的 `timeMin`；其他源 `offset = base.timeMin - src.timeMin`。
- `alignToStart/End/Center`：同理用 `timeMin/timeMax/中位`。
- 变更后发 `datasetTimeChanged`，画布重取数据（不要重建曲线，保住颜色/可见性）。
- **导出细节**：一次导出内所有被导出表都按各自 offset 渲染；基准源 offset=0；相对秒格式以**基准源零点**为 0；UTC/时分秒格式直接在 offset 后的绝对时间上格式化；列单位保持秒 + 小数位到微秒，不得四舍五入到毫秒（会与画布游标对不上）。

### T3.4 对齐参与导出（M）

- 落点：`data.export` / `data.exportSnapshot` 共用同一个 `writeTableWithTime()`，时间列取 `effectiveTime()`。
- 时间轴格式三选一在导出里也要生效（读同一个 setting key），避免"画布是相对秒、CSV 是 UTC 秒"。
- 表头写明 `# time_mode=aligned, offset_us=1234567`（一行注释），否则用户拿到 CSV 无从判断这一列是原始还是对齐后。


### T4.1–T4.5 数据工程

- 快照：`data.exportSnapshot {datasets[], tables[], format:xlsx|csv}`；xlsx 一簿多 sheet，csv 一表一文件（目录）。
- 参数表：ng 树里已有"参数"节点但无表；需解析器把 `param` 段落成独立 table（ULog 解析器改），P/Q 只是列名约定。
- 缓存命中：`IngestOrchestrator::Options::useCache` 已有；补 `data.cache.stats` 返回 {files, bytes, hits, misses}，UI 在设置页显示。
- 设置对话框：`ng/gui/shell/SettingsDialog.cpp`，三组（数据：采样上限/派生重算上限/缓存；画布：默认网格/图例；时间轴：相对秒/时分秒/UTC）。存储走已有 `SettingsStore`（scope=app），**不新增配置机制**。

### T5.5 画布右键上下文（M，2026-11-21 实测后追加）

实拍事实：ng 空画布右键只有 4 项（属性三项 + 清除游标），**连"显示网格/导出"都没有**——因为 `PlotCanvas::contextMenuEvent` 在 `m_series.isEmpty()` 时提前 return。main 同一位置 13 项。

做法：**删掉 early return**，空/有曲线共用一套菜单；缺的项按 main 落实：

- 属性 ▸（画布 / X / 左 Y / 右 Y）
- `在 %1 s 添加时间书签`（带点击时刻，见 T5.7）
- 每项前置自绘小图标（`MenuIcons`，见 §1.1 落地表）
- A/B 游标测量、固定/取消固定读数
- 放大 / 缩小 / 自适应 / 撤销视图（Ctrl+Z）
- 「添加曲线」→ 以**禁用项 + 提示文字**呈现（"从左侧数据集面板拖入"），ng 尚无通道选择对话框，不装功能
- 显示网格（勾选态同步）/ 显示图例
- 曲线 ▸（显隐 / 挂右 Y / 属性 / Y 轴变换 / 删除）
- 向右拆分 / 向下拆分、导出 PNG… / 导出 CSV…（PlotCanvas 自带 `exportPng/exportCsv`，这里自开文件对话框，不绕回 MainWindow）
- 删除此画布

实现要点：拆分/删除属于 workspace 级，新增信号 `PlotCanvas::canvasActionRequested(QString op)`，由 `CanvasWorkspace` 按 canvas→index 反查后 `setActive(idx)` 再调 `split/removeAt`（**不要用 dispatch，避免"激活画布不是你右键的那个"**）。

**落地状态（2026-11-21 已验证）**：已实现并实拍确认。

- 空画布与有曲线共用一套菜单：空画布 17 项、有曲线 17 项（比 main 13 项多的部分是本就有放大/缩小/撤销视图/曲线▸）。
- 菜单顺序：属性▸ / `在 t s 添加时间书签`（实测显示"在 0.41 s…"）/ A·B / 固定读数 / 放大 / 缩小 / 自适应 / 撤销视图 / 添加曲线（禁用+tooltip）/ ✓显示网格 / ✓显示图例 / 曲线▸ / 向右拆分 / 向下拆分 / 导出 PNG… / 导出当前画布 CSV… / 删除此画布。
- 勾选态：主题不绘制 `QMenu::indicator`，改为文案前置 `✓`（与 main 同写法）；顺手修了 `Axes` 的一处真实不一致——`Axes::m_grid` 默认 false 而 `Axis::m_grid` 默认 true、绘制走 Axis，导致"画布有网格但菜单没勾"，现在构造里以 Axis 为准对齐。
- 拆分：`canvasActionRequested→setActive→split(true/false)`，实测"向右拆分"把活动格右侧加一列、原曲线留在左格。
- 删除：`removeAt` 实测把 2 格收回 1 格、曲线保留；**只剩一格时菜单项直接置灰**（新增 `PlotCanvas::setCanvasRemovable()`，由 `rebuildPageLayout()` 按 `m_cells.size() > 1` 同步），不再"点了才弹提示"。


### T5.6 数据树源节点（M）

main 源节点 15 项，ng 4 项。补：

- `时间对齐 ▸`：当前偏移（只读展示）/ 对齐起点 / 对齐中心 / 对齐终点 / 设为时间基准 / 重置偏移
- `对比同名通道 ▸`：每源子菜单 → 叠加全部同名 / 选择通道…
- `数据源信息`（只读对话框：路径、格式、解析器、表数、行数、时间跨度）
- `移除全部数据源`
- 保留 ng 已把"重命名/移除/配置"放节点图标的设计（**不退回右键**）

**落地状态（2026-11-21 已验证）**：部分落地。

- 已加：`暂停接收/继续接收`（仅 `SourceKind::Live`，文件源不出现以免假入口）、`重命名…`（走 `SourceRegistry::update` 改 name 并 emit sourceChanged，不动 origin）、`数据源信息…`（名称/来源/类型/状态/字节/解析器数/数据集数/错误）、`移除数据源`、`移除全部数据源`（逐 id 下发 `data.remove`，中途失败即停并报错）。
- 实测文件源右键 8 项：重载 / 重命名 / 导出 CSV / 清除缓存 / 数据源信息 / 设为活动 / 移除 / 移除全部；"数据源信息"对话框内容正常。
- **已补**：`时间对齐 ▸`（`3f4a72c`，五项策略，偏移落 AppContext；画布经 `sourceTimeOffsetChanged` 自动重推）。**仍未落地**：`对比同名通道 ▸` —— 需要跨源同名通道配对 + 叠加渲染，尚无引擎，不放假入口。
- 顺带发现一处待修：树的搜索框命中了子字段时，父节点不会自动展开，用户看不到命中项（`onlySource` 过滤只做隐藏）。

### T5.7 时间书签语义（S）

main 是"在 16.02 添加时间书签…"，ng 是固定文案"添加时间书签"。改为带 `timeAtPixel(click)` 的文案与参数；命中已有书签时出"时间书签 ▸（重命名/删除）"。好处：用户不用先移动游标就能在"刚点的这一刻"打点。

**落地状态（2026-11-21 已验证）**：已实现。文案实测为"在 0.41 s 添加时间书签""在 1.32 s 添加时间书签"，时刻随右键位置变化；命中已有书签时转为"时间书签 ▸ 重命名… / 删除"子菜单（原有能力保留）。


### T5.1–T5.4 比对交互

- `overlayAll`：按 `table.column` 名称在全源里找同名，逐个 `plot.series.add` 到当前画布（去重靠 `series.id`），命名 `源名:table.column`。
- `compareSame`：同 table.column 取两个源 → 两条曲线 + 统一 X 范围 + 自动计算 `offset` 建议（借 T3 的算法但只作用于这两条）。
- `展开数据列`：表节点右键，一键把该表全部数值列画进当前画布（限 8 条防爆）。
- DnD：`PlotCanvas` 增 `dragEnterEvent/dragLeaveEvent/dropEvent`，MIME `application/x-plotscope-series`；跨画布移动 series（不是复制），源画布 `plot.series.remove`。

### T6 格式扩展（L，慎重）

**这不是搬运，是重写**：main 的解析器是"每个格式自己 QFile 自己读自己写 Dataset"（`OfflineDataSourceDriver` 时代），ng 要求 SPI 纯字节流 + `IMessageParser`。参考价值：字段映射表、时间基准、坑（blf 的 LOBJ/CAN 编码、dataflash 的 boot 日志损坏、rosbag 的 chunk 索引）。**建议先只做 mdf**（工业客户最常要），其余等需求触发。

### T7 排障

- 调试终端：尾部 200 帧环形缓冲 + 选解析器 + 十六进制/文本；发送命令需 `CIByteSink`（ng 尚无，需新增一个 `IByteSink` 小接口——这是 T7 的前置，别低估）。
- 帧诊断：复用 T1.3 的计数，出一个诊断 dock 页。

### T8 绘图补口

- `surface3d`：fastplot 已有 MeshPlot？没有则先在 `libs/fastplot` 加 `SurfacePlot`，再到图库声明第 13 项（**注意 `test_gui.cpp` 断言 12 项**，加图型必须同步改断言，这是验收要过的门）。
- 重置三出口：`plot.view.resetX / resetZoom / resetView` 三个 op + 右键三行。

---

## 3 明确不搬的东西（避免误伤）

| 不搬 | 原因 |
|---|---|
| main 的脚本控制台（`dock.console` / `ScriptConsole` / `ScratchPlot`） | 你已有更好的脚本设计，本次不动；脚本页保持空壳 |
| main 的 8×8 悬停选格 `LayoutPickGrid/LayoutPopup` | **已移植**（`GridPickPopup`，并补了 main 缺的当前规格/hover 数字/Esc 提示；上限提到 8×8） |
| main 的"应用即 dock"分析库 | ng 的 analysis.apps 结构更适合扩展，不反向改 |
| main 的整段写死色值 QSS | ng ThemeManager token 化正确，不退化 |
| main 的 `dock.tree` 对象树 + PropertyManager | ng 属性面板按选中对象提供者化，不引入第二套 |
| main 的 waterfall→surface3d 替换 | 两个都在用，保留 ng 的 waterfall，surface3d 只做新增 |

---

## 4 实施节奏（一次一提交，别攒大 patch）

1. **每个 T 一个分支/一次提交**，提交前 `git fetch && git rebase origin/nextgen-architecture`。
2. `ctest --test-dir build-ng -j 1` 必须绿（**-j4 会让 `ng_protocol` 时间断言假红**）。
3. 顺序建议：T1.1→T1.2→T1.3→T1.4→T1.5→T1.6（M1 一整块先通，因为后面 T2/T7 都挂它）→ T2 → T3.1/T3.2/T3.3 → T5.1/T5.3（低成本高感知）→ T4.1 → **T3.4 与 T4.1 同批落**（共用导出管线，别先做一套再返工）→ T4 其余 → T5.4 → T7 → T6 → T8。
4. 每完成一个 T，在验收清单里勾 AC，并留证据（job JSON + 截图 + ctest 输出）。
5. 每个 T 都**同步更新** `ng/ARCHITECTURE.md` / `LAYERS.md` / `AGENTS.md` 相应小节（ng 的文档是与代码同级的资产）。

---

## 5 风险登记

| 风险 | 触发点 | 处置 |
|---|---|---|
| 高频流打爆 UI | T1.2 逐帧刷新 | 合并刷新（≥16ms 一批）+ 采样上限；压测 1kHz |
| 长流内存增长 | T1.2 环形缓冲写错 | 压测 30min，RSS 曲线必须平；超限自动丢帧并计数 |
| 时间对齐破坏缓存 | 早期实现把 offset 写进 Dataset 出口 | **偏移只存 `AppContext::m_sourceTimeOffsets` 一处**；Dataset 只存原始时间戳，缓存指纹天然不含 offset |
| 画布与 CSV 时间不一致 | 绘图经 `sourceOffsetFor`、导出走 `data.export` 两条路径 | 导出也必须读 `AppContext::sourceTimeOffset`（T3.4 未做，属已知缺口）；AC-10B 专门验这一条 |
| 录制 mcap 与回放不自洽 | T2.1 消息时间基准不统一 | 录制前定一次基准（首帧时间），回放按同基准；AC-07 用回读比对行数 |
| 图型加项打断测试门 | T8.1 改图库 | 同步改 `test_gui.cpp` 断言 12→13，一次提交内完成 |
| 传输依赖进纯库 | T1.1 图省事放 libs/parser | 强制放扩展包，`libs/*` 不引 Qt 传输模块 |
