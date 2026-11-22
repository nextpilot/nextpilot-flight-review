#include "fastplot/Spectrum.h"

#include <QPainter>
#include <cmath>
#include <limits>

namespace nextpilot::plotscope::fastplot
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
}

Spectrum::Spectrum(QWidget *parent) : Instrument(parent)
{
    m_color = accent();
    setMinimumSize(200, 140);
}

void Spectrum::setData(const IPlotData *data)
{
    m_data = data;
    update();
}

void Spectrum::setSampleDt(double dt)
{
    m_dt = dt;
    update();
}

void Spectrum::setColor(const QColor &color)
{
    m_color = color;
    update();
}

void Spectrum::rebuild() const
{
    m_amp.clear();
    m_ampMax = 0.0;
    if (!m_data || m_data->size() < 4)
        return;

    const quint64 rev = m_data->revision();
    const qsizetype n = m_data->size();
    if (rev == m_revision && n == m_n && !m_amp.isEmpty())
        return;

    const double *px = nullptr;
    const double *py = nullptr;
    const bool direct = m_data->directAccess(&px, &py);

    QVector<double> x(n);
    double mean = 0.0;
    for (qsizetype i = 0; i < n; ++i)
    {
        const double y = direct ? py[i] : m_data->yAt(i);
        x[i] = y;
        mean += y;
    }
    mean /= n;
    for (qsizetype i = 0; i < n; ++i)
        x[i] -= mean;

    const int half = n / 2;
    m_amp.fill(0.0, half);
    for (int k = 0; k < half; ++k)
    {
        double re = 0.0;
        double im = 0.0;
        for (qsizetype i = 0; i < n; ++i)
        {
            const double a = -2.0 * kPi * k * double(i) / double(n);
            re += x.at(i) * std::cos(a);
            im += x.at(i) * std::sin(a);
        }
        m_amp[k] = 2.0 * std::sqrt(re * re + im * im) / double(n);
        m_ampMax = std::max(m_ampMax, m_amp[k]);
    }

    if (m_dt > 1e-12)
        m_fMax = double(half) / std::max(1e-12, double(n) * m_dt);
    else
        m_fMax = 1.0;

    m_revision = rev;
    m_n = n;
}

void Spectrum::paintInRect(QPainter &painter, const QRectF &rect) const
{
rebuild();
    if (m_amp.isEmpty() || m_ampMax <= 0.0)
        return;

    const double labelH = rect.height() * 0.16;
    const QRectF plot(rect.left(), rect.top(), rect.width(), rect.height() - labelH);
    const int half = m_amp.size();

    QPolygonF poly;
    poly.reserve(half);
    for (int k = 0; k < half; ++k)
    {
        const double fx = plot.left() + plot.width() * k / std::max(1, half - 1);
        const double fy = plot.bottom() - plot.height() * m_amp.at(k) / m_ampMax;
        poly.append(QPointF(fx, fy));
    }

    painter.setPen(QPen(m_color, 1.0));
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.drawPolyline(poly);
    painter.setRenderHint(QPainter::Antialiasing, false);

    painter.setPen(labelInk());
    painter.drawText(QRectF(plot.left(), plot.bottom() + 2, plot.width() * 0.5, labelH - 2),
                     Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("0"));
    painter.drawText(QRectF(plot.right() - plot.width() * 0.5, plot.bottom() + 2,
                            plot.width() * 0.5, labelH - 2),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QStringLiteral("%1 Hz").arg(m_fMax, 0, 'g', 3));
}

void Spectrum::paintInstrument(QPainter &painter, const QRectF &rect)
{
    paintInRect(painter, rect);
}

} // namespace nextpilot::plotscope::fastplot
