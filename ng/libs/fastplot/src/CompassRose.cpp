#include "fastplot/CompassRose.h"

#include <QFontMetrics>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace nextpilot::plotscope::fastplot
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
}

CompassRose::CompassRose(QWidget *parent)
    : Instrument(parent)
{
}

void CompassRose::setHeading(double headingDeg)
{
    // 归一化到 [0,360)：航向是圆周量，360° 和 0° 是同一个方向。
    double h = std::fmod(headingDeg, 360.0);
    if (h < 0.0)
        h += 360.0;
    m_heading = h;
    update();
}

void CompassRose::paintInstrument(QPainter &painter, const QRectF &rect)
{
    const QFontMetrics fm(font());
    const double textRow = fm.height() + 2.0;
    const double radius = std::min(rect.width(), rect.height() - textRow) * 0.5;
    if (radius <= 6.0)
        return;
    const QPointF c(rect.center().x(), rect.top() + radius);

    painter.setPen(QPen(m_ink, 1.0));
    painter.drawEllipse(c, radius, radius);

    // 每 30° 一个刻度，正北那个更长更亮
    for (int i = 0; i < 12; ++i)
    {
        const double a = i * 30.0 * kPi / 180.0;
        const bool north = (i == 0);
        const double inner = radius - (north ? 12.0 : 6.0);
        painter.setPen(QPen(north ? m_accent : m_ink, north ? 1.6 : 1.0));
        painter.drawLine(c + QPointF(std::sin(a) * inner, -std::cos(a) * inner),
                         c + QPointF(std::sin(a) * radius, -std::cos(a) * radius));
    }

    // 指针：航向从正北顺时针量，屏幕上就是 sin/cos 直接映射
    const double a = m_heading * kPi / 180.0;
    painter.setPen(QPen(m_accent, 2.2));
    painter.drawLine(c, c + QPointF(std::sin(a) * radius * 0.72, -std::cos(a) * radius * 0.72));
    // 尾部短一截，让指向一眼可辨
    painter.setPen(QPen(m_ink, 1.6));
    painter.drawLine(c, c - QPointF(std::sin(a) * radius * 0.22, -std::cos(a) * radius * 0.22));

    painter.setPen(m_labelInk);
    painter.drawText(QRectF(rect.left(), c.y() + radius + 4, rect.width(), fm.height()),
                     Qt::AlignCenter, QStringLiteral("HDG %1°").arg(m_heading, 0, 'f', 1));
}

} // namespace nextpilot::plotscope::fastplot
