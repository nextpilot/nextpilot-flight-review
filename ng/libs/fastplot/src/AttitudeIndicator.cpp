#include "fastplot/AttitudeIndicator.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace fastplot
{

AttitudeIndicator::AttitudeIndicator(QWidget *parent)
    : Instrument(parent)
{
}

void AttitudeIndicator::setRollPitch(double rollDeg, double pitchDeg)
{
    m_roll = rollDeg;
    m_pitch = pitchDeg;
    update();
}

void AttitudeIndicator::setSkyColor(const QColor &color)
{
    m_sky = color;
    update();
}

void AttitudeIndicator::setGroundColor(const QColor &color)
{
    m_ground = color;
    update();
}

void AttitudeIndicator::paintInstrument(QPainter &painter, const QRectF &rect)
{
    const QFontMetrics fm(font());
    // 底部要给一行读数留位置，否则字被切掉半行（第一版就切了）。
    const double textRow = fm.height() + 2.0;
    const double radius = std::min(rect.width(), rect.height() - textRow) * 0.5;
    if (radius <= 2.0)
        return;

    const QPointF c(rect.center().x(), rect.top() + radius);

    painter.save();
    painter.translate(c);
    painter.rotate(-m_roll);

    // 关键：先裁成圆，再在圆里填天/地两个半平面。
    // 早期做法是画两个大圆互相错位——那不是地平线，整块圆都被"天"盖住。
    QPainterPath circle;
    circle.addEllipse(QPointF(0.0, 0.0), radius, radius);
    painter.setClipPath(circle);
    painter.setPen(Qt::NoPen);
    const double horizon = -std::clamp(m_pitch, -90.0, 90.0) * (radius / 45.0);
    painter.setBrush(m_sky);
    painter.drawRect(QRectF(-radius, -radius * 2.0 + horizon, radius * 2.0, radius * 2.0));
    painter.setBrush(m_ground);
    painter.drawRect(QRectF(-radius, horizon, radius * 2.0, radius * 2.0));
    painter.setPen(QPen(m_labelInk, 1.2));
    painter.drawLine(QPointF(-radius, horizon), QPointF(radius, horizon));
    painter.restore();

    painter.setPen(QPen(m_accent, 1.6));
    painter.drawEllipse(c, radius, radius);

    // 固定飞机符号（不随滚转转，转的是球）
    painter.setPen(QPen(m_accent, 2.0));
    painter.drawLine(QPointF(c.x() - radius * 0.45, c.y()), QPointF(c.x() + radius * 0.45, c.y()));
    painter.drawLine(QPointF(c.x() - radius * 0.06, c.y()), QPointF(c.x() + radius * 0.06, c.y()));
    painter.drawLine(QPointF(c.x(), c.y() + radius * 0.10), QPointF(c.x(), c.y() + radius * 0.20));

    painter.setPen(m_labelInk);
    painter.drawText(QRectF(rect.left(), c.y() + radius + 4, rect.width(), fm.height()),
                     Qt::AlignCenter,
                     QStringLiteral("roll %1°   pitch %2°")
                         .arg(m_roll, 0, 'f', 1)
                         .arg(m_pitch, 0, 'f', 1));
}

} // namespace fastplot
