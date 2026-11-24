#include "fastplot/CrosshairOverlay.h"
#include "fastplot/SemanticColors.h"

#include <algorithm>

namespace nextpilot::plotscope::fastplot
{

void CrosshairOverlay::paint(QPainter &painter, const OverlayContext &ctx)
{
    const QRectF &plot = ctx.plotRect;
    const Viewport &vp = *ctx.vp;

    if (!m_overlayText.isEmpty())
    {
        painter.save();
        QFont f = ctx.font;
        f.setPointSizeF(std::max(7.0, f.pointSizeF()));
        painter.setFont(f);
        const QFontMetrics fm(f);
        const int pad = 6;
        const QRectF textRect(plot.right() - fm.horizontalAdvance(m_overlayText) - 2 * pad,
                              plot.top() + pad,
                              fm.horizontalAdvance(m_overlayText) + 2 * pad,
                              fm.height() + pad);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 40));
        painter.drawRoundedRect(textRect, 3, 3);
        painter.setPen(ctx.labelColor);
        painter.drawText(textRect, Qt::AlignCenter, m_overlayText);
        painter.restore();
    }

    if (m_hoverActive && plot.contains(vp.toPixel(m_hoverPoint.x(), m_hoverPoint.y())))
    {
        const QPointF px = vp.toPixel(m_hoverPoint.x(), m_hoverPoint.y());
        painter.save();
        painter.setClipRect(plot);
        QPen pen(QColor(0x88, 0x88, 0x88, 0xC0), 1.0, Qt::DashLine);
        painter.setPen(pen);
        painter.drawLine(QPointF(px.x(), plot.top()), QPointF(px.x(), plot.bottom()));
        painter.drawLine(QPointF(plot.left(), px.y()), QPointF(plot.right(), px.y()));
        painter.setBrush(QColor(0x1F, 0x6F, 0xC0));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(px, 3.0, 3.0);
        painter.restore();
    }

    // 播放头竖线：跨整幅绘图区的醒目橙红竖线。
    // 用数据 x 映射像素，落在可见范围外则不画（随播放推进自然进/出视野）。
    if (m_playheadActive)
    {
        const QPointF px = vp.toPixel(m_playheadX, vp.yLower());
        if (px.x() >= plot.left() && px.x() <= plot.right())
        {
            painter.save();
            painter.setClipRect(plot);
            painter.setPen(QPen(semanticColor(QStringLiteral("plot.cursor"),
                                              QColor(QStringLiteral("#f2b134"))),
                                 1.6, Qt::SolidLine));
            painter.drawLine(QPointF(px.x(), plot.top()), QPointF(px.x(), plot.bottom()));
            // 顶部小三角，便于在密集曲线中定位。
            QPolygonF head;
            head << QPointF(px.x() - 4, plot.top()) << QPointF(px.x() + 4, plot.top())
                 << QPointF(px.x(), plot.top() + 6);
            painter.setBrush(semanticColor(QStringLiteral("plot.cursor"),
                                           QColor(QStringLiteral("#f2b134"))));
            painter.setPen(Qt::NoPen);
            painter.drawPolygon(head);
            painter.restore();
        }
    }

    // 联动游标竖线（同页其它画布同步的时间，只画竖线，对齐老 setLinkedCursor）。
    if (m_linkedActive)
    {
        const double lx = vp.toPixel(m_linkedX, vp.yLower()).x();
        if (lx >= plot.left() && lx <= plot.right())
        {
            painter.save();
            painter.setClipRect(plot);
            painter.setPen(QPen(QColor(0x5b, 0xc0, 0xf0, 0xB0), 1.0, Qt::DashLine));
            painter.drawLine(QPointF(lx, plot.top()), QPointF(lx, plot.bottom()));
            painter.restore();
        }
    }
}

void CrosshairOverlay::paintRubberBand(QPainter &painter, const OverlayContext &ctx)
{
    if (m_rubberBand.isNull())
        return;
    painter.save();
    painter.setPen(QPen(ctx.axisInk, 1.0));
    painter.setBrush(QColor(0x1F, 0x6F, 0xC0, 40));
    painter.drawRect(m_rubberBand);
    painter.restore();
}

} // namespace nextpilot::plotscope::fastplot
