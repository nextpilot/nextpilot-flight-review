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
    QString title() const { return m_title; }
    void setTitleColor(const QColor &color);

    void setGridVisible(bool visible);
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
    // 右上角读数文本（空串 = 不画）。
    void setOverlayText(const QString &text);
    void clearOverlay();

    // 图例：显示各曲线的名字 + 色块。
    void setLegendVisible(bool visible);
    bool legendVisible() const { return m_legend; }

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
    bool m_grid = false;
    bool m_box = false;            // 画布边框（对齐老界面显示组）
    QRect m_rubberBand;
    QPointF m_hoverPoint;
    bool m_hoverActive = false;
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

// 旧名兼容：老代码里的 PlotWidget 就是 Axes。
using PlotWidget = Axes;

} // namespace nextpilot::plotscope::fastplot
