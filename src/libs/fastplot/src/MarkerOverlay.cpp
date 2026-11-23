#include "fastplot/MarkerOverlay.h"

#include <algorithm>

namespace nextpilot::plotscope::fastplot
{

void MarkerOverlay::paint(QPainter &painter, const OverlayContext &ctx)
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
    const auto sevColor = [](MarkerMark::Severity s) -> QColor {
        switch (s)
        {
        case MarkerMark::Severity::Warning:  return QColor(0xf2, 0xb1, 0x34);
        case MarkerMark::Severity::Error:    return QColor(0xe5, 0x4c, 0x3a);
        case MarkerMark::Severity::Critical: return QColor(0xd1, 0x2b, 0x2b);
        case MarkerMark::Severity::Info:
        default:                             return QColor(0x3d, 0x8f, 0xd6);
        }
    };
    int lane = 0;
    for (const MarkerMark &mk : m_marks)
    {
        const QColor col = mk.color.isValid() ? mk.color : sevColor(mk.severity);
        if (mk.kind == MarkerMark::Kind::ValueBand)
        {
            // 值带：横跨绘图区的半透明水平带 + 左端 label。
            const double yp1 = vp.yToPixel(mk.valueLow);
            const double yp2 = vp.yToPixel(mk.valueHigh);
            const double top = std::min(yp1, yp2), bottom = std::max(yp1, yp2);
            if (bottom < plot.top() || top > plot.bottom())
                continue;
            const QRectF band(plot.left(), top, plot.width(), std::max(2.0, bottom - top));
            painter.fillRect(band, QColor(col.red(), col.green(), col.blue(), 30));
            painter.setPen(QPen(col, 1, Qt::DashLine));
            painter.drawLine(QPointF(plot.left(), top), QPointF(plot.right(), top));
            painter.drawLine(QPointF(plot.left(), bottom), QPointF(plot.right(), bottom));
            if (!mk.label.isEmpty())
            {
                painter.setPen(col);
                painter.drawText(QPointF(plot.left() + 4, std::max(plot.top() + 11.0, top - 3.0)),
                                 fm.elidedText(mk.label, Qt::ElideRight, 160));
            }
            continue;
        }

        const double x = vp.toPixel(mk.tStart, vp.yLower()).x();
        if (x < plot.left() || x > plot.right())
            continue;
        if (mk.kind == MarkerMark::Kind::Region)
        {
            const double x2 = vp.toPixel(mk.tEnd, vp.yLower()).x();
            const double left = std::max(plot.left(), x), right = std::min(plot.right(), x2);
            painter.fillRect(QRectF(left, plot.top(), std::max(0.0, right - left), plot.height()),
                             QColor(col.red(), col.green(), col.blue(), 24));
            painter.setPen(QPen(col, 1, Qt::DashLine));
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            painter.drawLine(QPointF(x2, plot.top()), QPointF(x2, plot.bottom()));
        }
        else // Event：竖线（hasValue 时在值处画点）
        {
            painter.setPen(QPen(col, mk.hasValue ? 2 : 1, Qt::SolidLine));
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            if (mk.hasValue)
            {
                const double yp = vp.yToPixel(mk.valueLow);
                painter.setPen(Qt::NoPen);
                painter.setBrush(col);
                painter.drawEllipse(QPointF(x, yp), 4.0, 4.0);
            }
        }
        if (!mk.label.isEmpty())
        {
            QRectF chip(x + 3, plot.top() + 2 + (lane % 3) * 15,
                        fm.horizontalAdvance(mk.label) + 8, 14);
            if (chip.right() > plot.right()) chip.moveLeft(x - 3 - chip.width());
            if (chip.left() < plot.left()) chip.moveLeft(plot.left() + 2);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0x14, 0x1a, 0x20, 220));
            painter.drawRoundedRect(chip, 2, 2);
            painter.setPen(col);
            painter.drawText(chip.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                             fm.elidedText(mk.label, Qt::ElideRight, 160));
        }
        ++lane;
    }
    painter.restore();
}

} // namespace nextpilot::plotscope::fastplot
