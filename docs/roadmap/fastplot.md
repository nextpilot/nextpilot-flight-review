# fastplot：独立绘图库

| 项 | 值 |
|---|---|
| 状态 | P1–P4 完成（GUI 画布已接管，2026-11-18） |
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
