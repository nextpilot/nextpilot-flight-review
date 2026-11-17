#include "scripting/ScratchPlot.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace ps::scripting
{
namespace
{
const QColor kBg(30, 30, 30);
const QColor kGrid(62, 62, 66);
const QColor kAxis(150, 150, 155);
const QColor kCurve(96, 170, 255);

QString tickText(double v, const QString &unit)
{
    const double a = std::abs(v);
    if (a != 0.0 && (a < 1e-3 || a >= 1e5))
        return QString::number(v, 'g', 3) + (unit.isEmpty() ? QString() : unit);
    QString s = QString::number(v, 'f', a < 10.0 ? 2 : (a < 1000.0 ? 1 : 0));
    return unit.isEmpty() ? s : s + unit;
}
} // namespace

ScratchPlot::ScratchPlot(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(140);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ScratchPlot::setSeries(const QVector<QPointF> &points, const QString &title,
                            const QString &xUnit)
{
    m_points = points;
    m_title = title;
    m_xUnit = xUnit;
    update();
}

void ScratchPlot::clearSeries()
{
    m_points.clear();
    m_title.clear();
    update();
}

void ScratchPlot::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), kBg);

    if (m_points.size() < 2)
    {
        p.setPen(kAxis);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("scratch 图：无数据"));
        return;
    }

    const QRectF box = QRectF(rect()).adjusted(52, 26, -14, -32);
    double xMin = m_points.first().x();
    double xMax = xMin;
    double yMin = m_points.first().y();
    double yMax = yMin;
    for (const QPointF &pt : m_points)
    {
        xMin = std::min(xMin, pt.x());
        xMax = std::max(xMax, pt.x());
        yMin = std::min(yMin, pt.y());
        yMax = std::max(yMax, pt.y());
    }
    if (xMax <= xMin)
        xMax = xMin + 1.0;
    if (yMax <= yMin)
    {
        yMax = yMin + 1.0;
        yMin -= 1.0;
    }

    p.setPen(QPen(kGrid, 1));
    for (int i = 0; i <= 4; ++i)
    {
        const double fx = box.left() + box.width() * i / 4.0;
        const double fy = box.top() + box.height() * i / 4.0;
        p.drawLine(QPointF(fx, box.top()), QPointF(fx, box.bottom()));
        p.drawLine(QPointF(box.left(), fy), QPointF(box.right(), fy));
    }
    p.setPen(QPen(kAxis, 1));
    p.drawLine(box.bottomLeft(), box.bottomRight());
    p.drawLine(box.bottomLeft(), box.topLeft());

    // 曲线
    QPolygonF poly;
    poly.reserve(m_points.size());
    for (const QPointF &pt : m_points)
    {
        const double nx = (pt.x() - xMin) / (xMax - xMin);
        const double ny = (pt.y() - yMin) / (yMax - yMin);
        poly << QPointF(box.left() + nx * box.width(), box.bottom() - ny * box.height());
    }
    p.setPen(QPen(kCurve, 1.2));
    p.drawPolyline(poly);

    // 刻度
    p.setPen(kAxis);
    for (int i = 0; i <= 4; ++i)
    {
        const double fx = box.left() + box.width() * i / 4.0;
        const double fy = box.top() + box.height() * i / 4.0;
        const double xv = xMin + (xMax - xMin) * i / 4.0;
        const double yv = yMax - (yMax - yMin) * i / 4.0;
        p.drawText(QRectF(fx - 34, box.bottom() + 4, 68, 16), Qt::AlignHCenter,
                   tickText(xv, m_xUnit));
        p.drawText(QRectF(0, fy - 8, 46, 16), Qt::AlignRight, tickText(yv, QString()));
    }

    if (!m_title.isEmpty())
    {
        p.setPen(QColor(220, 220, 225));
        p.drawText(QRectF(box.left(), 4, box.width(), 18), Qt::AlignLeft, m_title);
    }
}

} // namespace ps::scripting
