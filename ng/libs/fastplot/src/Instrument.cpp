#include "fastplot/Instrument.h"

#include <QPainter>
#include <QPaintEvent>

namespace fastplot
{

Instrument::Instrument(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(120, 90);
}

void Instrument::setBackground(const QColor &color)
{
    m_background = color;
    update();
}

void Instrument::setInk(const QColor &color)
{
    m_ink = color;
    update();
}

void Instrument::setLabelInk(const QColor &color)
{
    m_labelInk = color;
    update();
}

void Instrument::setAccent(const QColor &color)
{
    m_accent = color;
    update();
}

void Instrument::setMargin(int px)
{
    m_margin = px;
    update();
}

void Instrument::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), m_background);
    painter.setRenderHint(QPainter::Antialiasing, true);
    paintInstrument(painter, QRectF(rect()).adjusted(m_margin, m_margin, -m_margin, -m_margin));
}

} // namespace fastplot
