#pragma once

#include "fastplot/Axis.h"
#include "fastplot/Artist.h"
#include "fastplot/Line.h"
#include "fastplot/Viewport.h"

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QPointF>
#include <QRect>
#include <QJsonObject>
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

    // ---- 交互叠加层（部件负责状态，画布只负责画）----
    // 框选橡皮筋（像素坐标）。空 QRect = 清除。
    void setRubberBand(const QRect &pixels);
    QRect rubberBand() const { return m_rubberBand; }
    // 十字光标：给一个数据坐标；active=false 时隐藏。
    void setHover(const QPointF &dataPoint, bool active);
    // 播放头竖线：给一个 x 数据坐标；active=false 时隐藏。
    // 画同色竖线。
    void setPlayhead(double x, bool active);
    bool playheadActive() const { return m_playheadActive; }
    double playheadX() const { return m_playheadX; }
    // 联动游标竖线：同页其它画布的时间同步，只画竖线不画读数框（对齐老 setLinkedCursor）。
    void setLinkedCursor(double x, bool active);
    // 右上角读数文本（空串 = 不画）。
    void setOverlayText(const QString &text);
    void clearOverlay();

    // ---- 读数叠加层（对齐老 PlottingCanvas 的打磨交互；状态由上层持有，画布只画）----
    // 一行读数：曲线名 + 该时刻值 + 曲线色。取值/格式化都在 PlotCanvas 做（库不依赖 Dataset）。
    struct ReadoutRow
    {
        QString label;
        QString value;
        QColor color;
    };
    // 十字游标 + 逐曲线读数框。x 为数据时间；pinned=固定读数（标题不同+高亮边框）。
    void setCursorReadout(double x, bool active, bool pinned,
                          const QString &header, const QVector<ReadoutRow> &rows);
    bool cursorReadoutActive() const { return m_cursorActive; }
    double cursorReadoutX() const { return m_cursorX; }

    // A/B 双游标测量：a/b 为数据时间，hasB=false 时只画 A 和提示。
    // rows 为每曲线 "Δy / 斜率" 读数；firstDelta 为首条可见曲线在 A→B 间的 Δy（画直角三角形）。
    void setAB(bool enabled, double a, double b, bool hasB,
               const QString &header, const QVector<ReadoutRow> &rows,
               bool drawTriangle, double ay, double by, double ayPixelData);
    bool abEnabled() const { return m_abEnabled; }

    // 时间书签：稳定 id + 时间 + 标签 + 是否选中。
    struct BookmarkMark
    {
        QString id;
        double t = 0.0;
        QString label;
        bool selected = false;
    };
    void setBookmarks(const QVector<BookmarkMark> &marks);

    // Marker 规则产出：竖线事件 / 时间区间带 / 值区间带。
    struct MarkerMark
    {
        enum class Kind { Event, Region, ValueBand } kind = Kind::Event;
        enum class Severity { Info, Warning, Error, Critical } severity = Severity::Info;
        double tStart = 0.0;
        double tEnd = 0.0;
        double valueLow = 0.0;
        double valueHigh = 0.0;
        bool hasValue = false;
        QString label;
        QColor color; // 无效则用 severity 色
    };
    void setMarkers(const QVector<MarkerMark> &marks);
    int markerCount() const { return m_markers.size(); }

    // 状态视图：每条通道一行色带，
    // 每段是一个连续不变的状态（label 为着色键，见 StateColors.h）。
    // 绘制在绘图区顶部（每行 22px + 6px 间距），行名画在段带上方。
    struct StateBandMark
    {
        double tStart = 0.0;
        double tEnd = 0.0;
        QString label; // 状态标签（bool→"true"/"false"、枚举→十进制串、string 原文）
        QColor color;  // 无效则用 stateColor(label)
        bool operator==(const StateBandMark &o) const
        { return tStart == o.tStart && tEnd == o.tEnd && label == o.label && color == o.color; }
    };
    struct StateRowMark
    {
        QString channel; // 行名（"table.column"）
        QVector<StateBandMark> segments;
        bool operator==(const StateRowMark &o) const
        { return channel == o.channel && segments == o.segments; }
    };
    void setStateBands(const QVector<StateRowMark> &rows);
    void clearStateBands() { setStateBands({}); }
    int stateRowCount() const { return m_stateRows.size(); }

    // 像素/数据互转 + 命中：交互部件据此判断点到了哪条线/书签。
    double pixelToX(double px) const;
    double xToPixel(double x) const;

    // 图例：显示各曲线的名字 + 色块。
    // 图例顶部让位：图例块从 plotRect.top()+inset 起排（默认 0）。
    // 画布上方压着悬浮工具栏时，由上层设一个大于工具栏高度的值，图例就不会被按钮压住。
    void setLegendTopInset(qreal px) { m_legendTopInset = std::max<qreal>(0.0, px); }
    qreal legendTopInset() const { return m_legendTopInset; }

    void setLegendVisible(bool visible);
    bool legendVisible() const { return m_legend; }

    // === 图例交互（第二梯队 #205，
    // 点击行切显隐（隐藏行灰显仍占位）、标题栏可折叠、Ctrl+滚轮改字号。
    // 命中检测基于最近一次 paint 缓存的行矩形（resize/paint 后自动更新）。
    int legendHitTest(const QPointF &pixel) const; // >=0 曲线行(m_curves 索引)；-1 标题栏；-2 未命中
    bool legendCollapsed() const { return m_legendCollapsed; }
    void setLegendCollapsed(bool collapsed);
    double legendFontScale() const { return m_legendFontScale; }
    void zoomLegendFont(int steps); // Ctrl+滚轮步进：缩放因子 clamp [0.7, 2.0]
    void toggleLegendItem(int curveIndex);

    // 图例行被点击切换显隐（PlotCanvas 同步 PlotSeries.visible，pushToPlot 重建不丢）。
Q_SIGNALS:
    void legendItemToggled(int curveIndex, bool visible);
    void legendCollapseChanged(bool collapsed);
    void legendTitleClicked();

public:
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

    QSize minimumSizeHint() const override { return QSize(240, 160); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    Axis *axisPtr(AxisSide side);
    void paintCurves(QPainter &painter);
    void paintOverlay(QPainter &painter);
    void paintLegend(QPainter &painter);
    void paintCursorReadout(QPainter &painter, const QRectF &plot);
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
    bool m_legend = false;
    bool m_legendCollapsed = false;   // 图例折叠（只显示标题栏）
    double m_legendFontScale = 1.0;   // 图例字号缩放因子（Ctrl+滚轮）
    struct LegendRow
    {
        int curveIndex = -1;
        QRectF rect;
    };
    QVector<LegendRow> m_legendRows;  // paintLegend 缓存的行矩形（命中检测）
    QRectF m_legendTitleRect;         // 标题栏矩形（折叠按钮命中）
    // 图例整块矩形（标题栏 + 所有行）：右上角贴边绘制，读数框据此避让，
    // 不能再用"固定左上 + 估算行数下移"的老办法（图例换到右边后那条规则失效）。
    QRectF m_legendBlockRect;
    qreal m_legendTopInset = 0.0;     // 图例顶部让位（悬浮工具栏占位）
    bool m_grid = false;
    bool m_box = false;            // 画布边框（对齐老界面显示组）
    QRect m_rubberBand;
    QPointF m_hoverPoint;
    bool m_hoverActive = false;
    double m_playheadX = 0.0;
    bool m_playheadActive = false;
    double m_linkedX = 0.0;
    bool m_linkedActive = false;
    // 读数叠加层状态（只存"画什么"，不存"是什么数据"）。
    double m_cursorX = 0.0;
    bool m_cursorActive = false;
    bool m_cursorPinned = false;
    QString m_cursorHeader;
    QVector<ReadoutRow> m_cursorRows;
    bool m_abEnabled = false;
    double m_abA = 0.0;
    double m_abB = 0.0;
    bool m_abHasB = false;
    QString m_abHeader;
    QVector<ReadoutRow> m_abRows;
    bool m_abTriangle = false;
    double m_abAy = 0.0;
    double m_abBy = 0.0;
    QVector<BookmarkMark> m_bookmarks;
    QVector<MarkerMark> m_markers;
    QVector<StateRowMark> m_stateRows;
    QString m_overlayText;
    QColor m_titleColor{0x33, 0x33, 0x33};
    QString m_title;
    QPixmap m_staticCache;
    bool m_staticDirty = true;
    QVector<Line> m_curves;
    // 画在坐标系里的 artist 图元。bar/hist/spectrum 创建的实例由本 Axes 以父子所有权持有；
    // addArtist 挂载的外部实例不拥有，clearArtists 只清空不 delete。
    QVector<Artist *> m_artists;
};

// 兼容别名：PlotWidget 即 Axes。
using PlotWidget = Axes;

} // namespace nextpilot::plotscope::fastplot
