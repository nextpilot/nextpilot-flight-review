# PlotScope PJ4 功能对齐与源码核对 v1.1

> 参考基线与路线判断，不是当前开发指南。当前架构见 [`../develop/architecture.md`](../develop/architecture.md)，硬边界见 [`../../AGENTS.md`](../../AGENTS.md)。

> 合并：原《功能对齐矩阵》+《PJ4 源码核对笔记》。前 9 章是能力矩阵与路线，第 10 章是源码事实与对矩阵的修正依据。

口径：
- PJ3 = `facontidavide/PlotJuggler` 3.9.3 核心 + 同仓库扩展 + `PlotJuggler` 组织下的官方扩展仓库。
- PJ4 = 当前 main 核心 + `pj-official-extensions` 官方扩展。
- “对齐”不是扩展数量相同，而是同一工作流可完成、同一输入结果一致、同一布局可恢复。
- 目标取 PJ3 与 PJ4 的**并集**：PJ4 丢掉的 PJ3 能力也要补，不能拿“PJ4 还没做完”当理由。

状态：✅ 已有；⚠️ 部分/需验证；❌ 没有。

> 2026-11-13 复核：下表已按当前代码回填。实时源（MAVLink UDP / OEM7 TCP·串口 / Sim internal）、MCAP 加载与录制、CSV/MCAP/PSRAW1 统一录制、外部扩展 ABI 已落地；仍缺 MQTT/ZMQ/ROS2、MDF4、Parquet、视频同步等。
>
> 2026-11-14 追加：P2 设置三项与 CSV 源别名已落地；P3 增加 1–4 输入绑定、`init/reset/step`、错误行定位、源码随 `.psl` 保存；OEM7 RAW 写 `schema_blob`。`ctest` 15/15。

## 1. 文件加载

| 能力 | PJ3 | PJ4 | PlotScope | 阶段 |
|---|---|---|---|---|
| CSV | ✅ | ✅ | ✅ | — |
| ULog | ✅ | ✅ | ✅ | — |
| MCAP | ✅ | ✅ | ✅ `data_load_mcap`：JSON channel、zstd/lz4 chunk；录制由 `StreamRecorder` 写最小 MCAP | B |
| Parquet | ✅ | ✅ | ❌ | B |
| MF4/MDF | ❌ | ✅ | ❌ | B |
| BLF / candump | ⚠️ CAN 扩展 | ✅ | ✅ ASC / ✅ BLF，DBC 解码待确认 | B |
| LeRobot | ❌ | ✅ | ❌ | C |
| MP4 | ⚠️ 仅视频查看 | ✅ 作为数据源 | ❌ | C |
| 3D（PLY/PCD） | ❌ | ✅ | ❌ | C |
| ROS1 bag | ⚠️ README 声称，3.9.3 代码仅见 ROS2 | ❌ | ❌ | C |
| ArduPilot .bin | ❌ | ❌ | ✅ DataFlash 二进制；597 通道 / 1304 参数 / 40 MSG 验收 | A |

## 2. 实时流与录制

| 能力 | PJ3 | PJ4 | PlotScope | 阶段 |
|---|---|---|---|---|
| MQTT | ✅ | ✅ | ❌ | C |
| ZeroMQ | ✅ | ✅ | ❌ | C |
| UDP | ✅ | ✅ | ✅ MAVLink UDP（绑定/端口/sysid）；OEM7 TCP/串口另有实时源 | — |
| 通用 WebSocket | ✅ | ❌（只剩 Foxglove/PJ Bridge） | ❌ | C |
| LSL | ✅ | ✅ | ❌ | C |
| ROS2 | ✅ | ✅ | ❌ | C |
| Foxglove Bridge | ❌ | ✅ | ❌ | C |
| PJ Bridge | ❌ | ✅ | ❌ | C |
| WebRTC | ❌ | ✅ | ❌ | C |
| ZCM | ✅ | ❌ | ❌ | 不做 |
| 流录制（MCAP） | ❌ | ✅ | ✅ CSV/MCAP/PSRAW1 三格式；MCAP 为最小 writer | — |

## 3. 消息解析

| 能力 | PJ3 | PJ4 | PlotScope | 阶段 |
|---|---|---|---|---|
| JSON / CBOR / BSON / MessagePack | ✅ | ✅（已补） | ❌ | B |
| Protobuf | ✅ | ✅ | ❌ | B |
| ROS1/ROS2 msg | ✅ | ✅（ros1msg/ros2msg/idl） | ❌ | C |
| Arrow | ❌ | ✅ | ❌ | C |
| DataTamer | ✅ | ✅ | ❌ | C |
| Influx line | ✅ | ✅ | ❌ | C |

## 4. 绘图前数据变换

| 能力 | PJ3 | PJ4 | PlotScope | 阶段 |
|---|---|---|---|---|
| Transform Editor | ✅ | ✅ | ✅ 单通道滤波 + 四元数对话框、before/after 预览；链式编排 UI 未做 | A |
| 12 个内置过滤器 | ✅ | ✅ | ✅ 内核 12 算子批处理 + 参数 UI；12 算子 golden 已覆盖 | A |
| 四元数 → RPY | ✅ 扩展 | ✅ 扩展（单 topic 三列） | ✅ 4→3，三条扁平通道 `rpy.roll/.pitch/.yaw`；尚无单 topic 三列 | A |
| 自定义函数编辑器（多输入、预览、应用、存布局） | ✅ Lua | ✅ Luau | ⚠️ Lua 5.4 面板可运行、输出到画布；缺 1–4 输入绑定、源码入 `.psl`、错误行号 | B（A 后第一项） |
| 过滤器输出作为普通通道 | ⚠️ 派生序列 | ✅ datastore topic | ✅ 写回同一 `PsDataset`，进「派生通道」组 | A |
| 配方进布局、可移植 | ✅ XML | ✅ 内嵌源码 | ✅ `.psl` `transforms[]`，导入按序重放 | A |
| FFT | ✅ 扩展 | ✅ 扩展 | ✅ 画布 FFT 图型（512 点 DFT） | — |

自定义函数编辑器不照搬 PJ 的界面，但必须保留同等能力：
- 1–4 个输入通道 + 若干标量参数；输出单值或分组多列（如 roll/pitch/yaw）。
- 代码编辑区、输入绑定、参数表单、before/after 预览、Apply 替换。
- 内置片段：`quat_to_rpy`、derivative、integral、clamp、rad2deg、moving_average。
- 脚本 API：`init(params)` / `reset()` / `step(t, value, v1..v3)` 返回 number 或 table。
- 沙箱：禁 `io/os/package`，步数与内存上限；错误带行号，不崩 UI。
- 源码随 `.psl` 保存，换机器可恢复；支持导入 PJ3 Lua 脚本改写使用。

## 5. 绘图与交互

| 能力 | PJ3 | PJ4 | PlotScope | 阶段 |
|---|---|---|---|---|
| 多页签 / 切分 | ✅ | ✅ | ✅ | — |
| 页签“+”新增 | ✅ | ✅ | ✅ | — |
| 页签复制 | ❌ | ✅ | ❌ | A |
| 单画布全屏 | ✅ | ✅ | ❌ | A |
| 曲线拖到画布 | ✅ | ✅ | ✅ | — |
| 时间游标 + 各曲线值 | ✅ | ✅ | ✅ 十字游标逐曲线读数；A/B 游标含 Δt/Δy/斜率 | — |
| 数据树显示游标处数值 | ✅ | ✅ | ❌ | A |
| XY 图 | ✅ | ✅ | ✅ 首通道为 X、其余为 Y | — |
| plot 级样式/线宽 | ⚠️ 曲线级为主 | ✅ plot 级 | ⚠️ 曲线级 | B |
| GPU 画布 | ⚠️ OpenGL | ✅ QRhi | ❌ | C |
| 百万点/千序列 | ✅（宣传） | ✅（基准） | ❌ 未验证 | C |

## 6. 数据树与数据集管理

| 能力 | PJ3 | PJ4 | PlotScope | 阶段 |
|---|---|---|---|---|
| 搜索 | ✅ | ✅ | ✅ | — |
| 曲线/2D/3D 类型开关 | ⚠️ | ✅ | ❌ | B |
| 数据集 Info | ✅ | ✅ | ✅ 源右键「数据源信息」 | — |
| Reload / Replace | ✅ | ✅ | ⚠️ 右键「重新加载」会新开一份源，未做原位替换 | A |
| Remove / Remove all | ✅ | ✅ | ✅ 单源移除 + 右键「移除全部数据源」 | — |
| Custom Topics 分区 | ❌ | ✅ | ❌ | C |
| 同名通道前缀区分 | ⚠️ | ✅ | ⚠️ 别名图例 | A |

## 7. 布局、导出、扩展

| 能力 | PJ3 | PJ4 | PlotScope | 阶段 |
|---|---|---|---|---|
| 布局保存/恢复 | ✅ | ✅ | ✅ | — |
| 保存多源 offset/顺序 | ❌ | ✅ | ✅ `.psl` 保存每源 offset/别名 | — |
| 保存变换配方 | ✅ | ✅ | ✅ `.psl` `transforms[]` | — |
| CSV 数据导出 | ❌ 核心 | ✅ 扩展 | ✅ 当前源快照 + 画布可见时间窗长表 | — |
| Parquet 导出 | ❌ | ✅ 扩展 | ❌ | B |
| PNG 导出 | ✅ 全部页签 | ⚠️ CLI/MCP | ✅ 单画布 | — |
| 状态视图（字符串/枚举/布尔） | ❌ | ✅ | ❌ | B |
| 2D 图像/视频/深度 | ✅ 视频扩展 | ✅ 核心 | ❌ | C |
| 3D / TF / 点云 | ❌ | ✅ | ❌ | C |
| 扩展 SDK | ✅ C++ | ✅ C ABI | ❌ | C |
| 扩展市场 | ❌ | ✅ | ❌ | C |
| MCP / AI 控制 | ❌ | ✅ | ❌ | C |
| PJ3 生态 GPS/罗盘/直方图/仪表 | ⚠️ 第三方 | ⚠️ 仅 widget 仪表 | ❌ | 按场景 |

## 8. 路线

### A. 分析闭环（当前优先，必须清零）
1. 内核派生通道 + 12 过滤器 + 四元数 4→3 + Filter Editor。
2. 多源时间偏移、对齐、前缀、同名通道对比。
3. 游标各曲线值 + 数据树游标列。
4. 绘图页签“+”、复制、全屏、曲线拖入。
5. 数据集 Info/Reload/Replace/Remove all。
6. CSV 导出、布局保存变换配方与 offset。

### B. 格式与基础视图
MCAP、Parquet、MF4、BLF/candump 的 DBC 解码、JSON/Protobuf 解析、XY 图、状态视图、plot 级样式、Parquet 导出。

### C. 平台能力
MQTT/ZMQ/UDP/LSL/ROS2/Foxglove/PJ Bridge/WebRTC、MCAP 录制、Luau/Python、扩展 SDK 与市场、2D/3D、GPU/列式存储、MCP。

### D. 超越 PJ 的部分
- 飞控语义：PX4 ULog 与 ArduPilot DataFlash 的参数/消息/飞行模式/估计器新息结构化视图。
- 两次仿真/试飞对比：同名通道配对 + 时间对齐 + 参数差异 + 指标差异，一步出结论。
- 批量与自动化：无界面 CLI、批量截图/报告、回归对比。
- 本地化与离线：中文界面、离线安装、轻量部署。

## 9. 验收口径

每个能力项必须满足：
1. 同一输入文件能打开，数值与 PJ 一致（golden case）。
2. 同一工作流能在 UI 完成，不缺步骤。
3. 布局保存后重启可恢复。
4. 回归用例进 `ctest` 或脚本化 GUI 测试。

“对齐”完成的标志不是 README 打勾，而是矩阵每行有可复现的验收用例。

---

## 10. PJ4 源码核对（事实基线）

**用途**：修正此前“对照 PJ”清单里凭 README / PJ3 文档得出的错误，作为 PlotScope 补全计划的依据。
**核对对象**：`facontidavide/PlotJuggler` main（PJ4）、`PlotJuggler/pj-official-extensions` main。

### 10.1 PJ4 的真实分层

```
pj_base（SDK C ABI）
pj_datastore：列式时序存储 + 派生 DAG + 数据处理器底座
pj_runtime：会话、目录、回放、源对齐、派生服务、扩展宿主
pj_scripting：Luau / Python 脚本与内置过滤器
pj_plotting：Qwt + QRhi 绘图画布、曲线编辑、Filter Editor
pj_app：MainWindow 外壳、布局 XML、数据树、时间轴
pj_marketplace：扩展市场
pj_scene2D / pj_scene3D：图像、视频、点云、TF、机器人模型
```

关键点：**派生序列不是 UI 侧数组，而是 datastore 里的普通 topic**。过滤器作为 eager `IncrementalEngine` 节点运行，输出成新 topic，绘图、导出、布局都按普通序列处理。

### 10.2 数据源：官方扩展真实清单

文件加载器（`pj-official-extensions/data_load_*`）：
- CSV、ULog、MCAP、Parquet、MF4/MDF、BLF、candump、LeRobot、MP4、3D 模型。
- **没有 ROS1 bag**，**没有 ArduPilot .bin**。
- CAN DBC 解码在 candump / BLF / MF4 三个 loader 里共用 `pj_can_dbc`。

实时流（`data_stream_*`）：
- ROS2、MQTT、ZeroMQ、UDP、LSL、WebRTC、Foxglove Bridge（WebSocket）、PJ Bridge、Dummy。
- 没有通用 WebSocket 流；没有 ROS1 流。

消息解析（`parser_*`）：
- ROS1/ROS2、Protobuf、JSON、Arrow、DataTamer、Influx。
- **CBOR / BSON / MessagePack 已不在当前 JSON 解析器里**（`parser_json/README.md` 明确写为限制），README 的旧表述不能当现状。

工具箱（`toolbox_*`）：
- Transform Editor、Quaternion、FFT、Data Exporter、Anomaly Detector、Mosaico、Assistant Agent。
- 说明：四元数、FFT、CSV/Parquet 导出都是**扩展**，不是核心内置。

### 10.3 过滤器：12 个内置，Luau 实现

`resources/filters/builtin_filters.luau` 是唯一内置目录，按 Luau 类自描述（`id/name/parameters/output`）：
`none`、`absolute`、`scale`、`derivative`、`integral`、`moving_average`、`moving_rms`、`moving_variance`、`outlier_removal`、`samples_counter`、`binary_filter`、`time_since_previous`。

语义细节（源码确认）：
- `derivative`：首样本抑制；`dt=0` 输出 0；可选固定 Δt。
- `integral`：梯形积分；首样本抑制。
- `moving_average`：不足窗口时用当前值补齐、除以完整窗口；可选把时间戳居中到窗口跨度。
- `moving_variance`：同样 pad-with-current，可输出标准差。
- `outlier_removal`：4 点环形判定，单点尖峰抑制；正常时输出前一个样本（一采样延迟）。
- `scale`：`k*v+b`，同时平移输出时间戳。
- `binary_filter`：输出 0/1 int64；`samples_counter` 输出窗口内样本数 int64。
- `time_since_previous`：相邻样本 Δt，首样本抑制。

**没有低通/Butterworth 内置**；PJ4 要靠自定义脚本做。也没有通用重采样。

### 10.4 四元数：官方 Quaternion 扩展

`toolbox_quaternion/quaternion_to_rpy.hpp`：
- 输入顺序 `{x,y,z,w}`，但 UI 输入绑定顺序是 `w,x,y,z`，脚本里 `value=w, v1=x, v2=y, v3=z`。
- 先归一化；roll/pitch/yaw 用标准公式；pitch 做 ±π/2 截断。
- 支持角度制、unwrap（连续角展开）。
- 输出是**一个 topic，三列 roll/pitch/yaw**（grouped output `topic:roll,pitch,yaw`），不是三条独立 topic。
- 底层仍走 `pj.data_processors.v1` 的 transform API，脚本由扩展生成。

### 10.5 Filter Editor 的真实行为

`pj_plotting/widget/FilterEditorPanel`：
- 面板式编辑器，不是模态框；选源曲线 → 选过滤器 → 参数表单 → 实时 before/after 预览 → Apply。
- 预览的 before 曲线是虚线 ghost；Apply 后 `PlotWidget::replaceCurve()` **用输出曲线替换原曲线，并继承原颜色**。原始数据仍在数据树里，但图上不再保留 ghost。
- 支持多选源批量 Apply；再次对输出应用过滤器即可形成链。
- 配方写入布局 `<data_processors><processor>`，内嵌 `<source_fallback>` 完整 Luau 源码，换机器没装该过滤器也能恢复。

### 10.6 多源对比 / 时间对齐

`pj_runtime/SessionManager`：
- 每个 dataset 有 `sourceDisplayOffset`；显示时间 = raw − source offset − global reference。
- `globalTimeReference` 是所有数据最早 raw 时间，用于全局“Use time offset”切换；与 per-source 偏移分别存储。
- `pj_app/SourceTimelineController`：拖动条即改 offset；支持 Align starts / centers / ends、Reset all、Merge。
- 合并是破坏性的：`DataEngine::mergeDatasets` 把多个源折进 anchor，按调用方给的 raw shift 排序去重；anchor 的 topic id 保留，源被清空。布局保存每个源的 offset 和 timeline 顺序。
- 加载对话框有 prefix 输入，用于同名 topic 区分；曲线显示名是 `topic/field`，数据集层面靠 prefix 区分。

结论：PJ4 的对比不是“同图叠两条同名曲线”这么简单，它先做**时间轴对齐**，再按需 merge。

### 10.7 绘图与 UI 细节

- Qwt 画布 + 可选 QRhi GPU 画布（`PlotRhiCanvas`），不是单纯 QPainter；`use_opengl` 是后端开关。
- 曲线样式/线宽是 **plot 级**属性；颜色、可见性、marker 是 curve 级。此前清单按“每曲线线宽”对比是错的。
- 曲线适配器不做预抽稀，抽稀在绘制阶段完成（`PlotCurve::drawCurve` / Qwt weeding / GPU reducer）。
- `CurveTracker`：鼠标处竖线 + 浮动值框，显示 time 和该 x 下各曲线值；这是“时间游标读数”的真实实现。
- `TabbedPlotWidget`：页签、切分、复制页签（带曲线和样式）、全屏 dock（`PlotDocker::fullscreenDock`）。
- XY 图：`PointSeriesXY`，右键/拖两个序列创建，存布局时 X/Y 各自存 topic+field。
- 数据树 `CurveListPanel`：搜索框、三类别开关（曲线/2D/3D）、Custom Topics 独立分区与过滤、数据集右键 Info/Merge/Reload/Replace/Remove、Remove all datasets。
- 布局 `.pj4.xml`：曲线用稳定 `topic+field` 路径，X 范围存绝对纳秒，过滤器配方内嵌源码，源路径/offset/timeline 顺序/流配置都保存。
- 状态视图 `StateTransitions`：字符串/整数/布尔序列显示为状态色带，不是曲线。
- 2D viewer：图像/视频/深度图随游标同步；3D：TF、点云、栅格、URDF、marker。
- 扩展体系：市场四类 loader / parser / streamer / toolbox，另有独立 widget 扩展族；支持市场安装、ZIP 本地安装、启用/禁用/卸载；SDK 是 C ABI。

### 10.8 原清单逐条修正

| 原说法 | 源码事实 |
|---|---|
| ROS1 bag、ROS2 mcap | MCAP 有；ROS1 bag 没有 |
| ArduPilot bin | 没有 |
| Parquet/JSON | 有；JSON 解析器当前只支持 JSON |
| CBOR/BSON/MessagePack | 当前不支持 |
| MQTT/WebSocket/ZeroMQ/UDP/LSL | 有 MQTT/ZMQ/UDP/LSL/ROS2/WebRTC/Foxglove；无通用 WS |
| CAN DBC | 有，随 candump/BLF/MF4 |
| 四元数转 RPY 是内置算子 | 是官方 toolbox 扩展，输出单 topic 三列 |
| 低通/Butterworth | PJ4 没有内置 |
| Lua 多输入 | 实际是 Luau；Python 可选 |
| 直方图/GPS/姿态仪 | PJ4 核心和官方扩展都没有；属 PJ3 生态，不能算缺口 |
| ULog/JSON 导出 | 官方导出扩展只有 CSV/Parquet |
| OpenGL 后端 | 是 QRhi GPU 画布，多后端；OpenGL 只是其一 |
| 每曲线线宽 | PJ4 是 plot 级样式/线宽 |
| 区间测量 | PJ4 没有专门测量工具；有的是 tracker 读数 |
| 单画布导出 PNG | PJ4 核心无此 UI（有 CLI/MCP 截图），不能算 PJ 能力 |

仍然成立的缺口：实时流与录制、扩展 SDK/市场、时间轴对齐/合并、过滤器、Custom Topics、XY 图、页签复制/全屏、GPU 画布、状态视图、2D/3D 同步、布局保存变换、数据集搜索/重载/替换。

### 10.9 对 PlotScope 计划的修正

1. **派生曲线落到内核数据集**：不要只做 UI 侧 `DerivedSeries` 数组。给 `ps_kernel` 增加“派生通道/主题”写入能力，过滤器输出作为普通通道，才能被多画布、导出、布局统一引用。
2. **过滤器先行、脚本后置**：先实现 12 个 SISO 原生算子（语义按 Luau 源码），四元数单独做 4→3、输出一个 `rpy.roll/pitch/yaw` 分组；Luau 后接。
3. **Filter Editor 交互照 PJ**：面板式、before 虚线 + after 实线、Apply 替换曲线并继承颜色；配方存布局并内嵌实现版本。
4. **多源对比先做时间轴**：per-source offset + Align start/center/end + Reset，再考虑 merge；同名通道靠 prefix/别名区分。
5. **绘图补齐顺序**：曲线 tracker 读数 → 页签复制/全屏 → XY 图 → 数据集搜索/重载/替换。
6. **不要照搬的**：Qwt/Qt-ADS/PJ 的视觉与 XML 结构可以换成我们自己的 Ribbon/JSON，但接口语义（过滤器是普通 topic、apply 替换、offset 分层）要照。

### 10.10 读过的关键源码

- `pj_datastore/CLAUDE.md`、`data_processor.hpp`、`merge_result.hpp`、`engine.hpp`
- `pj_runtime/CLAUDE.md`、`SessionManager.h`、`DataProcessorService.h`、`DerivedObjects.h`
- `pj_scripting/CLAUDE.md`、`filter_catalogue.cpp`、`resources/filters/builtin_filters.luau`
- `pj_plotting/CLAUDE.md`、`FilterEditorPanel.h`、`FilteredCurveAdapter.h`、`CurveTracker.h`、`PlotWidget.cpp`、`PlotDocker.cpp`
- `pj_app/CLAUDE.md`、`CurveListPanel.h`、`SourceTimelineController.h`、`LayoutXml.h`
- `pj_marketplace/README.md`
- `pj-official-extensions`: `data_load_*`、`data_stream_*`、`parser_*`、`toolbox_quaternion`、`toolbox_transform_editor`、`toolbox_fft`、`toolbox_data_exporter`


### 10.11 PJ3→PJ4 迁移欠账（官方自述）

PJ4 是平台级重写：SDK 换 C ABI、扩展全部重制、数据层换列式存储。官方扩展仓库的 `PORTING_PLAN.md` 就是逐扩展对比 PJ3 的欠账表：

- 统计：12 个移植扩展，**46 项逐扩展缺口 + 11 项横切缺口**；其中 CRITICAL 3、HIGH 16、MEDIUM 17、LOW 10。
- 3 个 CRITICAL 已修：MQTT TLS 证书、Protobuf 内嵌时间戳、PJ Bridge 的 u16/u32 线格式。
- 大多数 HIGH 已补：CSV 日期格式帮助、Parquet 时区/排序、Protobuf 枚举/字符串字段、JSON 的 CBOR/BSON/MessagePack、Foxglove 断线重连、PJ Bridge 心跳等。
- **仍未收尾的**：ULog 的参数/Info/日志三页对话框——数据已写成 `_parameters` / `_info` / `_log` topic，但宿主没有状态面板 UI；部分流扩展的暂停/恢复、主题列表刷新、错误弹窗仍靠宿主补。

PJ3 有、PJ4 官方扩展里直接没有的：
- **StatePublisherCSV / StatePublisherZMQ**：向机器人回写状态/指令。PJ4 明确“不能向机器人发布”。
- **ZCM** 扩展。
- **通用 WebSocket 流**：PJ4 只剩 Foxglove Bridge 和 PJ Bridge 两种协议专用的 WS。
- **Lua Editor**：被 `toolbox_transform_editor` 的自定义函数/脚本编辑器替代，形态变了，不是原样保留。
- **ROS1 传输链路**：PJ4 的 `parser_ros` 仍认 `ros1msg` schema，但没有 ROS1 流/连接器；PJ3 的 ParserROS + 通用 WS 组合没了。
- **VideoViewer**：被核心 2D viewer（图像/视频/深度）取代，能力是增强不是丢失。

所以：
- **“PJ4 没来得及完善 PJ3 功能”在扩展生态、发布链路、旧格式兼容上是真的**，官方迁移表就是证据。
- **但不是整体倒退**：核心新增了数据处理器、源时间轴、页签复制/全屏、2D/3D、GPU 画布、市场、录制、MCP，这些 PJ3 没有。
- 对我们最有用的结论：**PJ3 当“成熟分析交互”的参考，PJ4 当“架构与数据模型”的参考**。像直方图、GPS、姿态仪这些 PJ3 第三方生态功能，不能拿 PJ4 当缺口标准；而发布、ZCM、通用 WS、ULog 参数页这类，确实是 PJ4 的欠账，我们可以按需做，但不必为了“对标 PJ4”而做。
