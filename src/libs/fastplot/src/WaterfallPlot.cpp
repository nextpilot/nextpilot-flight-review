#include "fastplot/WaterfallPlot.h"
#include "fastplot/PlotData.h"

#include <QFontMetrics>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace nextpilot::plotscope::fastplot
{

WaterfallPlot::WaterfallPlot(QWidget *parent)
    : Instrument(parent)
{
}

void WaterfallPlot::setSeries(const QVector<const IPlotData *> &series, const QVector<QColor> &colors)
{
    m_series = series;
    m_colors = colors;
    update();
}

void WaterfallPlot::clearSeries()
{
    m_series.clear();
    m_colors.clear();
    update();
}

void WaterfallPlot::paintInstrument(QPainter &painter, const QRectF &rect)
{
    const QFontMetrics fm(font());
    const int count = m_series.size();
    if (count < 1)
    {
        painter.setPen(m_ink);
        painter.drawText(rect, Qt::AlignCenter, QStringLiteral("瀑布图需要一条或多条可见曲线"));
        return;
    }

    // 值域与横轴范围：所有序列共用一套，否则各画各的错位
    double lo = std::numeric_limits<double>::infinity();
    double hi = -lo;
    double xLo = std::numeric_limits<double>::infinity();
    double xHi = -xLo;
    bool any = false;
    for (const IPlotData *s : m_series)
    {
        if (!s || s->size() < 2)
            continue;
        for (qsizetype i = 0; i < s->size(); ++i)
        {
            const double v = s->yAt(i);
            const double x = s->xAt(i);
            if (!std::isfinite(v) || !std::isfinite(x))
                continue;
            any = true;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            xLo = std::min(xLo, x);
            xHi = std::max(xHi, x);
        }
    }
    if (!any)
        return;

    const double pad = (hi - lo) * 0.1 + 1e-9;
    lo -= pad;
    hi += pad;
    if (xHi - xLo < 1e-12)
        xHi = xLo + 1.0;

    const double zStep = std::min(10.0, rect.height() * 0.4 / std::max(1, count));
    const double w = rect.width() * 0.72;
    const double h = rect.height() * 0.55;
    const double x0 = rect.left() + 4;
    const double yBase = rect.bottom() - 8;

    painter.setPen(QPen(m_ink, 1.0));
    painter.drawLine(QPointF(x0, yBase), QPointF(x0 + w, yBase));
    painter.drawLine(QPointF(x0, yBase), QPointF(x0, yBase - h));

    // 从远到近画：后画的压在前面的之上，才像一叠
    for (int k = count - 1; k >= 0; --k)
    {
        const IPlotData *s = m_series.at(k);
        if (!s || s->size() < 2)
            continue;
        const double z = (count - 1 - k) * zStep * 0.5;
        QPolygonF poly;
        poly.reserve(s->size());
        for (qsizetype i = 0; i < s->size(); ++i)
        {
            const double v = s->yAt(i);
            const double x = s->xAt(i);
            if (!std::isfinite(v) || !std::isfinite(x))
                continue;
            const double t = (x - xLo) / (xHi - xLo);
            poly.push_back(QPointF(x0 + w * t + z,
                                   yBase - h * (v - lo) / (hi - lo) + z));
        }
        if (poly.size() < 2)
            continue;
        QColor c = (k < m_colors.size()) ? m_colors.at(k) : m_accent;
        c.setAlpha(110 + 145 * (k + 1) / std::max(1, count));
        painter.setPen(QPen(c, 1.2));
        painter.drawPolyline(poly);
    }

    painter.setPen(m_labelInk);
    painter.drawText(QPointF(rect.left(), rect.top() + fm.ascent()),
                     QStringLiteral("瀑布 · %1 条").arg(count));
}

} // namespace nextpilot::plotscope::fastplot
