#include "plot/CanvasWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QWheelEvent>

#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;

QColor colorBg()      { return QColor("#0d1117"); }
QColor colorGrid()    { return QColor("#1c2430"); }
QColor colorAxis()    { return QColor("#6e7681"); }
QColor colorText()    { return QColor("#8b949e"); }
QColor colorAccent()  { return QColor("#4ea1ff"); }
}

CanvasWidget::CanvasWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(240, 140);
    setMouseTracking(true);
    m_title = QStringLiteral("未命名画布");
    static int counter = 0;
    m_seed = ++counter;

    // M0 合成数据：15 Hz 采样、30 s 窗口、三条不同相位/频率的曲线。
    const double hz = 15.0;
    const int n = static_cast<int>(hz * m_t1);
    const struct { const char *name; double freq; double amp; double phase; const char *color; } defs[] = {
        {"att.roll",  0.35, 1.00, 0.0,  "#4ea1ff"},
        {"engine_rpm", 0.12, 0.55, 1.2, "#ffb454"},
        {"battery_v", 0.80, 0.30, 2.4,  "#7ee787"},
    };
    for (const auto &d : defs)
    {
        Curve c;
        c.name = QString::fromLatin1(d.name);
        c.color = QColor(d.color);
        c.v.reserve(n);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / hz;
            const double phase = d.phase + m_seed * 0.9;
            const double noise = 0.02 * std::sin(t * 37.0 + phase);
            c.v.append(d.amp * std::sin(2.0 * kPi * d.freq * (1.0 + 0.12 * m_seed) * t + phase) + noise);
        }
        m_curves.append(c);
    }
}

void CanvasWidget::setTitle(const QString &title)
{
    m_title = title;
    update();
}

QRectF CanvasWidget::plotRect() const
{
    return QRectF(rect()).adjusted(58, 30, -16, -26);
}

void CanvasWidget::resetView()
{
    m_yMin = -1.6;
    m_yMax = 1.6;
    update();
}

void CanvasWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), colorBg());

    const QRectF plot = plotRect();
    if (plot.width() <= 1 || plot.height() <= 1)
        return;

    // 标题 + 采样信息
    p.setPen(colorText());
    QFont f = p.font();
    f.setPixelSize(13);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QPointF(14, 21), m_title);
    f.setBold(false);
    f.setPixelSize(11);
    p.setFont(f);
    p.setPen(QColor("#6e7681"));
    p.drawText(QPointF(14 + p.fontMetrics().horizontalAdvance(m_title) + 12, 21),
               QStringLiteral("15 Hz · %1 通道").arg(m_curves.size()));

    // 网格 + Y 刻度
    p.setPen(QPen(colorGrid(), 1));
    const int yTicks = 5;
    QFontMetrics fm(p.font());
    for (int i = 0; i <= yTicks; ++i)
    {
        const double y = plot.top() + plot.height() * i / yTicks;
        p.setPen(QPen(colorGrid(), 1));
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        const double val = m_yMax - (m_yMax - m_yMin) * i / yTicks;
        p.setPen(colorText());
        const QString lbl = QString::number(val, 'f', 2);
        p.drawText(QPointF(plot.left() - 8 - fm.horizontalAdvance(lbl), y + fm.ascent() / 2 - 1), lbl);
    }

    // X 刻度（时间，秒）
    const int xTicks = 6;
    for (int i = 0; i <= xTicks; ++i)
    {
        const double x = plot.left() + plot.width() * i / xTicks;
        const double t = m_t0 + (m_t1 - m_t0) * i / xTicks;
        p.setPen(QPen(colorGrid(), 1));
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        p.setPen(colorText());
        const QString lbl = QString::number(t, 'f', 1);
        p.drawText(QPointF(x - fm.horizontalAdvance(lbl) / 2, plot.bottom() + fm.ascent() + 6), lbl);
    }

    // 曲线
    p.save();
    p.setClipRect(plot);
    for (const auto &c : m_curves)
    {
        if (c.v.isEmpty())
            continue;
        const double n = static_cast<double>(c.v.size() - 1);
        QPainterPath path;
        for (int i = 0; i < c.v.size(); ++i)
        {
            const double x = plot.left() + plot.width() * (static_cast<double>(i) / n) * (1.0 / 1.0);
            const double y = plot.bottom() - plot.height() * (c.v[i] - m_yMin) / (m_yMax - m_yMin);
            if (i == 0)
                path.moveTo(x, y);
            else
                path.lineTo(x, y);
        }
        p.setPen(QPen(c.color, 1.4));
        p.drawPath(path);
    }
    p.restore();

    // 图例
    double lx = plot.left() + 10;
    const double ly = plot.top() + 14;
    f.setBold(false);
    f.setPixelSize(11);
    p.setFont(f);
    for (const auto &c : m_curves)
    {
        p.setPen(QPen(c.color, 1.2));
        p.drawLine(QPointF(lx, ly - 4), QPointF(lx + 14, ly - 4));
        p.setPen(colorText());
        p.drawText(QPointF(lx + 19, ly), c.name);
        lx += 19 + fm.horizontalAdvance(c.name) + 18;
    }

    // 十字光标：纵线 + 当前时刻标注
    if (m_hover && plot.contains(m_cursor))
    {
        p.setPen(QPen(QColor(255, 255, 255, 60), 1, Qt::DashLine));
        p.drawLine(QPointF(m_cursor.x(), plot.top()), QPointF(m_cursor.x(), plot.bottom()));
        p.drawLine(QPointF(plot.left(), m_cursor.y()), QPointF(plot.right(), m_cursor.y()));

        const double tRatio = (m_cursor.x() - plot.left()) / plot.width();
        const double t = m_t0 + (m_t1 - m_t0) * tRatio;
        const double v = m_yMax - (m_yMax - m_yMin) * (m_cursor.y() - plot.top()) / plot.height();
        const QString tip = QStringLiteral("t %1 s · y %2").arg(t, 0, 'f', 3).arg(v, 0, 'f', 3);
        const int tw = fm.horizontalAdvance(tip) + 14;
        QRectF box(m_cursor.x() + 12, plot.top() + 8, tw, 20);
        if (box.right() > plot.right())
            box.moveLeft(m_cursor.x() - 12 - tw);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(22, 27, 34, 230));
        p.drawRoundedRect(box, 3, 3);
        p.setPen(colorText());
        p.drawText(box.adjusted(7, 0, -7, 0), Qt::AlignVCenter | Qt::AlignLeft, tip);
    }
}

void CanvasWidget::wheelEvent(QWheelEvent *event)
{
    const QRectF plot = plotRect();
    if (!plot.contains(event->position()))
        return;
    const int steps = event->angleDelta().y();
    if (steps == 0)
        return;

    // 以指针所在 Y 为锚点缩放（对应 §8.9“轴滚轮缩 Y 锚指针”）
    const double anchor = m_yMax - (m_yMax - m_yMin) * (event->position().y() - plot.top()) / plot.height();
    const double k = steps > 0 ? 0.85 : 1.18;
    m_yMin = anchor + (m_yMin - anchor) * k;
    m_yMax = anchor + (m_yMax - anchor) * k;
    update();
    event->accept();
}

void CanvasWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = true;
        m_lastPos = event->position();
        emit activated(m_title);
    }
    QWidget::mousePressEvent(event);
}

void CanvasWidget::mouseMoveEvent(QMouseEvent *event)
{
    m_cursor = event->position();
    m_hover = true;
    if (m_dragging)
    {
        const QRectF plot = plotRect();
        const double dy = event->position().y() - m_lastPos.y();
        const double span = m_yMax - m_yMin;
        const double shift = -dy * span / plot.height();
        m_yMin += shift;
        m_yMax += shift;
        m_lastPos = event->position();
    }
    update();
    QWidget::mouseMoveEvent(event);
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = false;
    QWidget::mouseReleaseEvent(event);
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    resetView();
    QWidget::mouseDoubleClickEvent(event);
}

void CanvasWidget::leaveEvent(QEvent *event)
{
    m_hover = false;
    update();
    QWidget::leaveEvent(event);
}
