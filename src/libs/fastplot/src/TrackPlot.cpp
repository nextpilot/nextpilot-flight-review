#include "fastplot/TrackPlot.h"
#include "fastplot/PlotData.h"

#include <QFontMetrics>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace nextpilot::plotscope::fastplot
{

TrackPlot::TrackPlot(QWidget *parent)
    : Instrument(parent)
{
}

void TrackPlot::setTrack(const IPlotData *longitude, const IPlotData *latitude,
                         const QString &lonLabel, const QString &latLabel)
{
    m_lon = longitude;
    m_lat = latitude;
    m_lonLabel = lonLabel;
    m_latLabel = latLabel;
    update();
}

void TrackPlot::clearTrack()
{
    m_lon = nullptr;
    m_lat = nullptr;
    update();
}

void TrackPlot::setStartColor(const QColor &color)
{
    m_start = color;
    update();
}

void TrackPlot::setEndColor(const QColor &color)
{
    m_end = color;
    update();
}

void TrackPlot::paintInstrument(QPainter &painter, const QRectF &rect)
{
    const QFontMetrics fm(font());
    const int n = (m_lon && m_lat) ? static_cast<int>(std::min(m_lon->size(), m_lat->size())) : 0;

    if (n < 2)
    {
        painter.setPen(m_ink);
        painter.drawText(rect, Qt::AlignCenter,
                         QStringLiteral("经纬轨迹需要经度与纬度两条通道"));
        return;
    }

    double minLon = std::numeric_limits<double>::infinity();
    double maxLon = -minLon;
    double minLat = minLon;
    double maxLat = -minLon;
    for (int i = 0; i < n; ++i)
    {
        const double lon = m_lon->yAt(i);
        const double lat = m_lat->yAt(i);
        if (!std::isfinite(lon) || !std::isfinite(lat) || std::fabs(lat) > 90.0 || std::fabs(lon) > 180.0)
            continue;
        minLon = std::min(minLon, lon);
        maxLon = std::max(maxLon, lon);
        minLat = std::min(minLat, lat);
        maxLat = std::max(maxLat, lat);
    }
    if (!std::isfinite(minLon) || minLon == maxLon || minLat == maxLat)
    {
        // 单点/一条线：给一点点跨度，避免除零后整条轨迹塌成一条边。
        minLon -= 1e-5;
        maxLon += 1e-5;
        minLat -= 1e-5;
        maxLat += 1e-5;
    }

    const double padLon = std::max((maxLon - minLon) * 0.08, 1e-6);
    const double padLat = std::max((maxLat - minLat) * 0.08, 1e-6);
    // 左侧 44px 留给纬度标签（原来的 34px 不够四位数带小数），顶部留一行的信息条。
    const QRectF plot = rect.adjusted(44, fm.height() + 6, -6, -fm.height() - 6);
    const double loLon = minLon - padLon;
    const double hiLon = maxLon + padLon;
    const double loLat = minLat - padLat;
    const double hiLat = maxLat + padLat;

    const auto xOf = [&](double lon) {
        return plot.left() + plot.width() * (lon - loLon) / (hiLon - loLon);
    };
    const auto yOf = [&](double lat) {
        return plot.bottom() - plot.height() * (lat - loLat) / (hiLat - loLat);
    };

    // 经纬网格 + 边上的数值（没有底图，网格就是唯一的"地图"参照）。
    // 分几格不写死：按标签实际宽度算，挤不下就少画几格，否则数字互相压。
    const QString probe = QString::number(hiLon, 'f', 4);
    const double labelW = fm.horizontalAdvance(probe);
    // 首尾标签各要占半个标签宽的"贴边余量"，所以可用宽度是 W-labelW；
    // 否则边标签被夹进来后正好压到相邻那个（3 格时就会撞）。
    const double usable = std::max(1.0, plot.width() - labelW);
    // 边标签是"贴边对齐"、中间是"居中"，贴边那个会把中心向左让出半个标签宽，
    // 所以间距必须按 1.5 倍标签宽算，否则第 1、2 个恰好相接。
    const int nDiv = std::clamp(static_cast<int>(usable / (labelW * 1.5 + 10.0)), 1, 4);

    painter.setPen(QPen(m_ink, 1.0));
    for (int i = 0; i <= nDiv; ++i)
    {
        const double x = plot.left() + plot.width() * i / nDiv;
        const double y = plot.top() + plot.height() * i / nDiv;
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        painter.setPen(m_labelInk);
        // 经度标签：首尾夹进绘图区，否则最右那个被裁掉半个数字。
        const QString lonText = QString::number(loLon + (hiLon - loLon) * i / nDiv, 'f', 4);
        const double lonW = fm.horizontalAdvance(lonText);
        // 首尾靠边对齐、中间居中：只做"居中后夹取"会让夹过的首标签压到第二个上。
        const double lonX = (i == 0) ? plot.left()
                           : (i == nDiv) ? plot.right() - lonW
                                         : x - lonW / 2.0;
        painter.drawText(QPointF(lonX, plot.bottom() + fm.ascent() + 2), lonText);
        // 纬度标签右对齐到标签列，并夹进绘图区竖向范围——
        // 否则最上面那个会压到顶部的"xxx 点"信息条上。
        const double labelY = std::clamp(y, plot.top() + fm.height() / 2.0,
                                         plot.bottom() - fm.height() / 2.0);
        painter.drawText(QRectF(plot.left() - 42, labelY - fm.height() / 2.0, 38, fm.height()),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(hiLat - (hiLat - loLat) * i / nDiv, 'f', 4));
        painter.setPen(QPen(m_ink, 1.0));
    }

    QPolygonF poly;
    poly.reserve(n);
    int drawn = 0;
    for (int i = 0; i < n; ++i)
    {
        const double lon = m_lon->yAt(i);
        const double lat = m_lat->yAt(i);
        if (!std::isfinite(lon) || !std::isfinite(lat) || std::fabs(lat) > 90.0 || std::fabs(lon) > 180.0)
            continue;
        poly.push_back(QPointF(xOf(lon), yOf(lat)));
        ++drawn;
    }
    if (drawn < 2)
        return;

    painter.save();
    painter.setClipRect(plot.adjusted(-1, -1, 1, 1));
    painter.setPen(QPen(m_accent, 1.6));
    painter.drawPolyline(poly);
    painter.restore();

    painter.setPen(Qt::NoPen);
    painter.setBrush(m_start);
    painter.drawEllipse(poly.first(), 4.0, 4.0);
    painter.setBrush(m_end);
    painter.drawEllipse(poly.last(), 4.0, 4.0);

    painter.setPen(m_ink);
    const QString labels = (!m_lonLabel.isEmpty() || !m_latLabel.isEmpty())
        ? QStringLiteral(" · %1 / %2").arg(m_latLabel, m_lonLabel)
        : QString();
    painter.drawText(QRectF(rect.left(), rect.top(), rect.width(), fm.height()),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("%1 点%2").arg(drawn).arg(labels));
}

} // namespace nextpilot::plotscope::fastplot
