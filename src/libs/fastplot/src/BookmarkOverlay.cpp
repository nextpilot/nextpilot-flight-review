#include "fastplot/BookmarkOverlay.h"

#include <algorithm>

namespace nextpilot::plotscope::fastplot
{

void BookmarkOverlay::paint(QPainter &painter, const OverlayContext &ctx)
{
    if (m_marks.isEmpty())
        return;
    const QRectF &plot = ctx.plotRect;
    const Viewport &vp = *ctx.vp;
    painter.save();
    painter.setClipRect(plot);
    QFont f = ctx.font;
    f.setPointSizeF(std::max(7.0, f.pointSizeF()));
    painter.setFont(f);
    const QFontMetrics fm(f);
    const QColor accent(QStringLiteral("#f2b134"));
    int lane = 0;
    for (const BookmarkMark &mk : m_marks)
    {
        const double x = vp.toPixel(mk.t, vp.yLower()).x();
        if (x < plot.left() || x > plot.right())
            continue;
        painter.setPen(QPen(QColor(0xf2, 0xb1, 0x34, 150), 1, Qt::DashLine));
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        const QString text = fm.elidedText(mk.label, Qt::ElideRight, 180);
        const int tw = fm.horizontalAdvance(text) + 10;
        QRectF chip(x + 3, plot.top() + 2 + (lane % 3) * 17, tw, 16);
        if (chip.right() > plot.right()) chip.moveLeft(x - 3 - tw);
        if (chip.left() < plot.left()) chip.moveLeft(plot.left() + 3);
        painter.setPen(QPen(mk.selected ? accent : QColor(0x4a, 0x52, 0x5c), mk.selected ? 2 : 1));
        painter.setBrush(QColor(0x14, 0x1a, 0x20, mk.selected ? 255 : 235));
        painter.drawRoundedRect(chip, 2, 2);
        painter.setPen(accent);
        painter.drawText(chip.adjusted(5, 0, -5, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
        ++lane;
    }
    painter.restore();
}

} // namespace nextpilot::plotscope::fastplot
