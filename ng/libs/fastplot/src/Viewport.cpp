#include "fastplot/Viewport.h"

#include <algorithm>
#include <cmath>

namespace fastplot
{

namespace
{

// 把 [lo, hi] 规整成合法的数据范围：顺序正确、跨度非零。
void normalize(double &lo, double &hi)
{
    if (lo > hi)
        std::swap(lo, hi);
    if (lo == hi)
    {
        // 单点或空范围：按 ±1 展开，让映射可逆而不是除零。
        const double pad = std::fabs(lo) > 1.0 ? std::fabs(lo) * 0.05 : 1.0;
        lo -= pad;
        hi += pad;
    }
}

} // namespace

void Viewport::setPlotRect(const QRectF &rect)
{
    // 绘图区退化成 0 宽/高时给 1 像素，避免后续映射出现 NaN。
    m_rect = QRectF(rect.x(), rect.y(), std::max(1.0, rect.width()), std::max(1.0, rect.height()));
}

void Viewport::setXRange(double lo, double hi)
{
    normalize(lo, hi);
    m_xLo = lo;
    m_xHi = hi;
}

void Viewport::setYRange(double lo, double hi)
{
    normalize(lo, hi);
    m_yLo = lo;
    m_yHi = hi;
}

double Viewport::xToPixel(double x) const
{
    return m_rect.left() + (x - m_xLo) / (m_xHi - m_xLo) * m_rect.width();
}

double Viewport::yToPixel(double y) const
{
    // y 向上增大：数据上限对应绘图区顶边。
    return m_rect.bottom() - (y - m_yLo) / (m_yHi - m_yLo) * m_rect.height();
}

double Viewport::pixelToX(double px) const
{
    return m_xLo + (px - m_rect.left()) / m_rect.width() * (m_xHi - m_xLo);
}

double Viewport::pixelToY(double py) const
{
    return m_yLo + (m_rect.bottom() - py) / m_rect.height() * (m_yHi - m_yLo);
}

bool Viewport::sameAs(const Viewport &other) const
{
    return m_rect == other.m_rect
        && m_xLo == other.m_xLo && m_xHi == other.m_xHi
        && m_yLo == other.m_yLo && m_yHi == other.m_yHi;
}

} // namespace fastplot
