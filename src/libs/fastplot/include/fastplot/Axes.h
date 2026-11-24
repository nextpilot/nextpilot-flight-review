#pragma once

#include "fastplot/ABMeasureOverlay.h"
#include "fastplot/Axis.h"
#include "fastplot/Artist.h"
#include "fastplot/BookmarkOverlay.h"
#include "fastplot/CrosshairOverlay.h"
#include "fastplot/CursorReadoutOverlay.h"
#include "fastplot/Legend.h"
#include "fastplot/Line.h"
#include "fastplot/MarkerOverlay.h"
#include "fastplot/Overlay.h"
#include "fastplot/StateBandOverlay.h"
#include "fastplot/Viewport.h"

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QPointF>
#include <QRect>
#include <QJsonObject>

#include <optional>
#include <QString>
#include <QWidget>

namespace nextpilot::plotscope::fastplot
{

// 画布：背景 + 网格 + 四条轴 + 静态图层缓存。
//
// P1 只画"空坐标系"；曲线在 P2 通过 setCurves 接进来（数据零拷贝）。
// 网格/轴这类"每帧都一样"的内容画进 QPixmap，只有视口/尺寸/配色变化才重画——
// 这是 hover 与拖动不掉帧的前提。
class Axes : public QWidget
{
    Q_OBJECT
public:
    explicit Axes(QWidget *parent = nullptr);
    ~Axes() override;

    Viewport &viewport() { return m_vp; }
    const Viewport &viewport() const { return m_vp; }

    Axis &axis(AxisSide side);
    const Axis &axis(AxisSide side) const;

    void setXRange(double lo, double hi);
    void setYRange(double lo, double hi);

    void setBackground(const QColor &color);
    QColor background() const { return m_background; }

    // 画布标题（左上角）。非空时顶部会额外预留一行字高。
    void setTitle(const QString &title);
    // 标题栏高度（像素）：画布内的悬浮工具栏按这条横带对齐。
    static constexpr int kTitleBarHeight = 22;
    // 标题栏底色：也供上层悬浮工具栏取同一个色，避免两个"栏"色差。
    QColor titleBarColor() const;
    QString title() const { return m_title; }
    void setTitleColor(const QColor &color);

    void setGridVisible(bool visible);
    // 按轴开关网格：Bottom 轴管竖线（X 刻度），Left/Right 轴管横线（Y 刻度）。
    // 「显示」组的 X 网格 / Y 网格两个开关走这里，整体开关仍走 setGridVisible。
    void setAxisGridVisible(AxisSide side, bool visible);
    bool axisGridVisible(AxisSide side) const;
    // 画布边框（plotRect 四边）：老界面「显示」组有独立开关。
    void setBoxVisible(bool visible);
    bool boxVisible() const { return m_box; }
    void replot();

    // ---- 曲线（P2）----
    // 按值移入并返回引用，便于连续调样式；曲线不拥有数据。
    Line &addCurve(Line curve);
    void clearCurves();
    int curveCount() const { return m_curves.size(); }

    // 按序取曲线（只读）：图例/自检/导出等"我有哪些曲线"的场景。
    const Line &curveAt(int index) const { return m_curves.at(index); }

    // ---- MATLAB / matplotlib 风格的绘图入口 ----
    // plot：新增一条曲线（等价于 addCurve，返回引用便于链式设样式）。
    Line &plot(Line curve) { return addCurve(qMove(curve)); }

    // bar/hist/spectrum：在坐标系里挂载一个 artist 图元并绘制。
    // 返回的对象由本 Axes 所有（QWidget 父子），生命周期跟随坐标系。
    class Bar &bar(const QStringList &labels, const QVector<double> &values,
                   const QVector<QColor> &colors = {});
    class Histogram &hist(const IPlotData *data, int bins = 40);
    class Spectrum &spectrum(const IPlotData *data, double dt = 0.0);

    // 通用 artist 挂载（非拥有：调用方负责生命周期；用于自定义 artist）。
    void addArtist(Artist *artist);
    void clearArtists();
    int artistCount() const { return m_artists.size(); }

    // ---- 轴与外观（MATLAB 风格别名）----
    void xlim(double lo, double hi) { setXRange(lo, hi); }
    void ylim(double lo, double hi) { setYRange(lo, hi); }
    void grid(bool on) { setGridVisible(on); }
    bool gridVisible() const { return m_grid; }
    void box(bool on) { setBoxVisible(on); }
    bool boxVisibleAlias() const { return m_box; }
    void legend(bool on) { setLegendVisible(on); }
    void xlabel(const QString &text) { setAxisLabel(AxisSide::Bottom, text); }
    void ylabel(const QString &text) { setAxisLabel(AxisSide::Left, text); }
    void setAxisLabel(AxisSide side, const QString &text);
    QString axisLabel(AxisSide side) const;

    // 第二 y 轴（右轴）的数据范围。曲线用 Line::setYAxis(Right) 声明挂到它上面。
    void setYRange2(double lo, double hi);
    double y2Lower() const { return m_y2Lo; }
    double y2Upper() const { return m_y2Hi; }
    bool hasSecondAxis() const { return m_secondAxis; }
    void setSecondAxis(bool enabled);

    // 按曲线数据自动定范围（两端各留 marginRatio 的余量）；无数据返回 false。
    // 左/右轴各算各的：挂在右轴的曲线不参与左轴定界。
    bool autoScaleY(double marginRatio = 0.05);
    bool autoScaleX(double marginRatio = 0.02);
    bool autoScale();   // 两轴一起

    QRectF plotRect() const { return m_vp.plotRect(); }

    // ---- 交互叠加层（渲染由 CrosshairOverlay 负责，Axes 只委托）----
    // 框选橡皮筋（像素坐标）。空 QRect = 清除。
    void setRubberBand(const QRect &pixels) { m_crosshair.setRubberBand(pixels); }
    QRect rubberBand() const { return m_crosshair.rubberBand(); }
    // 十字光标：给一个数据坐标；active=false 时隐藏。
    void setHover(const QPointF &dataPoint, bool active) { m_crosshair.setHover(dataPoint, active); }
    // 播放头竖线：给一个 x 数据坐标；active=false 时隐藏。
    // 画同色竖线。
    void setPlayhead(double x, bool active) { m_crosshair.setPlayhead(x, active); }
    bool playheadActive() const { return m_crosshair.playheadActive(); }
    double playheadX() const { return m_crosshair.playheadX(); }
    // 联动游标竖线：同页其它画布的时间同步，只画竖线不画读数框（对齐老 setLinkedCursor）。
    void setLinkedCursor(double x, bool active) { m_crosshair.setLinkedCursor(x, active); }
    // 右上角读数文本（空串 = 不画）。
    void setOverlayText(const QString &text) { m_crosshair.setOverlayText(text); }
    void clearOverlay() { m_crosshair.clearTransient(); update(); }

    // ---- 读数叠加层（对齐老 PlottingCanvas 的打磨交互；渲染由 CursorReadoutOverlay 负责）----
    // 逐曲线读数行：曲线名 + 该时刻值 + 曲线色。取值/格式化都在 AxesView 做（库不依赖 Dataset）。
    using ReadoutRow = ::nextpilot::plotscope::fastplot::ReadoutRow;
    // 十字游标 + 逐曲线读数框。x 为数据时间；pinned=固定读数（标题不同+高亮边框）。
    void setCursorReadout(double x, bool active, bool pinned,
                          const QString &header, const QVector<ReadoutRow> &rows);
    bool cursorReadoutActive() const { return m_cursorReadout.isActive(); }
    double cursorReadoutX() const { return m_cursorReadout.x(); }

    // A/B 双游标测量：a/b 为数据时间，hasB=false 时只画 A 和提示。
    // rows 为每曲线 "Δy / 斜率" 读数；firstDelta 为首条可见曲线在 A→B 间的 Δy（画直角三角形）。
    void setAB(bool enabled, double a, double b, bool hasB,
               const QString &header, const QVector<ReadoutRow> &rows,
               bool drawTriangle, double ay, double by, double ayPixelData);
    bool abEnabled() const { return m_abMeasure.isEnabled(); }

    // 时间书签：稳定 id + 时间 + 标签 + 是否选中。
    using BookmarkMark = ::nextpilot::plotscope::fastplot::BookmarkMark;
    void setBookmarks(const QVector<BookmarkMark> &marks);

    // Marker 规则产出：竖线事件 / 时间区间带 / 值区间带。
    using MarkerMark = ::nextpilot::plotscope::fastplot::MarkerMark;
    void setMarkers(const QVector<MarkerMark> &marks);
    int markerCount() const { return m_markerOverlay.marks().size(); }

    // 状态视图：每条通道一行色带，每段是一个连续不变的状态
    // （label 为着色键，见 StateColors.h）。绘制在绘图区顶部（每行 22px + 6px 间距）。
    using StateBandMark = ::nextpilot::plotscope::fastplot::StateBandMark;
    using StateRowMark = ::nextpilot::plotscope::fastplot::StateRowMark;
    void setStateBands(const QVector<StateRowMark> &rows);
    void clearStateBands() { setStateBands({}); }
    int stateRowCount() const { return m_stateBandOverlay.rowCount(); }

    // 像素/数据互转 + 命中：交互部件据此判断点到了哪条线/书签。
    double pixelToX(double px) const;
    double xToPixel(double x) const;

    // 图例：独立组件（Legend 类）负责状态/布局/绘制/命中，Axes 组装快照并委托。
    // 图例顶部让位：图例块从 plotRect.top()+inset 起排（默认 0）。
    // 画布上方压着悬浮工具栏时，由上层设一个大于工具栏高度的值，图例就不会被按钮压住。
    void setLegendTopInset(qreal px) { m_legend.setTopInset(px); }
    qreal legendTopInset() const { return m_legend.topInset(); }

    void setLegendVisible(bool visible);
    bool legendVisible() const { return m_legend.isVisible(); }

    // === 图例交互（第二梯队 #205，
    // 点击行切显隐（隐藏行灰显仍占位）、标题栏可折叠、Ctrl+滚轮改字号。
    // 命中检测基于最近一次 paint 缓存的行矩形（resize/paint 后自动更新）。
    int legendHitTest(const QPointF &pixel) const; // >=0 曲线行(m_curves 索引)；-1 标题栏；-2 未命中
    bool legendCollapsed() const { return m_legend.isCollapsed(); }
    void setLegendCollapsed(bool collapsed) { m_legend.setCollapsed(collapsed); }
    double legendFontScale() const { return m_legend.fontScale(); }
    void zoomLegendFont(int steps); // Ctrl+滚轮步进：缩放因子 clamp [0.7, 2.0]
    void toggleLegendItem(int curveIndex);
    // set 版：直接置显隐并发出同一信号（legendItemToggled）。AxesView 的
    // 单一出口 setSeriesVisible 用它同步 Line；toggle 是"取反"的语法糖路径。
    void setLegendItemVisible(int curveIndex, bool visible);
    bool legendItemVisible(int curveIndex) const
    {
        return curveIndex >= 0 && curveIndex < m_curves.size() && m_curves.at(curveIndex).isVisible();
    }
    Legend::Corner legendCorner() const { return m_legend.corner(); }
    void setLegendCorner(Legend::Corner corner) { m_legend.setCorner(corner); update(); }

    // 图例行被点击切换显隐（AxesView 同步 PlotSeries.visible，pushToPlot 重建不丢）。
    // Legend::collapseChanged 转发为 legendCollapseChanged（构造时连接）。
Q_SIGNALS:
    void legendItemToggled(int curveIndex, bool visible);
    void legendCollapseChanged(bool collapsed);

public:
    // ---- 参考线（第3仗 3c / F-2，对标 PJ4 参考线 + CurveTracker 相对 Δ）----
    // 存**相对时间轴值**：我们的 x 天然抗 offset（offset 只在绘图副本，不进轴范围/存盘）。
    // 参考线随视图状态持久化（saveState/restoreState 的 referenceT 键）。
    void setReferenceTime(double tSec)
    {
        m_referenceT = tSec;
        update();
    }
    void clearReference()
    {
        m_referenceT.reset();
        update();
    }
    bool hasReference() const { return m_referenceT.has_value(); }
    double referenceTime() const { return m_referenceT.value_or(0.0); }
    // 游标像素 Y（读数框纵向锚点，第3仗 3c / E-4）。<0 = 无锚点回顶部锚位。
    void setCursorPixelY(double pixelY)
    {
        m_cursorReadout.setCursorPixelY(pixelY);
        update();
    }
    QRectF cursorReadoutBoxForTest() const { return m_cursorReadout.boxRectForTest(); }
    // 导出：PNG（所见即所得）/ CSV（可见数据，LOD 输出 x,min,max）。
    bool exportPng(const QString &path);
    bool exportCsv(const QString &path);

    // 视图状态：范围 + 轴可见性 + 背景/网格。存盘/复原用（不含数据本身）。
    QJsonObject saveState() const;
    bool restoreState(const QJsonObject &state);

    // 鼠标最近样本（Picker 用）：返回是否找到，输出数据坐标。
    bool nearestSample(const QPointF &pixel, double *x, double *y) const;

    // 离屏渲染，导出与测试用。
    QImage grabImage();

    // 诊断计数（第1仗脏调度）：稳态叠加层缓存重建次数。游标移动不应使其增长。
    int overlayCacheRebuilds() const { return m_overlayCacheRebuilds; }

    QSize minimumSizeHint() const override { return QSize(240, 160); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    Axis *axisPtr(AxisSide side);
    void paintCurves(QPainter &painter);
    void paintOverlay(QPainter &painter);
    void paintLegend(QPainter &painter);
    void paintAB(QPainter &painter, const QRectF &plot);
    void paintBookmarks(QPainter &painter, const QRectF &plot);
    void paintMarkers(QPainter &painter, const QRectF &plot);
    void paintStateBands(QPainter &painter, const QRectF &plot);
    double yToPixel(AxisSide side, double y) const;
    QRectF dataBounds() const;
    void relayout();       // 边距 → 绘图区 → 刻度划分（三者必须一起算）
    void refreshScaleDivs();
    void invalidateStatic();

    Viewport m_vp;
    Axis m_axes[4];        // 索引 = AxisSide
    QColor m_background{0x10, 0x16, 0x1c};
    // 第二 y 轴的数据范围与开关（双轴不需要任何特判：只是另一套映射）。
    double m_y2Lo = 0.0;
    double m_y2Hi = 1.0;
    bool m_secondAxis = false;
    // 图例：状态/布局/命中全部在 Legend 类内，这里只留组件与绘制入口。
    Legend m_legend;
    bool m_grid = false;
    bool m_box = false;            // 画布边框（对齐老界面显示组）
    // 读数叠加层状态（"画什么"由各自 Overlay 类自持，Axes 只留组件）。
    CursorReadoutOverlay m_cursorReadout;   // 十字游标 + 逐曲线读数框
    // 参考线：相对时间轴值（第3仗 3c / F-2）。
    std::optional<double> m_referenceT;
    ABMeasureOverlay m_abMeasure;           // A/B 双游标测量
    BookmarkOverlay m_bookmarkOverlay;      // 时间书签
    MarkerOverlay m_markerOverlay;          // Marker 规则产出
    StateBandOverlay m_stateBandOverlay;    // 状态视图色带
    CrosshairOverlay m_crosshair;           // 悬浮十字/播放头/联动游标/橡皮筋/浮字
    QColor m_titleColor{0x33, 0x33, 0x33};
    QString m_title;
    QPixmap m_staticCache;
    bool m_staticDirty = true;
    // 稳态叠加层缓存（第1仗脏调度）：StateBand/Marker/Bookmark/AB 内容不随游标变化，
    // 缓存成透明层复用；游标层（Crosshair/CursorReadout）每帧实时画在其上。
    // 游标移动只走实时层，稳态层不重建——这是 P-2「游标移动不重算」的渲染侧保证。
    QPixmap m_overlayCache;
    bool m_overlayDirty = true;
    int m_overlayCacheRebuilds = 0;  // 诊断：稳态层缓存重建次数
    void markOverlayDirty(quint32 layers);  // 标脏指定层；命中稳态组则触发缓存重建
    quint32 m_overlayDirtyLayers = OverlayLayerAll;  // 本帧脏层（供层内部优化）
    QVector<Line> m_curves;
    // 画在坐标系里的 artist 图元。bar/hist/spectrum 创建的实例由本 Axes 以父子所有权持有；
    // addArtist 挂载的外部实例不拥有，clearArtists 只清空不 delete。
    QVector<Artist *> m_artists;
};

// 兼容别名：PlotWidget 即 Axes。
using PlotWidget = Axes;

} // namespace nextpilot::plotscope::fastplot
