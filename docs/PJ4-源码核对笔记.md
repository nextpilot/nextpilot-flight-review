# PJ4 源码核对笔记

用途：修正此前“对照 PJ”清单里凭 README / PJ3 文档得出的错误，作为 PlotScope 补全计划的依据。
核对对象：`facontidavide/PlotJuggler` main（PJ4）、`PlotJuggler/pj-official-plugins` main。

## 1. PJ4 的真实分层

```
pj_base（SDK C ABI）
pj_datastore：列式时序存储 + 派生 DAG + 数据处理器底座
pj_runtime：会话、目录、回放、源对齐、派生服务、插件宿主
pj_scripting：Luau / Python 脚本与内置过滤器
pj_plotting：Qwt + QRhi 绘图画布、曲线编辑、Filter Editor
pj_app：MainWindow 外壳、布局 XML、数据树、时间轴
pj_marketplace：扩展市场
pj_scene2D / pj_scene3D：图像、视频、点云、TF、机器人模型
```

关键点：**派生序列不是 UI 侧数组，而是 datastore 里的普通 topic**。过滤器作为 eager `IncrementalEngine` 节点运行，输出成新 topic，绘图、导出、布局都按普通序列处理。

## 2. 数据源：官方插件真实清单

文件加载器（`pj-official-plugins/data_load_*`）：
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
- 说明：四元数、FFT、CSV/Parquet 导出都是**插件**，不是核心内置。

## 3. 过滤器：12 个内置，Luau 实现

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

## 4. 四元数：官方 Quaternion 插件

`toolbox_quaternion/quaternion_to_rpy.hpp`：
- 输入顺序 `{x,y,z,w}`，但 UI 输入绑定顺序是 `w,x,y,z`，脚本里 `value=w, v1=x, v2=y, v3=z`。
- 先归一化；roll/pitch/yaw 用标准公式；pitch 做 ±π/2 截断。
- 支持角度制、unwrap（连续角展开）。
- 输出是**一个 topic，三列 roll/pitch/yaw**（grouped output `topic:roll,pitch,yaw`），不是三条独立 topic。
- 底层仍走 `pj.data_processors.v1` 的 transform API，脚本由插件生成。

## 5. Filter Editor 的真实行为

`pj_plotting/widget/FilterEditorPanel`：
- 面板式编辑器，不是模态框；选源曲线 → 选过滤器 → 参数表单 → 实时 before/after 预览 → Apply。
- 预览的 before 曲线是虚线 ghost；Apply 后 `PlotWidget::replaceCurve()` **用输出曲线替换原曲线，并继承原颜色**。原始数据仍在数据树里，但图上不再保留 ghost。
- 支持多选源批量 Apply；再次对输出应用过滤器即可形成链。
- 配方写入布局 `<data_processors><processor>`，内嵌 `<source_fallback>` 完整 Luau 源码，换机器没装该过滤器也能恢复。

## 6. 多源对比 / 时间对齐

`pj_runtime/SessionManager`：
- 每个 dataset 有 `sourceDisplayOffset`；显示时间 = raw − source offset − global reference。
- `globalTimeReference` 是所有数据最早 raw 时间，用于全局“Use time offset”切换；与 per-source 偏移分别存储。
- `pj_app/SourceTimelineController`：拖动条即改 offset；支持 Align starts / centers / ends、Reset all、Merge。
- 合并是破坏性的：`DataEngine::mergeDatasets` 把多个源折进 anchor，按调用方给的 raw shift 排序去重；anchor 的 topic id 保留，源被清空。布局保存每个源的 offset 和 timeline 顺序。
- 加载对话框有 prefix 输入，用于同名 topic 区分；曲线显示名是 `topic/field`，数据集层面靠 prefix 区分。

结论：PJ4 的对比不是“同图叠两条同名曲线”这么简单，它先做**时间轴对齐**，再按需 merge。

## 7. 绘图与 UI 细节

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
- 插件体系：市场四类 loader / parser / streamer / toolbox，另有独立 widget 插件族；支持市场安装、ZIP 本地安装、启用/禁用/卸载；SDK 是 C ABI。

## 8. 原清单逐条修正

| 原说法 | 源码事实 |
|---|---|
| ROS1 bag、ROS2 mcap | MCAP 有；ROS1 bag 没有 |
| ArduPilot bin | 没有 |
| Parquet/JSON | 有；JSON 解析器当前只支持 JSON |
| CBOR/BSON/MessagePack | 当前不支持 |
| MQTT/WebSocket/ZeroMQ/UDP/LSL | 有 MQTT/ZMQ/UDP/LSL/ROS2/WebRTC/Foxglove；无通用 WS |
| CAN DBC | 有，随 candump/BLF/MF4 |
| 四元数转 RPY 是内置算子 | 是官方 toolbox 插件，输出单 topic 三列 |
| 低通/Butterworth | PJ4 没有内置 |
| Lua 多输入 | 实际是 Luau；Python 可选 |
| 直方图/GPS/姿态仪 | PJ4 核心和官方插件都没有；属 PJ3 生态，不能算缺口 |
| ULog/JSON 导出 | 官方导出插件只有 CSV/Parquet |
| OpenGL 后端 | 是 QRhi GPU 画布，多后端；OpenGL 只是其一 |
| 每曲线线宽 | PJ4 是 plot 级样式/线宽 |
| 区间测量 | PJ4 没有专门测量工具；有的是 tracker 读数 |
| 单画布导出 PNG | PJ4 核心无此 UI（有 CLI/MCP 截图），不能算 PJ 能力 |

仍然成立的缺口：实时流与录制、插件 SDK/市场、时间轴对齐/合并、过滤器、Custom Topics、XY 图、页签复制/全屏、GPU 画布、状态视图、2D/3D 同步、布局保存变换、数据集搜索/重载/替换。

## 9. 对 PlotScope 计划的修正

1. **派生曲线落到内核数据集**：不要只做 UI 侧 `DerivedSeries` 数组。给 `ps_kernel` 增加“派生通道/主题”写入能力，过滤器输出作为普通通道，才能被多画布、导出、布局统一引用。
2. **过滤器先行、脚本后置**：先实现 12 个 SISO 原生算子（语义按 Luau 源码），四元数单独做 4→3、输出一个 `rpy.roll/pitch/yaw` 分组；Luau 后接。
3. **Filter Editor 交互照 PJ**：面板式、before 虚线 + after 实线、Apply 替换曲线并继承颜色；配方存布局并内嵌实现版本。
4. **多源对比先做时间轴**：per-source offset + Align start/center/end + Reset，再考虑 merge；同名通道靠 prefix/别名区分。
5. **绘图补齐顺序**：曲线 tracker 读数 → 页签复制/全屏 → XY 图 → 数据集搜索/重载/替换。
6. **不要照搬的**：Qwt/Qt-ADS/PJ 的视觉与 XML 结构可以换成我们自己的 Ribbon/JSON，但接口语义（过滤器是普通 topic、apply 替换、offset 分层）要照。

## 10. 读过的关键源码

- `pj_datastore/CLAUDE.md`、`data_processor.hpp`、`merge_result.hpp`、`engine.hpp`
- `pj_runtime/CLAUDE.md`、`SessionManager.h`、`DataProcessorService.h`、`DerivedObjects.h`
- `pj_scripting/CLAUDE.md`、`filter_catalogue.cpp`、`resources/filters/builtin_filters.luau`
- `pj_plotting/CLAUDE.md`、`FilterEditorPanel.h`、`FilteredCurveAdapter.h`、`CurveTracker.h`、`PlotWidget.cpp`、`PlotDocker.cpp`
- `pj_app/CLAUDE.md`、`CurveListPanel.h`、`SourceTimelineController.h`、`LayoutXml.h`
- `pj_marketplace/README.md`
- `pj-official-plugins`: `data_load_*`、`data_stream_*`、`parser_*`、`toolbox_quaternion`、`toolbox_transform_editor`、`toolbox_fft`、`toolbox_data_exporter`


## 11. PJ3→PJ4 迁移欠账（官方自述）

PJ4 是平台级重写：SDK 换 C ABI、插件全部重制、数据层换列式存储。官方插件仓库的 `PORTING_PLAN.md` 就是逐插件对比 PJ3 的欠账表：

- 统计：12 个移植插件，**46 项逐插件缺口 + 11 项横切缺口**；其中 CRITICAL 3、HIGH 16、MEDIUM 17、LOW 10。
- 3 个 CRITICAL 已修：MQTT TLS 证书、Protobuf 内嵌时间戳、PJ Bridge 的 u16/u32 线格式。
- 大多数 HIGH 已补：CSV 日期格式帮助、Parquet 时区/排序、Protobuf 枚举/字符串字段、JSON 的 CBOR/BSON/MessagePack、Foxglove 断线重连、PJ Bridge 心跳等。
- **仍未收尾的**：ULog 的参数/Info/日志三页对话框——数据已写成 `_parameters` / `_info` / `_log` topic，但宿主没有状态面板 UI；部分流插件的暂停/恢复、主题列表刷新、错误弹窗仍靠宿主补。

PJ3 有、PJ4 官方插件里直接没有的：
- **StatePublisherCSV / StatePublisherZMQ**：向机器人回写状态/指令。PJ4 明确“不能向机器人发布”。
- **ZCM** 插件。
- **通用 WebSocket 流**：PJ4 只剩 Foxglove Bridge 和 PJ Bridge 两种协议专用的 WS。
- **Lua Editor**：被 `toolbox_transform_editor` 的自定义函数/脚本编辑器替代，形态变了，不是原样保留。
- **ROS1 传输链路**：PJ4 的 `parser_ros` 仍认 `ros1msg` schema，但没有 ROS1 流/连接器；PJ3 的 ParserROS + 通用 WS 组合没了。
- **VideoViewer**：被核心 2D viewer（图像/视频/深度）取代，能力是增强不是丢失。

所以：
- **“PJ4 没来得及完善 PJ3 功能”在插件生态、发布链路、旧格式兼容上是真的**，官方迁移表就是证据。
- **但不是整体倒退**：核心新增了数据处理器、源时间轴、页签复制/全屏、2D/3D、GPU 画布、市场、录制、MCP，这些 PJ3 没有。
- 对我们最有用的结论：**PJ3 当“成熟分析交互”的参考，PJ4 当“架构与数据模型”的参考**。像直方图、GPS、姿态仪这些 PJ3 第三方生态功能，不能拿 PJ4 当缺口标准；而发布、ZCM、通用 WS、ULog 参数页这类，确实是 PJ4 的欠账，我们可以按需做，但不必为了“对标 PJ4”而做。
