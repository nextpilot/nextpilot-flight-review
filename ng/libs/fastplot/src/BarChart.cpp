#include "fastplot/BarChart.h"

#include <QPainter>
#include <QtMath>

namespace fastplot
{

BarChart::BarChart(QWidget *parent) : Instrument(parent)
{
    setMinimumSize(200, 140);
}

void BarChart::setBars(const QStringList &labels, const QVector<double> &values,
                       const QVector<QColor> &colors)
{
    m_labels = labels;
    m_values = values;
    m_colors = colors;
    update();
}

void BarChart::setValueRange(double lo, double hi)
{
    m_rangeSet = true;
    m_lo = lo;
    m_hi = hi;
    update();
}

void BarChart::clearValueRange()
{
    m_rangeSet = false;
    update();
}

void BarChart::setBarGap(double ratio)
{
    m_gap = qBound(0.0, ratio, 0.9);
    update();
}

void BarChart::paintInstrument(QPainter &painter, const QRectF &rect)
{
    const int n = m_values.size();
    if (n == 0)
        return;

    double lo = m_lo;
    double hi = m_hi;
    if (!m_rangeSet)
    {
        lo = 0.0;
        hi = 0.0;
        for (const double v : m_values)
        {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        if (hi - lo < 1e-9)
            hi = lo + 1.0;
    }
    const double span = std::max(1e-9, hi - lo);

    // 底部留出类别标签的高度。
    const double labelH = rect.height() * 0.18;
    const QRectF plot(rect.left(), rect.top(), rect.width(), rect.height() - labelH);
    const double baseline = plot.bottom() - (0.0 - lo) / span * plot.height();

    painter.setPen(Qt::NoPen);
    const double slot = plot.width() / n;
    const double barW = slot * (1.0 - m_gap);

    for (int i = 0; i < n; ++i)
    {
        const double v = m_values.at(i);
        const double yTop = plot.bottom() - (v - lo) / span * plot.height();
        const QColor color = m_colors.isEmpty()
            ? accent()
            : m_colors.at(i % m_colors.size());
        painter.fillRect(QRectF(plot.left() + slot * i + (slot - barW) / 2.0,
                                std::min(yTop, baseline), barW,
                                std::abs(baseline - yTop)),
                         color);

        // 类别标签（底部，居中，超长省略）。
        painter.setPen(labelInk());
        const QString label = i < m_labels.size() ? m_labels.at(i) : QString::number(i);
        const QRectF labelRect(plot.left() + slot * i, plot.bottom() + 2, slot, labelH - 2);
        painter.drawText(labelRect, Qt::AlignCenter | Qt::TextWordWrap, label);
    }

    // 基线。
    painter.setPen(QPen(ink(), 1.0));
    painter.drawLine(QPointF(plot.left(), baseline), QPointF(plot.right(), baseline));
}

} // namespace fastplot
