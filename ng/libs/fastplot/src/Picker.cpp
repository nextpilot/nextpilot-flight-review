#include "fastplot/Picker.h"

#include "fastplot/Axes.h"

#include <QEvent>
#include <QMouseEvent>

namespace nextpilot::plotscope::fastplot
{

Picker::Picker(Axes *plot, QObject *parent)
    : QObject(parent)
    , m_plot(plot)
{
    if (m_plot)
    {
        m_plot->setMouseTracking(true);
        m_plot->installEventFilter(this);
    }
}

void Picker::clear()
{
    if (!m_plot)
        return;
    m_plot->setHover(QPointF(), false);
    m_plot->setOverlayText(QString());
    Q_EMIT hovered(0.0, 0.0, false);
}

bool Picker::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_plot || !m_plot)
        return QObject::eventFilter(watched, event);

    if (event->type() == QEvent::MouseMove)
    {
        auto *m = static_cast<QMouseEvent *>(event);
        // 拖动（按键按下）时不做读数：拖动手势属于 Panner/Zoomer。
        if (m->buttons() != Qt::NoButton)
            return false;
        if (!m_plot->plotRect().contains(m->pos()))
        {
            clear();
            return false;
        }

        double x = 0.0;
        double y = 0.0;
        const bool found = m_plot->nearestSample(QPointF(m->pos()), &x, &y);
        m_plot->setHover(QPointF(x, y), found);
        Q_EMIT hovered(x, y, found);
        return false; // 读数不吞事件，其它部件照常收到
    }

    if (event->type() == QEvent::Leave)
    {
        clear();
        return false;
    }

    return QObject::eventFilter(watched, event);
}

} // namespace nextpilot::plotscope::fastplot
