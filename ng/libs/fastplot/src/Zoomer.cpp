#include "fastplot/Zoomer.h"

#include "fastplot/Axes.h"
#include "fastplot/Viewport.h"

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace nextpilot::plotscope::fastplot
{

Zoomer::Zoomer(Axes *plot, QObject *parent)
    : QObject(parent)
    , m_plot(plot)
{
    if (m_plot)
    {
        m_plot->setFocusPolicy(Qt::StrongFocus);
        m_plot->installEventFilter(this);
        // 复位基准 = 接手时的视口（"回到全量视图"）。
        const Viewport &vp = m_plot->viewport();
        m_home = QRectF(QPointF(vp.xLower(), vp.yLower()), QPointF(vp.xUpper(), vp.yUpper()));
    }
}

void Zoomer::setHomeRange(const QRectF &dataRange)
{
    m_home = dataRange;
}

void Zoomer::resetZoom()
{
    if (!m_plot || m_home.isNull())
        return;
    m_plot->setXRange(m_home.left(), m_home.right());
    m_plot->setYRange(m_home.top(), m_home.bottom());
    Q_EMIT zoomed();
}

void Zoomer::zoomBy(double factor, const QPointF &anchorPixel)
{
    zoomBy(factor, factor, anchorPixel);
}

void Zoomer::zoomBy(double factorX, double factorY, const QPointF &anchorPixel)
{
    if (!m_plot)
        return;
    const Viewport &vp = m_plot->viewport();
    const double ax = vp.pixelToX(anchorPixel.x());
    const double ay = vp.pixelToY(anchorPixel.y());

    // 锚点不动：缩放前后 anchor 的数据坐标与像素位置都不变。
    m_plot->setXRange(ax - (ax - vp.xLower()) * factorX, ax + (vp.xUpper() - ax) * factorX);
    m_plot->setYRange(ay - (ay - vp.yLower()) * factorY, ay + (vp.yUpper() - ay) * factorY);
    Q_EMIT zoomed();
}

void Zoomer::zoomToPixels(const QRect &pixels)
{
    if (!m_plot)
        return;
    const QRect r = pixels.normalized();
    if (r.width() < 4 || r.height() < 4)
        return;
    const Viewport &vp = m_plot->viewport();
    const double x0 = vp.pixelToX(r.left());
    const double x1 = vp.pixelToX(r.right());
    // y 向上：顶边是上限，底边是下限。
    const double y0 = vp.pixelToY(r.bottom());
    const double y1 = vp.pixelToY(r.top());
    m_plot->setRubberBand(QRect());
    m_plot->setXRange(x0, x1);
    m_plot->setYRange(y0, y1);
    Q_EMIT zoomed();
}

bool Zoomer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_plot || !m_plot)
        return QObject::eventFilter(watched, event);

    switch (event->type())
    {
    case QEvent::Wheel:
    {
        auto *w = static_cast<QWheelEvent *>(event);
        // Alt+滚轮保留给图例字号（#205），吞掉但不做视图缩放。
        if (w->modifiers() & Qt::AltModifier)
            return true;
        // 滚轮一格 120；上滚放大。
        const double factor = std::pow(1.2, -w->angleDelta().y() / 120.0);
        const bool xOnly = w->modifiers() & Qt::ShiftModifier;
        const bool yOnly = w->modifiers() & Qt::ControlModifier;
        if (xOnly)
            zoomBy(factor, 1.0, w->position());
        else if (yOnly)
            zoomBy(1.0, factor, w->position());
        else
            zoomBy(factor, w->position());
        return true;
    }
    case QEvent::MouseButtonPress:
    {
        auto *m = static_cast<QMouseEvent *>(event);
        // 右键 / 中键框选；左键在「放大/缩小工具」模式下也框选（m_leftBand）。
        const bool bandButton = m->button() == Qt::RightButton
                             || m->button() == Qt::MiddleButton
                             || (m_leftBand && m->button() == Qt::LeftButton);
        if (bandButton)
        {
            if (!m_plot->plotRect().contains(m->pos()))
                return false;
            m_banding = true;
            m_press = m->pos();
            m_plot->setRubberBand(QRect(m_press, m_press));
            return true;
        }
        return false;
    }
    case QEvent::MouseMove:
    {
        auto *m = static_cast<QMouseEvent *>(event);
        if (!m_banding)
            return false;
        m_plot->setRubberBand(QRect(m_press, m->pos()).normalized());
        return true;
    }
    case QEvent::MouseButtonRelease:
    {
        auto *m = static_cast<QMouseEvent *>(event);
        if (!m_banding)
            return false;
        m_banding = false;
        const QRect band(m_press, m->pos());
        m_plot->setRubberBand(QRect());
        if (m_bandZoomOut)
        {
            // 缩小工具：以框选中心为锚点缩小固定倍数（框越大缩得越多）。
            const QRect r = band.normalized();
            const double span = qMax(r.width(), 1);
            const double factor = qBound(1.2, 1.0 + span / 120.0, 4.0);
            const QPointF center = r.center();
            zoomBy(factor, center);
        }
        else
        {
            zoomToPixels(band);
        }
        return true;
    }
    case QEvent::KeyPress:
    {
        auto *k = static_cast<QKeyEvent *>(event);
        const QPointF center(m_plot->plotRect().center());
        const double panStep = 0.1;
        const Viewport &vp = m_plot->viewport();
        const double dx = (vp.xUpper() - vp.xLower()) * panStep;
        const double dy = (vp.yUpper() - vp.yLower()) * panStep;
        switch (k->key())
        {
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            zoomBy(1.0 / 1.25, center);
            return true;
        case Qt::Key_Minus:
            zoomBy(1.25, center);
            return true;
        case Qt::Key_0:
        case Qt::Key_Home:
            resetZoom();
            return true;
        case Qt::Key_Left:
            m_plot->setXRange(vp.xLower() - dx, vp.xUpper() - dx);
            Q_EMIT zoomed();
            return true;
        case Qt::Key_Right:
            m_plot->setXRange(vp.xLower() + dx, vp.xUpper() + dx);
            Q_EMIT zoomed();
            return true;
        case Qt::Key_Up:
            m_plot->setYRange(vp.yLower() + dy, vp.yUpper() + dy);
            Q_EMIT zoomed();
            return true;
        case Qt::Key_Down:
            m_plot->setYRange(vp.yLower() - dy, vp.yUpper() - dy);
            Q_EMIT zoomed();
            return true;
        default:
            return false;
        }
    }
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

} // namespace nextpilot::plotscope::fastplot
