#pragma once

#include "fastplot/Axis.h"
#include "fastplot/Curve.h"
#include "fastplot/Viewport.h"

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QPointF>
#include <QRect>
#include <QJsonObject>
#include <QString>
#include <QWidget>

namespace fastplot
{

// 画布：背景 + 网格 + 四条轴 + 静态图层缓存。
//
// P1 只画"空坐标系"；曲线在 P2 通过 setCurves 接进来（数据零拷贝）。
// 网格/轴这类"每帧都一样"的内容画进 QPixmap，只有视口/尺寸/配色变化才重画——
// 这是 hover 与拖动不掉帧的前提。
class PlotWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PlotWidget(QWidget *parent = nullptr);
    ~PlotWidget() override;

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
    void replot();

    // ---- 曲线（P2）----
    // 按值移入并返回引用，便于连续调样式；曲线不拥有数据。
    Curve &addCurve(Curve curve);
    void clearCurves();
    int curveCount() const { return m_curves.size(); }

    // 按序取曲线（只读）：图例/自检/导出等"我有哪些曲线"的场景。
    const Curve &curveAt(int index) const { return m_curves.at(index); }

    // 第二 y 轴（右轴）的数据范围。曲线用 Curve::setYAxis(Right) 声明挂到它上面。
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
    QRect m_rubberBand;
    QPointF m_hoverPoint;
    bool m_hoverActive = false;
    QString m_overlayText;
    QColor m_titleColor{0x33, 0x33, 0x33};
    QString m_title;
    QPixmap m_staticCache;
    bool m_staticDirty = true;
    QVector<Curve> m_curves;
};

} // namespace fastplot
