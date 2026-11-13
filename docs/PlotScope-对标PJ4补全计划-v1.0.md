# PlotScope 对标 PJ4 补全计划 v1.0

基线：`488fea9`（多数据源 + 源别名图例 + 布局持久化）
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
- 派生曲线惰性重算：参数或输入变化只置 dirty，下次绘制重算一次（对应 PJ `FilteredCurveAdapter::ensureFiltered()`）。
- 预览用“新建处理器 + 全量重算”，不复用累加器，避免预览污染正式结果。
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

- 结果数组归 `DerivedSeries` 所有（`QVector<double> t/v`），不改内核数据。
- 画布 `Series` 增加 `derivedId`，图例显示 `flight1 · attitude.roll → MA(100)`。
- 原始曲线可保留为半透明 ghost（PJ 的 raw ghost 行为），可开关。
- 派生曲线参与右键菜单：编辑变换、移除变换、导出数据。
- 派生定义写入 `.psl` 根节点 `derived`，画布系列按 `derivedId` 引用；导入时重建处理器和缓存。

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
| `quaternion_to_rpy` | 四元数→RPY（4 入 3 出） | `order`（wxyz / xyzw） |

语义细节按 PJ `resources/filters/builtin_filters.luau`：
- moving_average 不足窗口时用当前值补齐，除以完整窗口；可选按缓冲跨度居中时间戳。
- derivative / integral 首个样本抑制，integral 用梯形法。
- outlier_removal 只丢单点尖峰，不连续。
- samples_counter 输出 int64。
- scale 的时间平移作用在输出时间戳，不改输入。

### 2.4 多源对比

- 每个 `SourceData` 增加 `displayOffsetSec`：绘制 X = raw + offset；只影响显示和游标，不改数据。
- 源行右键/属性面板提供“时间偏移”“设为基准”。
- “对比同名通道”：选中两个源，按通道名配对，一次把两源的同一通道加到当前画布，颜色分源。
- 布局保存每源 offset 和别名。
- 说明：PJ4 用 `DatasetMergeSource{dataset_id, raw_shift_ns}` 把源折进 anchor；我们 v1 先用显示偏移，等数据层支持多 dataset 合并再做真正的 merge。

---

## 3. 阶段与验收

### P0 变换内核 + 派生曲线（优先）
任务：
1. 新建 `src/app/transform/`：`Processor.h`、`Catalogue.{h,cpp}`、`SisoFilters.cpp`、`QuatToRpy.cpp`、`DerivedSeries.{h,cpp}`。
2. 实现 12 个 SISO + 四元数 4→3，参数用 JSON 描述，目录可查询。
3. 字段树右键“添加变换…”；变换对话框：输入通道、算子、参数表单、预览（raw 虚线 + 结果实线）、确定生成派生曲线。
4. 派生曲线进树（每源一个“派生”分组）、进画布、可编辑/移除。
5. 画布 Series 支持 `derivedId` 与 ghost 原曲线。
6. `.psl` v2 保存派生 recipes，导入重建。
7. 测试：`test/transform_golden.cpp`，逐算子比对首/末/数量与关键点；四元数用已知姿态（0/90/45°）验证。

验收：
- `attitude.roll` 加 MA(100) 后原始与派生同图，图例可区分。
- `attitude_quat q[0..3]` 生成 roll/pitch/yaw 三条派生曲线，数值与 PX4 公式一致。
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

### P3 脚本（Luau / Python）
任务：
1. 先接 Luau（PJ4 用 Luau，兼容 Lua 语法），沙箱 + 步数/内存限制。
2. 自定义函数契约：`create(params)` 返回 `calculate(t, v)` / MIMO 版本。
3. 函数库：`quat_to_roll/pitch/yaw`、滤波模板、示例脚本。
4. 脚本随布局保存；错误显示在变换面板，不静默吞。
5. Python 后置，编译开关控制。

验收：PJ 风格的 Lua 脚本可直接改写使用；错误脚本不会崩 UI。

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
