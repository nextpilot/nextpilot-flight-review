#include "fastplot/Axis.h"
#include "fastplot/Viewport.h"

#include <QFontMetrics>
#include <QPainter>
#include <QRect>

#include <algorithm>
#include <cmath>

namespace nextpilot::plotscope::fastplot
{

Axis::Axis(AxisSide side)
    : m_side(side)
{
}

void Axis::setYRange(double lo, double hi)
{
    if (lo > hi)
        std::swap(lo, hi);
    if (lo == hi)
        hi = lo + 1.0;
    m_yLo = lo;
    m_yHi = hi;
    m_hasYRange = true;
}

double Axis::yToPixel(const Viewport &vp, double value) const
{
    // 轴只负责"我的范围怎么映射到我的长度"，与曲线、画布都无关。
    if (!m_hasYRange)
        return vp.yToPixel(value);
    const QRectF plot = vp.plotRect();
    return plot.bottom() - (value - m_yLo) / (m_yHi - m_yLo) * plot.height();
}

int Axis::preferredMargin() const
{
    if (!m_visible)
        return 0;
    const QFontMetrics fm(m_font);
    switch (m_side)
    {
    case AxisSide::Left:
    case AxisSide::Right:
    {
        int widest = fm.horizontalAdvance(QStringLiteral("-0.000"));
        for (const Tick &t : m_div.ticks)
        {
            if (t.major)
                widest = std::max(widest, fm.horizontalAdvance(t.label));
        }
        return widest + m_tickLength + 8;
    }
    case AxisSide::Bottom:
    case AxisSide::Top:
        // 标签基线再下移 tickLength+ascent+3，最底一行还要留 descent，
        // 所以 margin 必须覆盖 ascent+descent 整行（fm.height()）。
        return fm.height() + m_tickLength + 10;
    }
    return 0;
}

void Axis::paintGrid(QPainter *painter, const Viewport &vp) const
{
    if (!m_visible || !m_grid)
        return;
    const QRectF plot = vp.plotRect();
    painter->save();
    // 网格用虚线（对齐 PJ4 的观感）：实线网格在曲线密集时会和曲线抢视线。
    // 4:3 的短虚线在 1px 笔宽下既看得出是虚线，又不至于稀疏得像刻度。
    QPen gridPen(m_gridInk, 1.0);
    gridPen.setStyle(Qt::CustomDashLine);
    gridPen.setDashPattern({4.0, 3.0});
    painter->setPen(gridPen);
    for (const Tick &t : m_div.ticks)
    {
        if (!t.major)
            continue;
        switch (m_side)
        {
        case AxisSide::Left:
        case AxisSide::Right:
        {
            const double py = yToPixel(vp, t.value);
            painter->drawLine(QPointF(plot.left(), py), QPointF(plot.right(), py));
            break;
        }
        case AxisSide::Bottom:
        case AxisSide::Top:
        {
            const double px = vp.xToPixel(t.value);
            painter->drawLine(QPointF(px, plot.top()), QPointF(px, plot.bottom()));
            break;
        }
        }
    }
    painter->restore();
}

void Axis::paint(QPainter *painter, const Viewport &vp, const QRect &widgetRect) const
{
    if (!m_visible)
        return;
    const QRectF plot = vp.plotRect();
    const QFontMetrics fm(m_font);

    painter->save();
    painter->setFont(m_font);
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(QPen(m_ink, 1.0));

    // 轴线本体：贴着绘图区边沿画一条，明确"这里是轴"。
    switch (m_side)
    {
    case AxisSide::Left:
        painter->drawLine(QPointF(plot.left(), plot.top()), QPointF(plot.left(), plot.bottom()));
        break;
    case AxisSide::Right:
        painter->drawLine(QPointF(plot.right(), plot.top()), QPointF(plot.right(), plot.bottom()));
        break;
    case AxisSide::Bottom:
        painter->drawLine(QPointF(plot.left(), plot.bottom()), QPointF(plot.right(), plot.bottom()));
        break;
    case AxisSide::Top:
        painter->drawLine(QPointF(plot.left(), plot.top()), QPointF(plot.right(), plot.top()));
        break;
    }

    for (const Tick &t : m_div.ticks)
    {
        if (!t.major && m_minorPerMajor <= 0)
            continue;
        const int len = t.major ? m_tickLength : std::max(2, m_tickLength / 2);

        switch (m_side)
        {
        case AxisSide::Left:
        case AxisSide::Right:
        {
            const double py = yToPixel(vp, t.value);
            const double baseX = (m_side == AxisSide::Left) ? plot.left() : plot.right();
            const double dir = (m_side == AxisSide::Left) ? -1.0 : 1.0;
            painter->drawLine(QPointF(baseX, py), QPointF(baseX + dir * len, py));
            if (t.major)
            {
                // 标签右对齐到刻度线外侧，留 4px 间隙。
                const int textW = fm.horizontalAdvance(t.label);
                double textX = (m_side == AxisSide::Left)
                    ? baseX - len - 4 - textW
                    : baseX + len + 4;
                // 竖向标签夹在控件内：宁可贴边，也不要被裁掉半个字。
                textX = std::clamp(textX, double(widgetRect.left()),
                                   std::max(double(widgetRect.left()),
                                            double(widgetRect.right()) - textW));
                painter->setPen(QPen(m_labelColor, 1.0));
                painter->drawText(QPointF(textX, py + fm.ascent() / 2.0 - 1.0), t.label);
                painter->setPen(QPen(m_ink, 1.0));
            }
            break;
        }
        case AxisSide::Bottom:
        case AxisSide::Top:
        {
            const double px = vp.xToPixel(t.value);
            const double baseY = (m_side == AxisSide::Bottom) ? plot.bottom() : plot.top();
            const double dir = (m_side == AxisSide::Bottom) ? 1.0 : -1.0;
            painter->drawLine(QPointF(px, baseY), QPointF(px, baseY + dir * len));
            if (t.major)
            {
                const int textW = fm.horizontalAdvance(t.label);
                double textX = px - textW / 2.0;
                // 首尾刻度夹进绘图区：贴着画布边沿的标签不能被裁掉半截。
                textX = std::clamp(textX, plot.left(),
                                   std::max(plot.left(), plot.right() - textW));
                const double textY = (m_side == AxisSide::Bottom)
                    ? baseY + len + fm.ascent() + 3
                    : baseY - len - 4;
                painter->setPen(QPen(m_labelColor, 1.0));
                painter->drawText(QPointF(textX, textY), t.label);
                painter->setPen(QPen(m_ink, 1.0));
            }
            break;
        }
        }
    }

    // 轴标题：画在刻度标签一侧的中点。用 labelColor 区分于刻度值。
    if (!m_label.isEmpty())
    {
        painter->setPen(QPen(m_labelColor, 1.0));
        const QFontMetrics lfm(font());
        const QPointF center = plot.center();
        if (m_side == AxisSide::Left || m_side == AxisSide::Right)
        {
            const double x = (m_side == AxisSide::Left)
                ? widgetRect.left() + 2
                : widgetRect.right() - lfm.horizontalAdvance(m_label) - 2;
            painter->drawText(QPointF(x, center.y() - lfm.height() / 2.0 + lfm.ascent()), m_label);
        }
        else
        {
            const double x = std::clamp(center.x() - lfm.horizontalAdvance(m_label) / 2.0,
                                        plot.left(), std::max(plot.left(), plot.right() - lfm.horizontalAdvance(m_label)));
            painter->drawText(QPointF(x, widgetRect.bottom() - 2), m_label);
        }
    }

    painter->restore();
}

} // namespace nextpilot::plotscope::fastplot
