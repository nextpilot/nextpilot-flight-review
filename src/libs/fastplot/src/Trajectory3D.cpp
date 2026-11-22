#include "fastplot/Trajectory3D.h"
#include "fastplot/PlotData.h"

#include <QFontMetrics>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace nextpilot::plotscope::fastplot
{

Trajectory3D::Trajectory3D(QWidget *parent)
    : Instrument(parent)
{
}

void Trajectory3D::setTracks(const IPlotData *x, const IPlotData *y, const IPlotData *z,
                             const QString &xLabel, const QString &yLabel, const QString &zLabel)
{
    m_x = x;
    m_y = y;
    m_z = z;
    m_xLabel = xLabel;
    m_yLabel = yLabel;
    m_zLabel = zLabel;
    update();
}

void Trajectory3D::clearTracks()
{
    m_x = m_y = m_z = nullptr;
    update();
}

void Trajectory3D::setCurveColor(const QColor &color)
{
    m_curve = color;
    update();
}

void Trajectory3D::paintInstrument(QPainter &painter, const QRectF &rect)
{
    const QFontMetrics fm(font());
    const int n = (m_x && m_y) ? static_cast<int>(std::min(m_x->size(), m_y->size())) : 0;
    if (n < 2)
    {
        painter.setPen(m_ink);
        painter.drawText(rect, Qt::AlignCenter, QStringLiteral("3D 轨迹：至少两条通道（X/Y）"));
        return;
    }

    // 第一遍：中心 + 跨度（轨迹在哪个位置、多大，全部未知，先量出来）
    double cx = 0.0, cy = 0.0, cz = 0.0;
    int cnt = 0;
    for (int i = 0; i < n; ++i)
    {
        const double x = m_x->yAt(i);
        const double y = m_y->yAt(i);
        const double z = (m_z && m_z->size() == m_x->size()) ? m_z->yAt(i) : 0.0;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            continue;
        cx += x; cy += y; cz += z; ++cnt;
    }
    if (!cnt)
        return;
    cx /= cnt; cy /= cnt; cz /= cnt;

    double span = 1e-9;
    for (int i = 0; i < n; ++i)
    {
        const double x = m_x->yAt(i);
        const double y = m_y->yAt(i);
        const double z = (m_z && m_z->size() == m_x->size()) ? m_z->yAt(i) : 0.0;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            continue;
        span = std::max(span, std::max(std::fabs(x - cx), std::max(std::fabs(y - cy), std::fabs(z - cz))));
    }

    const double scale = std::min(rect.width(), rect.height()) * 0.62 / span / 2.0;
    const auto prj = [&](double x, double y, double z) {
        const double dx = (x - cx) * scale;
        const double dy = (y - cy) * scale;
        const double dz = (z - cz) * scale;
        return QPointF(rect.center().x() + (dx - dy) * 0.72,
                       rect.center().y() + dz * 0.9 - (dx + dy) * 0.22);
    };

    // 参考立方体：只画三条主棱，够看出"这是三维"就行
    painter.setPen(QPen(m_ink, 1.0));
    const QPointF o = prj(cx - span, cy - span, cz - span);
    painter.drawLine(o, prj(cx + span, cy - span, cz - span));
    painter.drawLine(o, prj(cx - span, cy + span, cz - span));
    painter.drawLine(o, prj(cx - span, cy - span, cz + span));

    QPolygonF poly;
    poly.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        const double x = m_x->yAt(i);
        const double y = m_y->yAt(i);
        const double z = (m_z && m_z->size() == m_x->size()) ? m_z->yAt(i) : 0.0;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            continue;
        poly.push_back(prj(x, y, z));
    }
    if (poly.size() < 2)
        return;

    painter.save();
    painter.setClipRect(rect);
    painter.setPen(QPen(m_curve, 1.6));
    painter.drawPolyline(poly);
    painter.setBrush(m_curve);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(poly.first(), 3.0, 3.0);
    painter.restore();

    painter.setPen(m_ink);
    painter.drawText(QPointF(rect.left(), rect.top() + fm.ascent()),
                     QStringLiteral("X %1  Y %2  Z %3")
                         .arg(m_xLabel.isEmpty() ? QStringLiteral("x") : m_xLabel,
                              m_yLabel.isEmpty() ? QStringLiteral("y") : m_yLabel,
                              (m_z && !m_zLabel.isEmpty()) ? m_zLabel : QStringLiteral("-")));
}

} // namespace nextpilot::plotscope::fastplot
