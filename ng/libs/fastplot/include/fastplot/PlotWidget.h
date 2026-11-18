#pragma once

#include "fastplot/Axis.h"
#include "fastplot/Curve.h"
#include "fastplot/Viewport.h"

#include <QColor>
#include <QImage>
#include <QPixmap>
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

    // 按曲线数据自动定范围（两端各留 marginRatio 的余量）；无数据返回 false。
    bool autoScaleY(double marginRatio = 0.05);
    bool autoScaleX(double marginRatio = 0.02);
    bool autoScale();   // 两轴一起

    QRectF plotRect() const { return m_vp.plotRect(); }

    // 离屏渲染，导出与测试用。
    QImage grabImage();

    QSize minimumSizeHint() const override { return QSize(240, 160); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    Axis *axisPtr(AxisSide side);
    void paintCurves(QPainter &painter);
    QRectF dataBounds() const;
    void relayout();       // 边距 → 绘图区 → 刻度划分（三者必须一起算）
    void refreshScaleDivs();
    void invalidateStatic();

    Viewport m_vp;
    Axis m_axes[4];        // 索引 = AxisSide
    QColor m_background{0x10, 0x16, 0x1c};
    QColor m_titleColor{0x33, 0x33, 0x33};
    QString m_title;
    QPixmap m_staticCache;
    bool m_staticDirty = true;
    QVector<Curve> m_curves;
};

} // namespace fastplot
