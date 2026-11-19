#pragma once

#include <QObject>
#include <QPointF>
#include <QRect>
#include <QRectF>

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;

namespace nextpilot::plotscope::fastplot
{

class Axes;

// 缩放部件：滚轮（以光标为锚点）、右键/中键框选、键盘（+ / - / 0 / 方向键）。
//
// 交互状态机全部在这里；画布只提供 setXRange/setYRange/setRubberBand，
// 不在 paintEvent 里做任何判断——这条纪律保证交互可以整体替换而不动绘制。
class Zoomer : public QObject
{
    Q_OBJECT
public:
    explicit Zoomer(Axes *plot, QObject *parent = nullptr);

    // 复位基准（"回到全量视图"）。构造时取当前视口，之后可显式设置。
    void setHomeRange(const QRectF &dataRange);
    QRectF homeRange() const { return m_home; }

    // 以像素点 anchor 为锚点缩放：factor<1 放大，>1 缩小。
    void zoomBy(double factor, const QPointF &anchorPixel);
    // 只缩放某一个轴（Shift=仅 x，Ctrl=仅 y，供滚轮使用）。
    void zoomBy(double factorX, double factorY, const QPointF &anchorPixel);
    // 框选缩放：把像素矩形放大到铺满绘图区。
    void zoomToPixels(const QRect &pixels);
    void resetZoom();

Q_SIGNALS:
    void zoomed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Axes *m_plot = nullptr;
    QRectF m_home;
    bool m_banding = false;
    QPoint m_press;
};

} // namespace nextpilot::plotscope::fastplot
