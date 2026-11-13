# PlotScope 对标 PJ4 补全计划 v1.1

基线：`488fea9`（多数据源 + 源别名图例 + 布局持久化）
修订依据：`docs/PJ4-源码核对笔记.md`（已读 PJ4 源码与官方插件仓库）。v1.0 中“派生数组归 UI、四元数算内置、低通算 PJ 能力”等判断已作废。
原则：
- UI 保持我们自己的深色工作台，不照搬 PJ 的 Qwt 观感。
- 内部机制按 PJ4 的分层来：数据存储 → 处理器 → 派生曲线 → 绘图适配。
- 变换非破坏性：原始曲线保留为 ghost，滤波/派生结果单独成曲线。
- 先内置算子，后脚本。PJ4 的内置过滤器本身是 Luau 类；我们先 C++ 实现同语义接口，后续可替换为 Luau。
- 每阶段必须可运行、可截图、有测试，不做半成品入口。

---

## 1. 现状

已完成：
- ULog / CSV / ASC / BLF 加载，单源字段/参数/消息。
- 多数据源并行：每源独立 `PsDataset`，切源不释放，删源只清该源曲线。
- 曲线图例带源别名，源可重命名。
- 页签/切分/多画布、拖拽/双击加曲线、缩放/平移、X 联动。
- 曲线右键：显隐/颜色/线宽/删除；画布右键：添加曲线/自适应/缩放/增删画布。
- `.psl` 保存/导入页面树、曲线、时间窗、Y 范围、停靠、窗口、全部源路径；启动恢复。
- 设置：启动恢复布局、默认网格/图例。

主要缺口：
- 没有任何数据变换/派生曲线。
- 多源只有“同时打开”，没有时间对齐、同名通道配对对比。
- 时间游标/读数、CSV 导出、脚本、实时源、插件机制缺失。

---

## 2. 目标架构

```
ps_kernel (Rust)
  raw dataset / channels / LOD
        │ ps-abi
SourceData (host, MainWindow)
  PsDataset* + alias + displayOffsetSec + derived registry
        │
transform::Processor (C++ v1)
  id / params / numInputs / numOutputs / reset / calculateNextPoint / applyBatch
        │
DerivedSeries
  recipe + owned samples + dirty/lazy recompute
        │
CanvasWidget::Series
  raw | derived，ghost 样式，图例显示 “源 · 通道 → 变换”
```

### 2.1 处理器接口（对齐 PJ `pj_datastore/data_processor.hpp`）

```cpp
struct Sample { double t; double v; };

class Processor {
public:
    virtual QString id() const = 0;          // 目录键
    virtual QString label() const = 0;       // 图例后缀，如 "MA(100)"
    virtual int numInputs() const { return 1; }
    virtual int numOutputs() const { return 1; }
    virtual void reset() = 0;
    virtual bool next(const Sample &in, Sample &out) = 0;   // false = 抑制该行
    virtual void applyBatch(const QVector<Sample> &in, QVector<Sample> &out);
    virtual bool failed() const { return false; }
};
```

要点照 PJ：
- 顺序契约：按时间升序逐样本调用，状态跨样本保留，只有 `reset()` 清状态。
- 派生结果按 PJ 的方式物化成数据集里的普通通道/主题，而不是只活在 UI 数组里；多画布、导出、布局都按普通通道引用。
- 预览阶段用“新建处理器 + 全量重算”，不复用累加器；Apply 后输出替换原曲线并继承颜色，原始数据仍在数据树。
- 多输入（四元数）先做 4→3，时间戳必须同拍；不同拍按最近邻 join，并记录告警。

### 2.2 派生曲线

```json
{
  "id": "ma_roll",
  "source": "/path/flight1.ulg",
  "inputs": ["attitude.roll"],
  "filter": { "id": "moving_average", "params": { "window": 100 } }
}
```

- 给 `ps_kernel` 增加派生通道写入：同一 `PsDataset` 下新增普通通道（名字如 `attitude.roll.abs`），拥有独立 t/v 数组。
- 画布 `Series` 增加 `derivedChannel` 引用；图例显示 `flight1 · attitude.roll · Absolute`。
- 预览时 before 曲线画虚线 ghost；Apply 后用输出替换来源曲线并继承颜色（对齐 `PlotWidget::replaceCurve`）。原始通道保留在树里，可重新拖回。
- 派生通道参与右键菜单：编辑变换、移除变换、导出数据。
- 配方写入 `.psl` v2 的 `derived` 根节点（id、源路径、输入通道、processor_id、params、输出名）；导入时先重建通道再绑曲线。
- 链式：对派生通道再次应用过滤器即可，不做单独的链编辑器。

### 2.3 内置算子（首批，ID 与 PJ4 对齐）

| id | 说明 | 参数（对齐 PJ） |
|---|---|---|
| `none` | 直通 | — |
| `absolute` | 绝对值 | — |
| `scale` | 比例/偏移/时间平移 | `value_scale`, `value_offset`, `time_offset_sec` |
| `derivative` | dv/dt | `use_custom_dt`, `custom_dt` |
| `integral` | 梯形积分 | `use_custom_dt`, `custom_dt` |
| `moving_average` | 滑动平均 | `window`, `compensate_time_offset` |
| `moving_rms` | 滑动 RMS | `window` |
| `moving_variance` | 滑动方差/标准差 | `window`, `std_dev` |
| `outlier_removal` | 单点尖峰剔除 | `outlier_factor` |
| `samples_counter` | 窗口内样本数 | `samples_ms` |
| `binary_filter` | 阈值二值化 | `binary_op`, `binary_a`, `binary_b`, `binary_tol` |
| `time_since_previous` | 相邻样本 Δt | — |

四元数不在 PJ4 内置目录里，而是官方 Quaternion 插件：4 输入（w,x,y,z）→ 一个分组 topic `rpy:roll,pitch,yaw`。我们按同样语义单独实现，不做三条独立通道；支持 degree、unwrap、pitch ±π/2 截断。

语义细节按 PJ `resources/filters/builtin_filters.luau`：
- moving_average 不足窗口时用当前值补齐，除以完整窗口；可选按缓冲跨度居中时间戳。
- derivative / integral 首个样本抑制，integral 用梯形法。
- outlier_removal 只丢单点尖峰，不连续。
- samples_counter 输出 int64。
- scale 的时间平移作用在输出时间戳，不改输入。

### 2.4 多源对比

- 每个 `SourceData` 增加 `displayOffsetSec`：显示时间 = raw − offset；再预留全局 reference（PJ 的 `sourceDisplayOffset` 与 `globalTimeReference` 分层）。
- 源行右键/底部源时间轴：拖动偏移、Align starts / centers / ends、Reset all。
- 加载时提供 prefix/别名，同名 topic 用前缀区分（PJ 的 `recordLoadedSource(prefix)` 语义）。
- “对比同名通道”：选中两个源，按通道名配对，一次把两源同一通道加到当前画布，颜色分源。
- 布局保存每源 offset、别名、时间轴顺序。
- 真正的合并不急：PJ 的 `mergeDatasets` 是破坏性的 anchor 合并，先把显示对齐做对，再决定是否实现。

---

## 3. 阶段与验收

### P0 变换内核 + 派生曲线（优先）
任务：
1. 新建 `src/app/transform/`：`Processor.h`、`Catalogue.{h,cpp}`、`SisoFilters.cpp`、`QuatToRpy.cpp`、`DerivedSeries.{h,cpp}`。
2. 实现 12 个 SISO + 四元数 4→3，参数用 JSON 描述，目录可查询。
3. 字段树右键“添加变换…”；面板式编辑器：输入通道、算子、参数表单、预览（before 虚线 + after 实线）、Apply。
4. 输出作为普通派生通道进树（每源一个“派生”分组）、进画布、可编辑/移除。
5. Apply 替换来源曲线并继承颜色；不保留 ghost。
6. `.psl` v2 保存派生 recipes，导入重建。
7. 测试：`test/transform_golden.cpp`，逐算子比对首/末/数量与关键点；四元数用已知姿态（0/90/45°）验证。

验收：
- `attitude.roll` 加 MA(100) 后输出替换原曲线，颜色不变；重新拖原始通道可对比。
- `attitude_quat` 的 w/x/y/z 生成一个分组 `rpy`（roll/pitch/yaw 三列），数值与 PX4 公式一致。
- 保存布局重启后派生曲线和参数原样恢复。
- ctest 全绿，过滤算子有 golden 数值。

### P1 多源对比 + 时间对齐
任务：
1. `SourceData.displayOffsetSec`，画布 X 统一走 `displayTime()`。
2. 源行右键“时间偏移”“设为基准”“对比同名通道”。
3. 对比动作自动配对并着色，图例 `源A · x` / `源B · x`。
4. 时间游标：竖线 + 各曲线当前值读数。
5. 布局保存 offset。

验收：两次仿真的 `attitude.roll` 叠在一张画布，能手动对齐起点；重启后偏移保留。

### P2 导出与测量
任务：
1. 导出数据 CSV：原始/派生/当前时间窗，列名带源别名。
2. 单画布导出 PNG、复制曲线数据到剪贴板。
3. 区间测量：A/B 游标、Δt、Δy、斜率。
4. 设置补充：时间格式、采样上限、派生重算上限。

验收：CSV 可被 Excel 打开，列名不歧义；测量值与数据一致。

### P3 自定义函数编辑器（Lua 5.4，Luau 后续兼容）
不照搬 PJ 的 Lua Editor 界面，能力对齐：
1. `ps-script` 接 Lua 5.4（mlua，vendored），沙箱禁 `io/os/package`，步数/内存/单块时间预算。
2. 契约：`init(params)`、`reset()`、`step(t, value, v1..v3)` 返回 number 或分组 table；支持 1–4 输入、单/多输出。
3. UI：代码编辑、输入绑定、参数表单、before 虚线 + after 实线预览、Apply 替换、错误行号。
4. 内置片段：`quat_to_rpy`、derivative、integral、clamp、rad2deg、moving_average。
5. 源码随 `.psl` 保存；导入时重建通道并重新编译。
6. 兼容：先保证 PJ3 Lua 脚本可改写运行；Luau 语法差异后续加适配层，不阻塞。
7. Python 后置，编译开关控制。

验收：写一个 4 输入四元数脚本能出 roll/pitch/yaw 并绘图；错误脚本只报错不崩 UI；重启后脚本和参数恢复。

### P4 数据源 / 插件 / 渲染
- MCAP、Parquet、ROS2、JSON/CBOR/MessagePack。
- MQTT/UDP/WebSocket/LSL 实时流与真正的暂停/继续。
- 插件 SDK：loader/streamer/parser 三类，动态库 ABI。
- FFT、XY 图、直方图、姿态仪、GPS 地图。
- GPU 渲染与百万点基准。

---

## 4. 工程约束

- 注释极简，只解释“为什么”，不写教科书。
- 处理器纯函数式优先；状态机必须 `reset()` 可复现。
- 派生曲线只在可见/被引用时重算，避免打开大日志就全量跑。
- 所有持久化字段带版本号；v1 布局升级到 v2 时保留旧字段兼容。
- 每阶段结束：构建、ctest、截图、提交 Gitee `main`。

## 5. 参考的 PJ4 代码位置

- 处理器抽象：`pj_datastore/include/pj_datastore/data_processor.hpp`
- 内置过滤器与参数语义：`resources/filters/builtin_filters.luau`
- 惰性滤波曲线：`pj_plotting/core/include/pj_plotting/FilteredCurveAdapter.h`
- 多源合并/时间偏移：`pj_datastore/include/pj_datastore/merge_result.hpp`、`pj_runtime/include/pj_runtime/SessionManager.h`
- 脚本引擎：`pj_scripting/src/luau_engine.cpp`、`python_engine.cpp`
- 布局与曲线描述：`pj_plotting/core/include/pj_plotting/PlotXml.h`、`pj_runtime/include/pj_runtime/CurveDescriptor.h`
- 插件与市场：`pj_marketplace/`、`plotjuggler_sdk`
