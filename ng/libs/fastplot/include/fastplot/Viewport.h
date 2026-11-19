#pragma once

#include <QPointF>
#include <QRectF>

namespace nextpilot::plotscope::fastplot
{

// 视口：世界坐标（数据）与像素坐标（画布）的双向映射。
//
// 约定：
//   - x 向右增大，y 向上增大（数学坐标系），所以像素 y 与数据 y 方向相反；
//   - 所有像素坐标都是 PlotWidget 的局部坐标。
class Viewport
{
public:
    void setPlotRect(const QRectF &rect);
    QRectF plotRect() const { return m_rect; }

    // 数据范围。lo > hi 自动交换；lo == hi 时按 ±1 展开，避免除零。
    void setXRange(double lo, double hi);
    void setYRange(double lo, double hi);

    double xLower() const { return m_xLo; }
    double xUpper() const { return m_xHi; }
    double yLower() const { return m_yLo; }
    double yUpper() const { return m_yHi; }

    double xToPixel(double x) const;
    double yToPixel(double y) const;
    double pixelToX(double px) const;
    double pixelToY(double py) const;

    QPointF toPixel(double x, double y) const { return QPointF(xToPixel(x), yToPixel(y)); }

    // 与另一视口是否等价（图层缓存失效判断用）。
    bool sameAs(const Viewport &other) const;

private:
    QRectF m_rect{0.0, 0.0, 1.0, 1.0};
    double m_xLo = 0.0;
    double m_xHi = 1.0;
    double m_yLo = 0.0;
    double m_yHi = 1.0;
};

} // namespace nextpilot::plotscope::fastplot
