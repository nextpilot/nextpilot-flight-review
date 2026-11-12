#include "plot/CanvasWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QWheelEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMenu>

#include <algorithm>
#include <cmath>

#include "ps_kernel.h"

namespace {
constexpr double kPi = 3.14159265358979323846;

QColor colorBg()   { return QColor("#10161c"); }
QColor colorGrid() { return QColor("#1c2430"); }
QColor colorText() { return QColor("#8b949e"); }
QColor colorDim()  { return QColor("#6e7681"); }

const QColor kPalette[] = {QColor("#4ea1ff"), QColor("#ffb454"), QColor("#7ee787"),
                           QColor("#d2a8ff"), QColor("#ff7b72"), QColor("#39c5cf")};
}

CanvasWidget::CanvasWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(240, 140);
    setMouseTracking(true);
    setAcceptDrops(true);
    m_title = QStringLiteral("未命名画布");
    m_kernelBacked = false;
    addDemoCurves();
}

void CanvasWidget::addDemoCurves()
{
    static int counter = 0;
    const int seed = ++counter;
    m_series.clear();

    const double hz = 15.0;
    const int n = static_cast<int>(hz * (m_t1 - m_t0));
    const struct { const char *name; const char *unit; double freq; double amp; double phase; } defs[] = {
        {"att.roll", "deg", 0.35, 1.00, 0.0},
        {"engine_rpm", "rpm", 0.12, 0.55, 1.2},
        {"battery_v", "V", 0.80, 0.30, 2.4},
    };
    int idx = 0;
    for (const auto &d : defs)
    {
        Series s;
        s.name = QString::fromLatin1(d.name);
        s.unit = QString::fromLatin1(d.unit);
        s.color = kPalette[idx++ % 6];
        s.tOwn.reserve(n);
        s.vOwn.reserve(n);
        const double phase = d.phase + seed * 0.9;
        for (int i = 0; i < n; ++i)
        {
            const double t = m_t0 + i / hz;
            s.tOwn.append(t);
            s.vOwn.append(d.amp * std::sin(2.0 * kPi * d.freq * (1.0 + 0.12 * seed) * t + phase)
                          + 0.02 * std::sin(t * 37.0 + phase));
        }
        m_series.append(s);
    }
    m_autoY = true;
    resetView();
}

void CanvasWidget::setTitle(const QString &title)
{
    m_title = title;
    update();
}

void CanvasWidget::clearChannels()
{
    m_series.clear();
    m_kernelBacked = false;
    m_t0 = 0.0;
    m_t1 = 30.0;
    m_autoY = true;
    resetView();
}

void CanvasWidget::setChannels(const QVector<const PsChannelInfo *> &channels)
{
    m_series.clear();
    m_kernelBacked = true;
    int idx = 0;
    for (const PsChannelInfo *ch : channels)
    {
        if (!ch || ch->point_count < 2 || !ch->time || !ch->value)
            continue;
        Series s;
        s.name = QString::fromUtf8(ch->name);
        s.unit = QString::fromUtf8(ch->unit ? ch->unit : "");
        s.color = kPalette[idx++ % 6];
        s.t = ch->time;
        s.v = ch->value;
        s.n = ch->point_count;
        m_series.append(s);
    }
    m_autoY = true;
    resetView();
}

void CanvasWidget::addChannel(const PsChannelInfo *ch)
{
    if (!ch || ch->point_count < 2 || !ch->time || !ch->value)
        return;
    for (const Series &s : m_series)
    {
        if (s.name == QString::fromUtf8(ch->name) && s.t == ch->time)
            return; // 同一通道重复拖入不重复加
    }
    Series s;
    s.name = QString::fromUtf8(ch->name);
    s.unit = QString::fromUtf8(ch->unit ? ch->unit : "");
    s.color = kPalette[m_series.size() % 6];
    s.t = ch->time;
    s.v = ch->value;
    s.n = ch->point_count;
    m_series.append(s);
    m_kernelBacked = true;
    m_autoY = true;
    resetView();
    update();
    emit activated(m_title);
}

void CanvasWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-plotscope-channel")))
        event->acceptProposedAction();
}

void CanvasWidget::dropEvent(QDropEvent *event)
{
    const QByteArray raw = event->mimeData()->data(QStringLiteral("application/x-plotscope-channel"));
    if (raw.isEmpty())
        return;
    bool ok = false;
    const qulonglong ptr = raw.toULongLong(&ok);
    if (!ok || !ptr)
        return;
    addChannel(reinterpret_cast<const PsChannelInfo *>(ptr));
    event->acceptProposedAction();
}

QRectF CanvasWidget::plotRect() const
{
    return QRectF(rect()).adjusted(62, 30, -16, -26);
}

void CanvasWidget::ensureTimeRange()
{
    if (m_series.isEmpty())
        return;
    const Series &first = m_series.first();
    if (first.count() < 2)
        return;
    m_t0 = first.time()[0];
    m_t1 = first.time()[first.count() - 1];
    if (!(m_t1 > m_t0))
        m_t1 = m_t0 + 1.0;
}

void CanvasWidget::resetView()
{
    ensureTimeRange();

    // 自动 Y：整段数据的 min/max（LOD 在 Rust 侧算，这里只要 256 个桶）
    if (m_autoY && !m_series.isEmpty())
    {
        bool init = false;
        double lo = 0.0;
        double hi = 0.0;
        for (const Series &s : m_series)
        {
            if (s.count() < 2)
                continue;
            QVector<double> buf(256 * 3);
            const uint32_t got = ps_lod_minmax(s.time(), s.value(), s.count(), m_t0, m_t1, 256, buf.data(), 256);
            for (uint32_t i = 0; i < got; ++i)
            {
                const double mn = buf[i * 3 + 1];
                const double mx = buf[i * 3 + 2];
                if (!std::isfinite(mn) || !std::isfinite(mx))
                    continue;
                if (!init)
                {
                    lo = mn;
                    hi = mx;
                    init = true;
                }
                else
                {
                    lo = std::min(lo, mn);
                    hi = std::max(hi, mx);
                }
            }
        }
        if (init)
        {
            const double pad = (hi - lo) * 0.12 + 1e-6;
            m_yMin = lo - pad;
            m_yMax = hi + pad;
        }
    }
    update();
}

void CanvasWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), colorBg());
    p.setPen(QColor("#1f2831"));
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    const QRectF plot = plotRect();
    if (plot.width() <= 1 || plot.height() <= 1)
        return;

    QFont f = p.font();
    f.setPixelSize(13);
    f.setBold(true);
    p.setFont(f);
    p.setPen(colorText());
    const int titleWidth = p.fontMetrics().horizontalAdvance(m_title);
    p.drawText(QPointF(14, 21), m_title);

    f.setBold(false);
    f.setPixelSize(11);
    p.setFont(f);
    const QFontMetrics fm(p.font());
    p.setPen(colorDim());
    const QString sub = m_series.isEmpty()
                            ? QStringLiteral("空画布 · 拖字段到这里，或「文件 → 打开文件」")
                            : QStringLiteral("%1 通道 · 抽稀在 Rust 侧完成").arg(m_series.size());
    p.drawText(QPointF(14 + titleWidth + 14, 21), sub);

    if (m_showGrid)
    {
    // 网格 + Y 刻度
    const int yTicks = 5;
    for (int i = 0; i <= yTicks; ++i)
    {
        const double y = plot.top() + plot.height() * i / yTicks;
        p.setPen(QPen(colorGrid(), 1));
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        const double val = m_yMax - (m_yMax - m_yMin) * i / yTicks;
        p.setPen(colorText());
        const QString lbl = QString::number(val, 'g', 4);
        p.drawText(QPointF(plot.left() - 8 - fm.horizontalAdvance(lbl), y + fm.ascent() / 2 - 1), lbl);
    }

    // X 刻度（秒）
    const int xTicks = 6;
    for (int i = 0; i <= xTicks; ++i)
    {
        const double x = plot.left() + plot.width() * i / xTicks;
        const double t = m_t0 + (m_t1 - m_t0) * i / xTicks;
        p.setPen(QPen(colorGrid(), 1));
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        p.setPen(colorText());
        const QString lbl = QString::number(t, 'f', t >= 100 ? 0 : 2);
        p.drawText(QPointF(x - fm.horizontalAdvance(lbl) / 2, plot.bottom() + fm.ascent() + 6), lbl);
    }

    }

    // 曲线：每像素一个 min/max 桶，画"包络带 + 中线"
    const uint32_t px = static_cast<uint32_t>(std::max(1.0, plot.width()));
    p.save();
    p.setClipRect(plot);
    p.setRenderHint(QPainter::Antialiasing, false);  // 曲线统一关 AA，整段开一次而不是每条切一次
    for (const Series &s : m_series)
    {
        if (!s.visible || s.count() < 2)
            continue;
        QVector<double> buf(static_cast<int>(px) * 3);
        const uint32_t got = ps_lod_minmax(s.time(), s.value(), s.count(), m_t0, m_t1, px, buf.data(), px);
        if (got < 1)
            continue;

        const auto toX = [&](double t) { return plot.left() + plot.width() * (t - m_t0) / (m_t1 - m_t0); };
        const auto toY = [&](double v) { return plot.bottom() - plot.height() * (v - m_yMin) / (m_yMax - m_yMin); };

        // 单屏只画中线：包络带留给"缩到很近、一像素含大量样本"的场景（M1）
        // 折线一律关 AA 用 drawPolyline：QPainterPath + AA 描边在 Qt 6.2 上是毫米级的
        // 灾难（20 条 × 1500 段实测 365 ms/帧，同样数据 drawPolyline 只要 1~2 ms）。
        QPolygonF poly;
        poly.reserve(static_cast<int>(got));
        for (uint32_t i = 0; i < got; ++i)
            poly.append(QPointF(toX(buf[i * 3]), toY((buf[i * 3 + 1] + buf[i * 3 + 2]) / 2.0)));
        p.setPen(QPen(s.color, 1.0));  // 1.0 走快速 Bresenham；1.3 这类小数宽度在 Qt 6.2 上会掉进宽线轮廓器，慢 7 倍
        p.drawPolyline(poly);
    }
    p.restore();
    p.setRenderHint(QPainter::Antialiasing, true);

    if (m_showLegend)
    {
        // 图例（带单位）：点击切换显隐，右键删除
        double lx = plot.left() + 6;
        const double ly = plot.top() + 14;
        m_legendRects.clear();
        for (int i = 0; i < m_series.size(); ++i)
        {
            const Series &s = m_series.at(i);
            const QString lbl = s.unit.isEmpty() ? s.name : QStringLiteral("%1 [%2]").arg(s.name, s.unit);
            const double w = 19 + fm.horizontalAdvance(lbl) + 12;
            m_legendRects.append(QRectF(lx - 2, ly - 12, w, 18));
            p.setPen(QPen(s.color, 1.2));
            p.drawLine(QPointF(lx, ly - 4), QPointF(lx + 14, ly - 4));
            p.setPen(s.visible ? colorText() : colorDim());
            p.drawText(QPointF(lx + 19, ly), lbl);
            lx += w + 8;
        }

    }

    // 十字光标
    if (m_hover && plot.contains(m_cursor))
    {
        p.setPen(QPen(QColor(255, 255, 255, 60), 1, Qt::DashLine));
        p.drawLine(QPointF(m_cursor.x(), plot.top()), QPointF(m_cursor.x(), plot.bottom()));
        p.drawLine(QPointF(plot.left(), m_cursor.y()), QPointF(plot.right(), m_cursor.y()));

        const double t = m_t0 + (m_t1 - m_t0) * (m_cursor.x() - plot.left()) / plot.width();
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

    // PJ 习惯：滚轮缩时间轴，Ctrl+滚轮缩 Y 轴，锚点都在指针处
    const double factor = std::pow(0.85, steps / 120.0);
    const QPointF pos = event->position();
    if (event->modifiers() & Qt::ControlModifier)
    {
        const double anchor = m_yMax - (m_yMax - m_yMin) * (pos.y() - plot.top()) / plot.height();
        m_yMin = anchor + (m_yMin - anchor) * factor;
        m_yMax = anchor + (m_yMax - anchor) * factor;
    }
    else
    {
        const double tc = m_t0 + (m_t1 - m_t0) * (pos.x() - plot.left()) / plot.width();
        const double span = std::clamp((m_t1 - m_t0) * factor, 1e-6, 1e9);
        const double ratio = span / std::max(1e-12, m_t1 - m_t0);
        m_t0 = tc - (tc - m_t0) * ratio;
        m_t1 = m_t0 + span;
        emit timeRangeChanged(m_t0, m_t1);
    }
    m_autoY = false;
    update();
    event->accept();
}

void CanvasWidget::mousePressEvent(QMouseEvent *event)
{
    int legend = -1;
    for (int i = 0; i < m_legendRects.size(); ++i)
        if (m_legendRects.at(i).contains(event->position()))
            legend = i;
    if (event->button() == Qt::LeftButton && legend >= 0)
    {
        toggleChannel(legend);
        emit activated(m_title);
        return;
    }
    if (event->button() == Qt::RightButton && legend >= 0)
    {
        QMenu menu(this);
        QAction *hide = menu.addAction(m_series.at(legend).visible ? QStringLiteral("隐藏曲线")
                                                                   : QStringLiteral("显示曲线"));
        QAction *del = menu.addAction(QStringLiteral("删除曲线"));
        QAction *chosen = menu.exec(event->globalPosition().toPoint());
        if (chosen == hide)
            toggleChannel(legend);
        else if (chosen == del)
            removeChannel(legend);
        return;
    }
    if (event->button() == Qt::RightButton)
    {
        QMenu menu(this);
        QAction *reset = menu.addAction(QStringLiteral("重置视图"));
        QAction *fit = menu.addAction(QStringLiteral("自适应 Y"));
        menu.addSeparator();
        QAction *legend = menu.addAction(QStringLiteral("显示图例"));
        legend->setCheckable(true);
        legend->setChecked(m_showLegend);
        QAction *grid = menu.addAction(QStringLiteral("显示网格"));
        grid->setCheckable(true);
        grid->setChecked(m_showGrid);
        menu.addSeparator();
        QAction *add = menu.addAction(QStringLiteral("向下添加画布"));
        QAction *del = menu.addAction(QStringLiteral("删除此画布"));
        QAction *chosen = menu.exec(event->globalPosition().toPoint());
        if (chosen == reset)
        {
            m_autoY = true;
            resetView();
            update();
        }
        else if (chosen == fit)
            requestAutoY();
        else if (chosen == legend)
        {
            m_showLegend = legend->isChecked();
            update();
        }
        else if (chosen == grid)
        {
            m_showGrid = grid->isChecked();
            update();
        }
        else if (chosen == add)
            emit addRequested();
        else if (chosen == del)
            emit removeRequested();
        return;
    }
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = true;
        m_lastPos = event->position();
        emit activated(m_title);
    }
    QWidget::mousePressEvent(event);
}

void CanvasWidget::setTimeRange(double t0, double t1)
{
    if (t1 <= t0)
        return;
    m_t0 = t0;
    m_t1 = t1;
    update();
}

void CanvasWidget::requestAutoY()
{
    m_autoY = true;
    resetView();
    update();
}

void CanvasWidget::toggleChannel(int index)
{
    if (index < 0 || index >= m_series.size())
        return;
    m_series[index].visible = !m_series[index].visible;
    update();
}

void CanvasWidget::removeChannel(int index)
{
    if (index < 0 || index >= m_series.size())
        return;
    m_series.removeAt(index);
    resetView();
    update();
    emit activated(m_title);
}

void CanvasWidget::mouseMoveEvent(QMouseEvent *event)
{
    m_cursor = event->position();
    m_hover = true;
    if (m_dragging)
    {
        const QRectF plot = plotRect();
        const double dx = event->position().x() - m_lastPos.x();
        const double dy = event->position().y() - m_lastPos.y();
        const double tShift = -dx * (m_t1 - m_t0) / plot.width();
        m_t0 += tShift;
        m_t1 += tShift;
        const double yShift = -dy * (m_yMax - m_yMin) / plot.height();
        m_yMin += yShift;
        m_yMax += yShift;
        m_lastPos = event->position();
        m_autoY = false;
        if (dx != 0.0)
            emit timeRangeChanged(m_t0, m_t1);
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
    m_autoY = true;
    resetView();
    QWidget::mouseDoubleClickEvent(event);
}

void CanvasWidget::leaveEvent(QEvent *event)
{
    m_hover = false;
    update();
    QWidget::leaveEvent(event);
}
