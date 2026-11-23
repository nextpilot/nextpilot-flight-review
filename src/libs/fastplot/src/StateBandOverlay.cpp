#include "fastplot/StateBandOverlay.h"

#include "fastplot/StateColors.h"

#include <algorithm>

namespace nextpilot::plotscope::fastplot
{

void StateBandOverlay::paint(QPainter &painter, const OverlayContext &ctx)
{
    if (m_rows.isEmpty())
        return;
    const QRectF &plot = ctx.plotRect;
    const Viewport &vp = *ctx.vp;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    QFont f = ctx.font;
    f.setPointSizeF(std::max(7.0, f.pointSizeF()));
    painter.setFont(f);
    const QFontMetrics fm(f);
    constexpr double kBandH = 22.0;
    constexpr double kGap = 6.0;
    constexpr double kMinLabelPx = 24.0; // 段宽低于此不画状态名
    constexpr double kNameW = 90.0;      // 行名固定宽（超出省略）
    const double bandLeft = plot.left();

    double y = plot.top() - m_rows.size() * (kBandH + kGap) - 4.0;
    for (const StateRowMark &row : m_rows)
    {
        const double bandY = y;
        const double segLeft = bandLeft + kNameW + 4.0;
        // 行名画在带内左侧。
        painter.setPen(ctx.axisInk);
        painter.drawText(QRectF(bandLeft, bandY, kNameW, kBandH),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         fm.elidedText(row.channel, Qt::ElideMiddle, static_cast<int>(kNameW)));

        for (const StateBandMark &seg : row.segments)
        {
            const double x0 = vp.toPixel(seg.tStart, vp.yLower()).x();
            const double x1 = vp.toPixel(seg.tEnd, vp.yLower()).x();
            const double left = std::max(segLeft, x0);
            const double right = std::min(plot.right(), x1);
            if (right - left < 1.0)
                continue; // 视口外或被行名遮住
            const QColor col = seg.color.isValid() ? seg.color : stateColor(seg.label);
            const QRectF rect(left, bandY, right - left, kBandH);
            QColor fill = col;
            fill.setAlphaF(0.82); // 中饱和：段内黑/白字都可读
            painter.fillRect(rect, fill);
            painter.setPen(QPen(QColor(0, 0, 0, 90), 1.0));
            painter.drawRect(rect.adjusted(0, 0, -1, -1));
            if (rect.width() > kMinLabelPx && !seg.label.isEmpty())
            {
                // 按背景亮度选黑/白字。
                const int brightness = (col.red() * 299 + col.green() * 587 + col.blue() * 114) / 1000;
                painter.setPen(brightness > 140 ? Qt::black : Qt::white);
                painter.drawText(rect, Qt::AlignCenter,
                                 fm.elidedText(seg.label, Qt::ElideRight,
                                               static_cast<int>(rect.width() - 4)));
            }
        }
        y += kBandH + kGap;
    }
    painter.restore();
}

} // namespace nextpilot::plotscope::fastplot
