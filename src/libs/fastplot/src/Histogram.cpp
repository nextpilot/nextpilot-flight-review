#include "fastplot/Histogram.h"

#include <QPainter>
#include <cmath>
#include <limits>

namespace nextpilot::plotscope::fastplot
{

Histogram::Histogram(QWidget *parent) : Instrument(parent)
{
    m_color = accent();
    setMinimumSize(200, 140);
}

void Histogram::setData(const IPlotData *data)
{
    m_data = data;
    update();
}

void Histogram::setBins(int bins)
{
    m_bins = std::clamp(bins, 4, 512);
    update();
}

void Histogram::setColor(const QColor &color)
{
    m_color = color;
    update();
}

void Histogram::rebuild() const
{
    m_counts.clear();
    m_maxCount = 0;
    if (!m_data || m_data->size() <= 0)
        return;

    const quint64 rev = m_data->revision();
    const qsizetype n = m_data->size();
    if (rev == m_revision && n == m_n && !m_counts.isEmpty())
        return;

    const double *px = nullptr;
    const double *py = nullptr;
    const bool direct = m_data->directAccess(&px, &py);

    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    QVector<double> values;
    values.reserve(n);
    for (qsizetype i = 0; i < n; ++i)
    {
        const double y = direct ? py[i] : m_data->yAt(i);
        if (!std::isfinite(y))
            continue;
        values.append(y);
        lo = std::min(lo, y);
        hi = std::max(hi, y);
    }
    if (values.isEmpty())
        return;

    const double span = std::max(1e-9, hi - lo);
    m_counts.fill(0, m_bins);
    for (const double y : values)
    {
        const int bin = std::clamp(int((y - lo) / span * m_bins), 0, m_bins - 1);
        ++m_counts[bin];
        m_maxCount = std::max(m_maxCount, m_counts[bin]);
    }
    m_lo = lo;
    m_hi = hi;
    m_revision = rev;
    m_n = n;
}

void Histogram::paintInRect(QPainter &painter, const QRectF &rect) const
{
rebuild();
    if (m_counts.isEmpty() || m_maxCount <= 0)
        return;

    // 底部留值域标注。
    const double labelH = rect.height() * 0.16;
    const QRectF plot(rect.left(), rect.top(), rect.width(), rect.height() - labelH);
    const double slot = plot.width() / m_bins;

    painter.setPen(Qt::NoPen);
    for (int i = 0; i < m_bins; ++i)
    {
        const double h = plot.height() * m_counts.at(i) / m_maxCount;
        painter.fillRect(QRectF(plot.left() + slot * i + 1, plot.bottom() - h, slot - 2, h),
                         m_color);
    }

    painter.setPen(labelInk());
    painter.drawText(QRectF(plot.left(), plot.bottom() + 2, plot.width() * 0.5, labelH - 2),
                     Qt::AlignLeft | Qt::AlignVCenter, QString::number(m_lo, 'g', 4));
    painter.drawText(QRectF(plot.right() - plot.width() * 0.5, plot.bottom() + 2,
                            plot.width() * 0.5, labelH - 2),
                     Qt::AlignRight | Qt::AlignVCenter, QString::number(m_hi, 'g', 4));
}

void Histogram::paintInstrument(QPainter &painter, const QRectF &rect)
{
    paintInRect(painter, rect);
}

} // namespace nextpilot::plotscope::fastplot
