#include "fastplot/DialGauge.h"

#include <QFontMetrics>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace fastplot
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
}

DialGauge::DialGauge(QWidget *parent)
    : Instrument(parent)
{
}

void DialGauge::setValue(double value)
{
    m_value = value;
    m_hasValue = std::isfinite(value);
    update();
}

void DialGauge::setValueMissing()
{
    m_hasValue = false;
    update();
}

void DialGauge::setRange(double lo, double hi)
{
    if (lo > hi)
        std::swap(lo, hi);
    if (hi - lo < 1e-12)
        hi = lo + 1.0;
    m_lo = lo;
    m_hi = hi;
    update();
}

void DialGauge::setTitle(const QString &title)
{
    m_title = title;
    update();
}

void DialGauge::paintInstrument(QPainter &painter, const QRectF &rect)
{
    const QFontMetrics fm(font());
    const double textRow = fm.height() + 2.0;
    const double radius = std::min(rect.width(), rect.height() - textRow) * 0.5;
    if (radius <= 6.0)
        return;
    const QPointF c(rect.center().x(), rect.top() + radius);

    const QRectF arcRect(c.x() - radius, c.y() - radius, radius * 2, radius * 2);
    painter.setPen(QPen(m_ink, 2.0));
    painter.drawArc(arcRect, 225 * 16, -270 * 16);

    const double frac = m_hasValue ? std::clamp((m_value - m_lo) / (m_hi - m_lo), 0.0, 1.0) : 0.0;
    painter.setPen(QPen(m_accent, 4.0));
    painter.drawArc(arcRect, 225 * 16, static_cast<int>(-270.0 * frac * 16));

    const double a = (225.0 - 270.0 * frac) * kPi / 180.0;
    painter.setPen(QPen(m_accent, 2.0));
    painter.drawLine(c, c + QPointF(std::cos(a) * radius * 0.86, -std::sin(a) * radius * 0.86));

    painter.setPen(m_labelInk);
    // 数值压在圆心下方一点：贴着圆心会和指针根部撞在一起。
    painter.drawText(QRectF(c.x() - rect.width() / 2.0, c.y() + radius * 0.34, rect.width(), fm.height()),
                     Qt::AlignCenter,
                     m_hasValue ? QString::number(m_value, 'g', 5) : QStringLiteral("--"));
    // 量程标签放在表盘下方，超出的部分由 rect 的高度兜住（半径已经扣掉这一行）。
    painter.drawText(QRectF(rect.left(), c.y() + radius + 4, rect.width(), fm.height()),
                     Qt::AlignCenter,
                     m_title.isEmpty()
                         ? QStringLiteral("%1 ~ %2").arg(m_lo, 0, 'g', 3).arg(m_hi, 0, 'g', 3)
                         : QStringLiteral("%1  %2 ~ %3").arg(m_title).arg(m_lo, 0, 'g', 3).arg(m_hi, 0, 'g', 3));
}

} // namespace fastplot
