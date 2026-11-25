#include "fastplot/ABMeasureOverlay.h"

#include <algorithm>

namespace nextpilot::plotscope::fastplot
{

void ABMeasureOverlay::set(bool enabled, double a, double b, bool hasB, const QString &header,
                           const QVector<ReadoutRow> &rows, bool drawTriangle, double ay, double by)
{
    m_enabled = enabled;
    m_a = a;
    m_b = b;
    m_hasB = hasB;
    m_header = header;
    m_rows = rows;
    m_triangle = drawTriangle;
    m_ay = ay;
    m_by = by;
}

void ABMeasureOverlay::paint(QPainter &painter, const OverlayContext &ctx)
{
    if (!m_enabled)
        return;
    const QRectF &plot = ctx.plotRect;
    const Viewport &vp = *ctx.vp;
    // A 游标主色走语义 token（验收 8）：未注入时回落历史实色 #f2b134，
    // 透明装饰色由主色派生，注入后一起跟随，未注入时 RGB 与历史值一致、外观不变。
    const QColor accent =
        ctx.semanticColor(QStringLiteral("plot.abMeasure"), QColor(QStringLiteral("#f2b134")));
    auto tint = [](const QColor &c, int alpha) {
        QColor r = c;
        r.setAlpha(alpha);
        return r;
    };
    const QColor bColor(QStringLiteral("#5bc0f0"));
    painter.save();
    painter.setClipRect(plot);
    QFont f = ctx.font;
    f.setPointSizeF(std::max(7.0, f.pointSizeF()));
    painter.setFont(f);
    const QFontMetrics fm(f);

    const double xa = vp.toPixel(m_a, vp.yLower()).x();
    if (xa < plot.left() || xa > plot.right())
    {
        painter.restore();
        return;
    }
    if (!m_hasB)
    {
        const QString hint = QStringLiteral("点击图表设置游标 A，再点一下设置 B");
        const QRectF hr(plot.left() + 10, plot.top() + 4, fm.horizontalAdvance(hint) + 20, 20);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0x14, 0x1a, 0x20, 220));
        painter.drawRoundedRect(hr, 3, 3);
        painter.setPen(accent);
        painter.drawText(hr.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, hint);
        painter.restore();
        return;
    }

    const double xb = vp.toPixel(m_b, vp.yLower()).x();
    // 区间带（可能 A 在 B 右侧）。
    painter.setPen(Qt::NoPen);
    painter.setBrush(tint(accent, 24));
    painter.drawRect(QRectF(QPointF(qMin(xa, xb), plot.top()),
                            QPointF(qMax(xa, xb), plot.bottom())));

    // A / B 竖线 + 顶部圆点。
    painter.setPen(QPen(accent, 1.4, Qt::DashLine));
    painter.drawLine(QPointF(xa, plot.top()), QPointF(xa, plot.bottom()));
    painter.setBrush(accent);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(xa, plot.top() + 1), 3, 3);
    if (xb >= plot.left() && xb <= plot.right())
    {
        painter.setPen(QPen(bColor, 1.4, Qt::DashLine));
        painter.drawLine(QPointF(xb, plot.top()), QPointF(xb, plot.bottom()));
        painter.setBrush(bColor);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(xb, plot.top() + 1), 3, 3);
    }

    const QColor textColor = ctx.labelColor;
    // ΔX 徽标（居中于 A/B，夹在绘图区内）。
    const int tw = fm.horizontalAdvance(m_header) + 20;
    QRectF badge((xa + xb) * 0.5 - tw * 0.5, plot.top() + 5, tw, 20);
    if (badge.left() < plot.left() + 4) badge.moveLeft(plot.left() + 4);
    if (badge.right() > plot.right() - 4) badge.moveRight(plot.right() - 4);
    painter.setPen(Qt::NoPen);
    painter.setBrush(tint(accent, 235));
    painter.drawRoundedRect(badge, 3, 3);
    painter.setPen(QColor(0x10, 0x16, 0x1c));
    painter.drawText(badge, Qt::AlignCenter, m_header);

    // 直角三角形：首条可见曲线的 Δy 一眼可见（y 值由上层换算成像素 y 传入）。
    if (m_triangle && m_ay >= plot.top() && m_ay <= plot.bottom())
    {
        painter.setPen(QPen(tint(accent, 150), 1, Qt::DashLine));
        painter.drawLine(QPointF(xa, m_ay), QPointF(xb, m_ay));
        painter.drawLine(QPointF(xb, m_ay), QPointF(xb, m_by));
        painter.drawLine(QPointF(xa, m_ay), QPointF(xb, m_by));
    }

    // 逐曲线 Δy / k 读数框（贴底部）。
    if (!m_rows.isEmpty())
    {
        const int lineH = fm.height() + 2;
        const int boxH = 40 + qMin(m_rows.size(), 6) * lineH + 6;
        QRectF box(plot.left() + 10, plot.bottom() - boxH - 8, qMin(int(plot.width() - 20), 340), boxH);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0x14, 0x1a, 0x20, 238));
        painter.drawRoundedRect(box, 3, 3);
        painter.setPen(accent);
        painter.drawText(QRectF(box.left() + 8, box.top() + 3, box.width() - 16, lineH),
                         Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("A→B  Δx / 各曲线 Δy 与斜率"));
        painter.setPen(QColor(0x7a, 0x84, 0x8f));
        painter.drawLine(QPointF(box.left() + 7, box.top() + 21), QPointF(box.right() - 7, box.top() + 21));
        int y = int(box.top()) + 23;
        for (int i = 0; i < m_rows.size() && i < 6; ++i)
        {
            const ReadoutRow &r = m_rows.at(i);
            painter.setPen(r.color);
            painter.drawText(QRectF(box.left() + 8, y, box.width() * 0.45, lineH),
                             Qt::AlignVCenter | Qt::AlignLeft,
                             fm.elidedText(r.label, Qt::ElideMiddle, int(box.width() * 0.45)));
            painter.setPen(textColor);
            painter.drawText(QRectF(box.left() + box.width() * 0.45, y, box.width() * 0.53, lineH),
                             Qt::AlignVCenter | Qt::AlignRight, r.value);
            y += lineH;
        }
    }
    painter.restore();
}

} // namespace nextpilot::plotscope::fastplot
