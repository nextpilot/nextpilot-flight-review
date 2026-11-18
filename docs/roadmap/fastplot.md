# fastplot：独立绘图库

| 项 | 值 |
|---|---|
| 状态 | P1–P5 完成（GUI 画布已接管，2026-11-18） |
| 位置 | `ng/libs/fastplot/` |
| 目标 | 把老 `src/application/modules/plotting` 的**绘图能力**抽成独立库，供 ng 直接调用 |
| 约束 | `src/` 只读参考，不改一行；库不依赖 `ng/core`，纯 Qt Widgets |

## 1. 为什么单独成库

老画布 `PlottingCanvas.cpp` 3810 行，把五件事混在一个类里：

```
坐标变换 + 刻度算法 + 采样(LOD) + 交互状态机 + 业务胶水(数据源 id/别名/撤销)
```

ng 现在只有 `gui/views/plot/PlotCanvas.cpp` 203 行（能画一条线，没有缩放/书签/A-B）。
直接搬老文件等于把 3810 行原样搬进新架构，问题照旧。

**拆法**：能复用的纯绘图能力进库，业务胶水留在 ng 的模块里。

| 老代码 | 行数 | 去向 |
|---|---|---|
`PlottingCanvas` 的变换/刻度/采样/绘制/交互/导出/状态存取 | ~2800 | **进 fastplot** |
`Series` 里的 `srcId` / `srcPath` / `renameSource` / `removeChannelsFromSource` | ~50 | 留数据层 |
`PlottingRibbon.cpp` | 502 | 不抽（外壳） |
`PlottingSession.cpp` | 1507 | 不抽（页签/多文档/布局） |
`PlottingModule.cpp` + `PlottingLayout.cpp` | 380 | 不抽（宿主胶水/布局存盘） |

## 2. 边界

| 边界 | 内容 |
|---|---|
**是** | 视口变换、刻度（nice numbers）、像素列 LOD 采样、曲线/网格/轴/图例绘制、缩放/平移/框选/拾取、导出 PNG+CSV、状态 JSON |
**不是** | 数据加载、数据集/数据源模型、ribbon、页签、布局持久化、主题 QSS、脚本 |
依赖方向 | `fastplot` → Qt Widgets；**不反向依赖 `ng/core`**。数据由 ng 侧适配器传进来 |

## 3. 目录

```
ng/libs/fastplot/
  include/fastplot/
    Viewport.h       世界坐标 <-> 像素，x/y 范围
    ScaleDiv.h       刻度划分（主/次刻度 + 标签）
    Axis.h           一条轴：位置、刻度长度、标签、网格开关
    LodCache.h       像素列 min/max 缓存（P2）
    PlotData.h       IPlotData：数据访问抽象（P2）
    Curve.h          曲线样式 + 数据指针（P2）
    PlotWidget.h     画布：网格 + 轴 + 曲线 + 图层缓存
    Zoomer.h Panner.h Picker.h   交互部件（P3）
  src/
  CMakeLists.txt     目标名 fastplot，STATIC
```

命名：`namespace fastplot`，头文件 `#include <fastplot/PlotWidget.h>`。

## 4. 公开接口（P1 已落地部分）

```cpp
namespace fastplot {

class Viewport {
public:
    void   setPlotRect(const QRectF &);
    QRectF plotRect() const;
    void   setXRange(double lo, double hi);
    void   setYRange(double lo, double hi);
    double xToPixel(double x) const;
    double yToPixel(double y) const;
    double pixelToX(double px) const;
    double pixelToY(double py) const;
    QPointF toPixel(double x, double y) const;
private:  QRectF m_rect; double m_lo, m_hi ...;
};

class ScaleDiv {
public:
    static ScaleDiv linear(double lo, double hi, int maxMajorTicks);
    double lower, upper, majorStep;
    QVector<Tick> ticks;      // {value, label, isMajor}
    int indexBefore(double v) const;
};

class Axis {
public:
    void setSide(AxisSide), setInk(QColor), setFont(QFont),
         setTickLength(int), setGridVisible(bool), setScaleDiv(ScaleDiv);
    void paint(QPainter *, const QRectF &plotRect, const QRect &widgetRect);
    int  preferredMargin() const;      // 给 PlotWidget 算左边距
};

class PlotWidget : public QWidget {
public:
    Viewport &viewport();
    Axis     &axis(AxisSide);          // Left / Right / Bottom / Top
    void setXRange(double, double);
    void setYRange(double, double);
    void replot();                     // 只失效缓存 + update
    QRectF plotRect() const;
    QImage grabImage();                // 离屏渲染，导出与测试用
};

} // namespace fastplot
```

后续批次按需扩展（P2 加 `setCurves`、`IPlotData`；P3 加 `Zoomer` 等）。

## 5. 效率三条硬约束（不可破）

| 约束 | 做法 | 违反的后果 |
|---|---|---|
**零拷贝** | 曲线只持有 `const double *x, *y + n + generation`；数据变更只换指针 | 每次刷新复制 10⁶ 点，卡顿回到老问题 |
**像素列聚合** | 渲染按屏幕像素列取 min/max，一列一条竖线；typed 表优先走 `ps_dataset_lod_minmax` | 逐点 `drawLine` 描 10⁶ 点，缩放一次几百毫秒 |
**图层缓存** | 网格/轴/图例画进 `QPixmap`，只在视口/尺寸/主题变化时重画 | 鼠标移动触发整屏重绘，hover 就掉帧 |

## 6. 分批与验收

| 批次 | 内容 | 验收 |
|---|---|---|
**P1** | `Viewport` + `ScaleDiv` + `Axis` + `PlotWidget`（网格+轴），CMake 目标 `fastplot` | 离屏渲染出干净坐标网格；`test_fastplot` 断言刻度、变换、边距 【本次】 |
**P2** | `IPlotData` + `Curve` + `LodCache`；ng 侧适配器把 `Dataset` 通道接进来 | `demo_flight.ulg` 画 `attitude.roll` 3000 点，与老画布同帧率 【已完成，见下】 |
**P3** | `Zoomer/Panner/Picker` + 框选 + 键盘 | 缩放/框选/读数与老行为一致 【已完成，见下】 |
**P4** | 双轴/图例/hover/导出 PNG+CSV/状态 JSON | 双轴可画，导出与复原正确 |
**P5** | 专用仪表（horizon/compass/gauge/gps map/3D） | 按需，`fastplot/gadgets` 子目标 |

### P2 落地记录（2026-11-18）

| 能力 | 实现 |
|---|---|
数据抽象 | `IPlotData`（size/xAt/yAt/directAccess/revision）+ 非拥有视图 `ArrayData` |
内核 LOD 直通 | `Curve::fromLod(QVector<LodPoint>)`——ng 的 `Dataset::lodMinMax` 结果直接画，**C++ 不重扫** |
C++ 侧聚合 | `LodCache`：O(n) 一次扫描 → 每屏幕列 min/max；按（数据版本, 视口, 列数）缓存，命中不重算 |
绘制策略 | 点数 ≤ 列数 → 逐点折线（保真）；否则像素列包络（竖线 + 中点连线），成本 O(像素宽) |
自动定界 | `autoScaleX/Y`：扫数据取极值，两端留余量；LOD 数据只扫三元组 |
验收 | `fastplot` 测 3000 点正弦：700 列、峰值不丢、缓存命中不重扫；`fastplot_dataset` 测真实 `demo_flight.ulg`：3000 行 → 592 三联 → 出图 |

实测（vm，offscreen）：ULog 3000 行 → 592 个三元组，曲线 5459 个像素，`fastplot_p2_ulg.png`。

**GUI 接管**：`gui/views/plot/PlotCanvas` 不再自己画——内部持一个 `fastplot::PlotWidget`，
只做"取 LOD → 转三元组 → addCurve"。`ng_gui` 公共链接 `fastplot`；CLI 不链，headless 依旧纯净。
`plotscope-ng-gui-shot demo_flight.ulg` 出图：数据集树 3000 行 + `attitude.roll` 曲线 + 左/下轴刻度齐全。

### P3 落地记录（2026-11-18）

| 部件 | 行为 |
|---|---|
`Zoomer` | 滚轮以光标为锚点缩放（Shift 仅 x / Ctrl 仅 y）；右键/中键框选缩放；`+`/`-`/`0`/方向键；`setHomeRange` + `resetZoom` |
`Panner` | 左键拖动平移，像素位移→数据位移，与缩放正交、互不知道对方 |
`Picker` | 悬停取最近样本（LOD 二分查找，数据曲线按步长抽样），发 `hovered(x,y,valid)`；十字光标+右上角读数由画布按状态绘制 |
画布 | `setRubberBand/setHover/setOverlayText` + `nearestSample`——只画状态，不做判断 |
ng 接线 | `PlotCanvas` 装三个部件；`zoomed/panned` → `rangeChanged(t0,t1)` → `ChannelView` 合并定时器重取 LOD（`plot.channel` 新增 `t0Us/t1Us` 窗口参数，越界夹回表范围） |

这样放大后画的是**窗口内**的 min/max，不是整段数据的包络——细节跟得上缩放。

### P4 落地记录（2026-11-18）

| 能力 | 实现 |
|---|---|
双轴 | `Curve::setYAxis(Right)` 声明归属；`PlotWidget::setYRange2` + 右轴独立的 `ScaleDiv`/映射 |
图例 | `Curve::setName` + `setLegendVisible`；色块 + 名字画在绘图区左上角，不参与 LOD 缓存 |
导出 | `exportPng()`（所见即所得）、`exportCsv()`（LOD 出 `x,min,max`、数据出 `x,y`，各曲线一段，带范围头注释） |
状态 | `saveState()/restoreState()` 只存视图（x/y/y2 范围、轴可见性、背景、网格、图例），数据归宿主 |
`autoScaleY` | 左右轴**各算各的**：挂右轴的曲线不参与左轴定界，否则两条不同量级的曲线会互相压扁 |

**踩到的真问题**：`Axis` 原来把纵向映射写死成 `vp.yToPixel`，于是右轴的刻度线仍按左轴范围摆放（图上表现为右轴标签挤在一起、曲线"看起来走了左轴"）。修法是让轴自带范围——`Axis::setYRange(lo,hi)`，不设则退回 Viewport，单轴场景零改动。**一条轴只认自己的范围，不再向画布借映射**，这是双轴能成立的前提。

### P5 落地记录（2026-11-18）

六个仪表各自一个类，共同底座只有 `Instrument`（底色 + 内容区 + `paintInstrument`）。它们**不认识 `PlotWidget`**，`PlotWidget` 也不认识它们：一个管坐标轴与视口，一个管"把几个标量画成一个形状"，硬塞在一起会把两套模型搅浑。

| 部件 | 输入 | 说明 |
|---|---|---|
`AttitudeIndicator` | roll / pitch（角度） | 姿态球：先裁圆，再在圆里填天/地两个半平面 |
`CompassRose` | heading | 航向归一化到 [0,360)，每 30° 一刻度，北向高亮 |
`DialGauge` | value + [lo,hi] + title | 270° 弧 + 指针 + 中央数值；有 `setValueMissing()` 空态 |
`TrackPlot` | 经度 / 纬度两条 `IPlotData*` | 经纬轨迹 + 网格（**没有底图**，所以不叫 Map） |
`Trajectory3D` | X / Y / Z（Z 可空） | 正交投影折线 + 参考立方体，不引 OpenGL |
`WaterfallPlot` | 多条 `IPlotData*` | 沿 z 错开堆叠的瀑布（**不是曲面**，所以不叫 Surface） |

**零拷贝的代价要写明白**：`TrackPlot` / `Trajectory3D` / `WaterfallPlot` 都只持 `const IPlotData*`，**不放数据**。于是调用方必须保证被指对象的地址稳定——测试里第一次把 `ArrayData` 放进 `QVector` 边 push 边取地址，扩容搬走元素后指针变野指针，直接 segfault。规矩是：装 `IPlotData` 的容器先 `reserve`，或者干脆放在 `std::deque` / 堆上。

**踩到的第二个坑**：姿态球第一版画两个错位的大圆当"天/地"，结果整块圆被天盖住——那不是地平线。正解是 `painter.setClipPath(圆)` 后填两个半平面，地平线只是一条分界线。

## 7. 构建接入

| 规则 | 说明 |
|---|---|
只进 GUI | `fastplot` 是 Widgets 库，只允许 `ng_gui` 与 GUI 测试链接；`plotscope-ng`（CLI）不链 |
源树只读 | `ng/CMakeLists.txt` 里 `add_subdirectory(libs/fastplot)`；不改 `src/` |
复用第三方 | SARibbon / ADS 仍走 `../src/3rdparty`，fastplot 不引新依赖 |

## 8. 风险

| 风险 | 应对 |
|---|---|
交互状态机搬迁走样 | P3 之前不许把交互写进 `PlotWidget::paintEvent`，一律做成 `Zoomer/Panner/Picker` 部件 |
数据适配层反过来污染库 | `ColumnSeries` 这类适配器写在 ng（`modules/plot` 或 `gui/views/plot`），不进 `fastplot` |
双轴/多轴口径不一致 | 轴一律走 `Axis` 实例，禁止在曲线里写死“左/右”判断逻辑 |
和老画布行为漂移 | 每批验收都用同一个 fixture + 同一段脚本，出图像素级对比 |

## 9. P5 接入 ng（仪表走意图 + 视图自注册）

六个仪表没有直接塞进主窗口，而是沿用既有"意图 → 视图适配 → 库绘制"链路：

| 层 | 落地 |
|---|---|
| 意图 | `plot.instrument`：params = `dataset` / `kind` / `roles`(角色→`table.column`) / `channels`(瀑布序列) / `range`(可选) / `cap`(64..65536)。返回 `scalars`(角色末值)、`series`(角色→{time,value,name})、`channels`、`range`、`title` |
| 模块 | `PlotModule` 按 kind 校验必需角色（attitude 至少 roll/pitch 其一；compass yaw/heading；gauge value；track lon/lat；traj3d x/y，z 可选；waterfall channels≥1），缺角色/未知 kind/无数据集均返回失败语义 |
| 视图 | `InstrumentView`：`QStackedLayout` 切六仪表；缓冲由视图持有（`RoleBuffer` + 瀑布缓冲 `reserve(64)` 后只 resize），仪表只持指针；面板以 `NG_REGISTER_PANEL("plot-instrument", ...)` 自注册进工厂表 |
| 测试 | `ng_test_instrument`：offscreen 内存造数，六 kind 逐一断言 scalars/series/channels/range + 出图非背景像素；错误语义（未知 kind、缺通道、数据集 9999）；工厂表可取到 `InstrumentView*` |

**和曲线画布的分工不变**：抽稀/取数在 Rust 内核与模块意图层，坐标/绘制在 fastplot，视图只做适配；仪表与曲线画布互不认识。

**遗留入口形态**：仪表在主界面怎么被打开（ribbon 动作 / 面板切换 / 双击通道按名推断 kind）尚未拍板；当前面板已注册、可按 id 请求，主窗口暂未挂入口。

## 10. 搬迁差距盘点（2026-11-18，对照老 `src/application/modules/plotting`）

### 已迁入（库 + 意图 + 视图 + 测试闭环）

| 能力 | 落地 |
|---|---|
坐标系 | Viewport/ScaleDiv/Axis/PlotWidget，网格、双轴、左右轴各自自动定界（P1/P4） |
曲线数据 | IPlotData/ArrayData 零拷贝、Curve、LodCache；Rust LOD 直通，C++ 不重扫（P2） |
交互 | Zoomer（滚轮/框选/键盘/复位）、Panner、Picker 悬停读数；窗口内重取 LOD（P3） |
库级导出/状态 | exportPng/exportCsv、saveState/restoreState（P4 库层；P6 经视图动作接进 ng，ribbon 可触发） |
散点符号 | Curve 符号模式（Circle/Square/Diamond/Cross）+ scatter 不连线（P7 库层） |
六仪表 | 姿态/罗盘/表盘/经纬轨迹/3D 轨迹/瀑布；`plot.instrument` 意图 + InstrumentView 自注册面板（P5） |
布局模型 | PlotLayout 存/读 pages 与 split 节点 JSON |

### 未迁：绘图类型

| 老模式 | 状态 | 备注 |
|---|---|---|
Scatter 散点 | **库层已迁（P7）** | ng 视图尚未有模式选择入口，待会话层一起接 |
BarChart 柱状图 | **库层已迁（P8）** | `BarChart`：类别-值柱，含负值基线；ng 视图入口待接 |
Histogram 直方图 | **库层已迁（P8）** | `Histogram`：值域分箱计数，revision 缓存；ng 视图入口待接 |
Spectrum 频谱 | **库层已迁（P9）** | `SpectrumPlot`：去均值单边 DFT，朴素 O(n²)，后续换 FFT；ng 视图入口待接 |
Surface3D 三维曲面 | **老实现已被 WaterfallPlot 覆盖** | 老 `paintSurface3D` 本身就是 z 向错开的折线瀑布（非真曲面）；真栅格曲面留待将来另立 `SurfacePlot` |

### 未迁：画布 / 会话层

| 能力 | 状态 |
|---|---|
多页多画布（新建页、增删画布、水平/垂直拆分、行列网格、最大化还原） | 只有 PlotLayout JSON 模型，shell 无拆分画布 UI |
A/B 双游标测量（Δt/Δy） | 未迁 |
时间书签标记线、播放头竖线 | 未迁 |
数据点符号样式（形状/大小） | 库层已支持（P7），视图选择入口待接 |
系列/轴属性编辑（颜色、左右轴归属、线型） | 未迁 |
导出 PNG/CSV、视图状态/图例开关入口 | **已接（P6）**：视图动作登记到 AppContext，外壳按 id 派发 |
仪表主界面入口 | 面板已注册 `plot-instrument`，ribbon 动作 / 双击通道推断 kind 未定 |

### 测试现状与缺口

- 已覆盖：ctest 8/8——架构装配、GUI 装配、解析管道、P1–P4 库能力、真实 ULog LOD、六仪表库断言、ng 仪表意图+视图、画布导出/状态/图例（含经 AppContext 视图动作派发闭环）、散点符号出图。
- 未覆盖：真实 fixture 的 track/traj3d/waterfall（该 fixture 无 gps/pos/多序列通道）；多画布拆分；A/B 游标；柱/直方/频谱/散点的 ng 视图入口；真栅格曲面；Windows 构建（本轮仅 Linux offscreen）。

## 12. P8 柱状/直方图、P9 频谱（2026-11-18）

| 部件 | 输入 | 说明 |
|---|---|---|
`BarChart` | 类别标签 + 值数组 + 可选配色 | 0 基线，负值向下；槽位留白比例可调；类别标签底部居中省略 |
`Histogram` | `IPlotData*`（y 值）+ 箱数 | 有限值过滤 → min/max 分箱计数；按 revision+点数缓存，内容不变不重算 |
`SpectrumPlot` | `IPlotData*` + 采样间隔 dt | 去均值 → 单边 DFT 幅值；频率轴按 dt 换算，dt 缺省归一化；**当前朴素 DFT O(n²)**，点数上来后换 FFT |

三者与仪表同底座 `Instrument`（底色 + 内容区），不认识 PlotWidget。测试：三柱含负值、32 箱双峰、256 点 5Hz 正弦幅值谱，均出图断言。

**老 `Surface3D` 核对结论**：翻老码确认它不是曲面——多条序列沿斜向 z 轴错开、按深度改透明度的折线瀑布，与已迁的 `WaterfallPlot` 同一形态（斜投影 + 深度 alpha 都已具备），不另立部件。真三维栅格曲面（网格/插值/着色）是新能力，将来另立 `SurfacePlot`，不占用 Waterfall 这个名字。

## 11. P6 视图动作机制 与 P7 散点（2026-11-18）

### P6：库能力接进 ng

导出/状态/图例是**部件侧动作**，模块层拿不到 QWidget。为此在 AppContext 加了一张视图动作表（与 PanelFactoryRegistry 同思路：只存 `std::function`，不含 QWidget 类型，core 依旧 headless 纯净）：

| 环节 | 落地 |
|---|---|
登记 | `createPlotCanvas` 工厂把 `plot.export.png` / `plot.export.csv` / `plot.state.save` / `plot.state.restore` / `plot.legend.toggle` 处理器登记到 AppContext，闭包捕获画布 |
派发 | 外壳 ribbon 触发时先 `dispatchViewAction`，命中交视图执行；未注册再回落意图层（CLI/脚本同路径） |
选文件 | 导出路径对话框是外壳交互能力，留在 MainWindow（与 openDataFile 同例），处理器只收 `path` 参数 |
测试 | `ng_test_plot_io`：直接调画布薄包装 + 经 AppContext 派发两条路径都断言；未知动作回落意图层 |

**双轴未在本批接 UI**：当前画布是单曲线，左右轴归属要等多曲线叠加一起做；库层双轴能力 P4 已就绪。

### P7：散点符号

- `Curve::setScatter(true)` 不连线；`setSymbol(Circle/Square/Diamond/Cross)` + `setSymbolSize`。
- 三种数据路径（LOD 桶 / 稀疏逐点 / 密集包络）都按 scatter 分支画符号；LOD 与密集路径取桶中点。
- 测试：200 点菱形散点，1821 个符号像素，`fastplot_scatter.png`。
- **遗留**：ng 视图还没有"按通道/图型选散点模式"的入口，随会话层图型选择一起接。
