#include "fastplot/Panner.h"

#include "fastplot/Axes.h"
#include "fastplot/Viewport.h"

#include <QEvent>
#include <QMouseEvent>

namespace nextpilot::plotscope::fastplot
{

Panner::Panner(Axes *plot, QObject *parent)
    : QObject(parent)
    , m_plot(plot)
{
    if (m_plot)
        m_plot->installEventFilter(this);
}

bool Panner::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_plot || !m_plot)
        return QObject::eventFilter(watched, event);

    switch (event->type())
    {
    case QEvent::MouseButtonPress:
    {
        auto *m = static_cast<QMouseEvent *>(event);
        if (!m_enabled || m->button() != Qt::LeftButton)
            return false;
        if (!m_plot->plotRect().contains(m->pos()))
            return false;
        m_dragging = true;
        m_lastPos = m->pos();
        m_plot->setCursor(Qt::ClosedHandCursor);
        return true;
    }
    case QEvent::MouseMove:
    {
        auto *m = static_cast<QMouseEvent *>(event);
        if (!m_dragging)
            return false;
        const Viewport &vp = m_plot->viewport();
        const QRectF plot = vp.plotRect();
        // 拖动方向与内容方向一致（抓住数据拖）：鼠标向右 → 数据向左移。
        const double dx = (m->pos().x() - m_lastPos.x()) / plot.width() * (vp.xUpper() - vp.xLower());
        const double dy = (m->pos().y() - m_lastPos.y()) / plot.height() * (vp.yUpper() - vp.yLower());
        m_plot->setXRange(vp.xLower() - dx, vp.xUpper() - dx);
        // y 向上：鼠标向下（dy>0）→ 数据窗口上移。
        m_plot->setYRange(vp.yLower() + dy, vp.yUpper() + dy);
        m_lastPos = m->pos();
        Q_EMIT panned();
        return true;
    }
    case QEvent::MouseButtonRelease:
    {
        if (!m_dragging)
            return false;
        m_dragging = false;
        m_plot->unsetCursor();
        return true;
    }
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

} // namespace nextpilot::plotscope::fastplot
