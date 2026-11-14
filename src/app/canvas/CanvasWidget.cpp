#include "canvas/CanvasWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QWheelEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QSaveFile>
#include <QTextStream>
#include <QSettings>
#include <QDateTime>
#include <QColorDialog>
#include <QJsonArray>
#include <QMap>

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
    const QSettings st(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
    m_showGrid = st.value(QStringLiteral("gridDefault"), true).toBool();
    m_showLegend = st.value(QStringLiteral("legendDefault"), true).toBool();
    m_timeFormat = st.value(QStringLiteral("timeFormat"), QStringLiteral("relative")).toString();
    addDemoCurves();
}

void CanvasWidget::reloadDisplaySettings()
{
    const QSettings st(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
    m_timeFormat = st.value(QStringLiteral("timeFormat"), QStringLiteral("relative")).toString();
    update();
}

QString CanvasWidget::formatTime(double t) const
{
    if (m_timeFormat == QLatin1String("clock"))
    {
        const bool neg = t < 0;
        const double abs = neg ? -t : t;
        const qint64 whole = static_cast<qint64>(abs);
        const int h = static_cast<int>(whole / 3600);
        const int m = static_cast<int>((whole / 60) % 60);
        const double sec = std::fmod(abs, 60.0);
        return QStringLiteral("%1%2:%3:%4")
            .arg(neg ? QStringLiteral("-") : QString())
            .arg(h, 2, 10, QChar('0'))
            .arg(m, 2, 10, QChar('0'))
            .arg(QString::number(sec, 'f', 2).rightJustified(5, QChar('0')));
    }
    if (m_timeFormat == QLatin1String("utc") && std::abs(t) >= 100000000.0)
        return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(t * 1000.0), Qt::UTC)
            .toString(QStringLiteral("HH:mm:ss"));
    return QString::number(t, 'f', t >= 100 ? 0 : 2);
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
        s.label = s.name;
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
        s.label = m_inputSrcAlias.isEmpty() ? s.name
                                            : m_inputSrcAlias + QStringLiteral(" · ") + s.name;
        s.unit = QString::fromUtf8(ch->unit ? ch->unit : "");
        s.color = kPalette[idx++ % 6];
        s.srcId = m_inputSrcId;
        s.srcPath = m_inputSrcPath;
        s.offsetSec = m_inputOffset;
        s.t = ch->time;
        s.v = ch->value;
        s.n = ch->point_count;
        m_series.append(s);
    }
    m_autoY = true;
    resetView();
}

QVector<CanvasWidget::SampleSeries> CanvasWidget::sampleSeries() const
{
    QVector<SampleSeries> out;
    for (const Series &s : m_series)
    {
        if (!s.visible || s.count() == 0)
            continue;
        SampleSeries o;
        o.name = s.label;
        o.time.reserve(static_cast<int>(s.count()));
        o.value.reserve(static_cast<int>(s.count()));
        for (quint32 i = 0; i < s.count(); ++i)
        {
            o.time.append(s.time()[i] - s.offsetSec);
            o.value.append(s.value()[i]);
        }
        out.append(o);
    }
    return out;
}

void CanvasWidget::addGeneratedSeries(const QString &name, const QVector<double> &time,
                                      const QVector<double> &value)
{
    if (time.isEmpty() || value.isEmpty() || time.size() != value.size())
        return;
    for (int i = m_series.size() - 1; i >= 0; --i)
        if (m_series.at(i).name == name && !m_series.at(i).t)
            m_series.removeAt(i);
    Series s;
    s.name = name;
    s.label = name;
    s.color = kPalette[m_series.size() % 6];
    s.tOwn = time;
    s.vOwn = value;
    m_series.append(s);
    m_kernelBacked = false;
    m_autoY = true;
    resetView();
    update();
    emit activated(m_title);
}

bool CanvasWidget::replaceGeneratedSeries(const QString &oldLabel, const QString &name,
                                          const QVector<double> &time, const QVector<double> &value)
{
    if (time.isEmpty() || value.isEmpty() || time.size() != value.size())
        return false;
    for (int i = m_series.size() - 1; i >= 0; --i)
        if (m_series.at(i).name == name && !m_series.at(i).t)
            m_series.removeAt(i);
    for (Series &s : m_series)
    {
        if (s.label != oldLabel && s.name != oldLabel)
            continue;
        // 只接管显示值：颜色/单位/来源路径保持原样。
        s.name = name;
        s.label = name;
        s.tOwn = time;
        s.vOwn = value;
        s.t = nullptr;
        s.v = nullptr;
        s.n = 0;
        m_kernelBacked = false;
        m_autoY = true;
        resetView();
        update();
        emit activated(m_title);
        return true;
    }
    return false;
}

void CanvasWidget::removeGeneratedSeries(const QString &name)
{
    for (int i = m_series.size() - 1; i >= 0; --i)
        if (m_series.at(i).name == name && !m_series.at(i).t)
            m_series.removeAt(i);
    update();
    emit activated(m_title);
}

void CanvasWidget::setInputSource(int id, const QString &path, const QString &alias, double offsetSec)
{
    m_inputSrcId = id;
    m_inputSrcPath = path;
    m_inputSrcAlias = alias;
    m_inputOffset = offsetSec;
}

void CanvasWidget::renameSource(const QString &path, const QString &alias)
{
    for (Series &s : m_series)
        if (s.srcPath == path)
            s.label = alias + QStringLiteral(" · ") + s.name;
    update();
}

void CanvasWidget::removeChannelsFromSource(const QString &path)
{
    for (int i = m_series.size() - 1; i >= 0; --i)
    {
        if (m_series.at(i).srcPath == path)
            m_series.remove(i);
    }
    m_kernelBacked = !m_series.isEmpty();
    update();
}

void CanvasWidget::addChannel(const PsChannelInfo *ch)
{
    addChannelEx(ch, m_inputSrcId, m_inputSrcPath, m_inputSrcAlias, m_inputOffset);
}

void CanvasWidget::addChannelEx(const PsChannelInfo *ch, int srcId, const QString &path,
                                const QString &alias, double offsetSec)
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
    s.label = alias.isEmpty() ? s.name : alias + QStringLiteral(" · ") + s.name;
    s.unit = QString::fromUtf8(ch->unit ? ch->unit : "");
    s.srcId = srcId;
    s.srcPath = path;
    s.offsetSec = offsetSec;
    s.color = kPalette[m_series.size() % 6];
    s.info = ch;
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

void CanvasWidget::removeChannel(const QString &name)
{
    for (int i = m_series.size() - 1; i >= 0; --i)
    {
        if (m_series[i].name == name && (m_inputSrcId < 0 || m_series[i].srcId == m_inputSrcId))
            m_series.removeAt(i);
    }
    m_autoY = true;
    resetView();
    update();
}

bool CanvasWidget::replaceChannel(const QString &oldName, const PsChannelInfo *ch)
{
    if (!ch || ch->point_count < 2 || !ch->time || !ch->value)
        return false;
    for (Series &s : m_series)
    {
        if (s.name != oldName || (m_inputSrcId >= 0 && s.srcId != m_inputSrcId))
            continue;
        s.name = QString::fromUtf8(ch->name);
        s.label = m_inputSrcAlias.isEmpty() ? s.name : m_inputSrcAlias + QStringLiteral(" · ") + s.name;
        s.unit = QString::fromUtf8(ch->unit ? ch->unit : "");
        s.t = ch->time;
        s.v = ch->value;
        s.n = ch->point_count;
        m_kernelBacked = true;
        m_autoY = true;
        resetView();
        update();
        emit activated(m_title);
        return true;
    }
    return false;
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

bool CanvasWidget::hasRightAxis() const
{
    for (const Series &s : m_series)
        if (s.axis == 1)
            return true;
    return false;
}

int CanvasWidget::seriesAxisCount() const
{
    int n = 0;
    for (const Series &s : m_series)
        n += (s.axis == 1 ? 1 : 0);
    return n;
}

QRectF CanvasWidget::plotRect() const
{
    return QRectF(rect()).adjusted(62, 30, hasRightAxis() ? -60 : -16, -26);
}

void CanvasWidget::setFollowLive(bool on, double windowSec)
{
    m_followLive = on;
    m_liveWindow = qMax(0.5, windowSec);
    if (on)
    {
        m_autoY = true;
        resetView();
    }
}

void CanvasWidget::ensureTimeRange()
{
    bool init = false;
    for (const Series &s : m_series)
    {
        if (s.count() < 2)
            continue;
        const double a = s.time()[0] - s.offsetSec;
        const double b = s.time()[s.count() - 1] - s.offsetSec;
        if (!init) { m_t0 = a; m_t1 = b; init = true; }
        else { m_t0 = std::min(m_t0, a); m_t1 = std::max(m_t1, b); }
    }
    if (m_followLive && init)
    {
        double latest = m_t1;
        for (const Series &s : m_series)
        {
            if (s.count() == 0)
                continue;
            latest = std::max(latest, s.time()[s.count() - 1] - s.offsetSec);
        }
        m_t1 = latest;
        m_t0 = latest - m_liveWindow;
        return;
    }
    if (!(m_t1 > m_t0))
        m_t1 = m_t0 + 1.0;
}

void CanvasWidget::setSourceOffset(const QString &path, double offsetSec, bool refit)
{
    int hit = 0;
    for (Series &s : m_series)
        if (s.srcPath == path)
        { s.offsetSec = offsetSec; ++hit; }
    if (refit)
        resetView();
    repaint();
}

void CanvasWidget::resetView()
{
    ensureTimeRange();

    // 自动 Y：整段数据的 min/max（LOD 在 Rust 侧算，这里只要 256 个桶）
    if (m_autoY && !m_series.isEmpty())
    {
        bool init[2] = {false, false};
        double lo[2] = {0.0, 0.0};
        double hi[2] = {0.0, 0.0};
        for (const Series &s : m_series)
        {
            if (s.count() < 2)
                continue;
            const int ax = s.axis == 1 ? 1 : 0;
            QVector<double> buf(256 * 3);
            const uint32_t got = ps_lod_minmax(s.time(), s.value(), s.count(),
                                              m_t0 + s.offsetSec, m_t1 + s.offsetSec,
                                              256, buf.data(), 256);
            for (uint32_t i = 0; i < got; ++i)
            {
                const double mn = buf[i * 3 + 1];
                const double mx = buf[i * 3 + 2];
                if (!std::isfinite(mn) || !std::isfinite(mx))
                    continue;
                if (!init[ax])
                {
                    lo[ax] = mn;
                    hi[ax] = mx;
                    init[ax] = true;
                }
                else
                {
                    lo[ax] = std::min(lo[ax], mn);
                    hi[ax] = std::max(hi[ax], mx);
                }
            }
        }
        for (int ax = 0; ax < 2; ++ax)
        {
            if (!init[ax])
                continue;
            const double pad = (hi[ax] - lo[ax]) * 0.12 + 1e-6;
            if (ax == 0)
            {
                m_yMin = lo[0] - pad;
                m_yMax = hi[0] + pad;
            }
            else
            {
                m_y2Min = lo[1] - pad;
                m_y2Max = hi[1] + pad;
            }
        }
    }
    update();
}


void CanvasWidget::setPlotMode(PlotMode mode, const QString &tag)
{
    m_mode = mode;
    m_modeTag = tag;
    m_abEnabled = false;
    m_abHasA = m_abHasB = false;
    update();
}

double CanvasWidget::lastValue(const QString &namePart, bool *ok) const
{
    if (ok)
        *ok = false;
    const Series *hit = nullptr;
    for (const Series &s : m_series)
    {
        if (s.visible && s.count() && (namePart.isEmpty() || s.label.contains(namePart, Qt::CaseInsensitive)))
        {
            hit = &s;
            break;
        }
    }
    if (!hit)
    {
        for (const Series &s : m_series)
            if (s.visible && s.count())
            {
                hit = &s;
                break;
            }
    }
    if (!hit)
        return 0.0;
    for (int i = int(hit->count()) - 1; i >= 0; --i)
    {
        const double t = hit->time()[i] - hit->offsetSec;
        if (t < m_t0 || t > m_t1)
            continue;
        const double v = hit->value()[i];
        if (!std::isfinite(v))
            continue;
        if (ok)
            *ok = true;
        return v;
    }
    return 0.0;
}

void CanvasWidget::paintXy(QPainter &p, const QRectF &plot, const QFontMetrics &fm, bool scatter)
{
    QVector<const Series *> vis;
    for (const Series &s : m_series)
        if (s.visible && s.count() >= 2)
            vis.append(&s);
    if (vis.isEmpty())
    {
        p.setPen(colorDim());
        p.drawText(plot, Qt::AlignCenter, QStringLiteral("XY：拖入两条通道"));
        return;
    }
    const Series *sx = vis.first();
    QVector<double> xv;
    xv.reserve(int(sx->count()));
    for (quint32 i = 0; i < sx->count(); ++i)
        xv.append(sx->value()[i]);
    double xlo = 0, xhi = 1, ylo = 0, yhi = 1;
    bool init = false;
    for (double x : xv)
    {
        if (!init) { xlo = xhi = x; init = true; }
        else { xlo = std::min(xlo, x); xhi = std::max(xhi, x); }
    }
    if (!init) { xlo = 0; xhi = 1; }
    if (xhi - xlo < 1e-12) { xlo -= 0.5; xhi += 0.5; }

    QVector<const Series *> yseries;
    QVector<QVector<double>> ys;
    for (int i = 1; i < vis.size(); ++i)
    {
        const Series *sy = vis.at(i);
        QVector<double> y;
        y.reserve(xv.size());
        for (int k = 0; k < xv.size(); ++k)
        {
            double v = 0.0;
            if (sy->count() == sx->count())
                v = sy->value()[k];
            else
            {
                bool ok = false;
                v = sampleAt(*sy, sx->time()[k] - sx->offsetSec, &ok);
                if (!ok)
                    continue;
            }
            if (!std::isfinite(v))
                continue;
            y.append(v);
            if (!init) { ylo = yhi = v; init = true; }
            else { ylo = std::min(ylo, v); yhi = std::max(yhi, v); }
        }
        yseries.append(sy);
        ys.append(y);
    }
    if (vis.size() == 1)
    {
        QVector<double> y;
        for (double v : xv)
        {
            y.append(v);
            if (!init) { ylo = yhi = v; init = true; }
            else { ylo = std::min(ylo, v); yhi = std::max(yhi, v); }
        }
        yseries.append(sx);
        ys.append(y);
        xv.clear();
        for (int i = 0; i < y.size(); ++i)
            xv.append(i);
        xlo = 0;
        xhi = std::max(1, int(xv.size()) - 1);
    }
    if (!init) { ylo = -1; yhi = 1; }
    if (yhi - ylo < 1e-12) { ylo -= 0.5; yhi += 0.5; }

    p.setPen(colorGrid());
    for (int i = 0; i <= 4; ++i)
        p.drawLine(QPointF(plot.left(), plot.top() + plot.height() * i / 4),
                   QPointF(plot.right(), plot.top() + plot.height() * i / 4));
    for (int i = 0; i <= 4; ++i)
        p.drawLine(QPointF(plot.left() + plot.width() * i / 4, plot.top()),
                   QPointF(plot.left() + plot.width() * i / 4, plot.bottom()));
    p.setPen(colorText());
    for (int i = 0; i <= 4; ++i)
    {
        const double v = yhi - (yhi - ylo) * i / 4;
        p.drawText(QPointF(plot.left() - 8 - fm.horizontalAdvance(QString::number(v, 'g', 4)),
                           plot.top() + plot.height() * i / 4 + fm.ascent() / 2 - 1),
                   QString::number(v, 'g', 4));
        const double x = xlo + (xhi - xlo) * i / 4;
        p.drawText(QPointF(plot.left() + plot.width() * i / 4 - 20, plot.bottom() + fm.ascent() + 6),
                   QString::number(x, 'g', 4));
    }
    p.setPen(colorDim());
    p.drawText(QPointF(plot.left(), plot.top() - 6), QStringLiteral("Y"));
    p.drawText(QPointF(plot.right() - 30, plot.bottom() + fm.ascent() + 16), QStringLiteral("X"));

    p.save();
    p.setClipRect(plot);
    for (int yi = 0; yi < ys.size(); ++yi)
    {
        const QVector<double> &y = ys.at(yi);
        if (y.isEmpty())
            continue;
        QPolygonF poly;
        const int n = std::min(int(xv.size()), int(y.size()));
        for (int i = 0; i < n; ++i)
        {
            const double px = plot.left() + plot.width() * (xv.at(i) - xlo) / (xhi - xlo);
            const double py = plot.bottom() - plot.height() * (y.at(i) - ylo) / (yhi - ylo);
            poly.append(QPointF(px, py));
        }
        p.setPen(QPen(yseries.at(yi)->color, scatter ? 1.4 : yseries.at(yi)->width));
        if (scatter)
        {
            for (const QPointF &q : poly)
                p.drawEllipse(q, 2.0, 2.0);
        }
        else
        {
            p.drawPolyline(poly);
        }
        m_hitPaths.append(poly);
    }
    p.restore();
}

void CanvasWidget::paintBar(QPainter &p, const QRectF &plot, const QFontMetrics &fm)
{
    QVector<const Series *> vis;
    for (const Series &s : m_series)
        if (s.visible && s.count())
            vis.append(&s);
    if (vis.isEmpty())
        return;
    QVector<double> means;
    double lo = 0, hi = 0;
    for (const Series *s : vis)
    {
        double sum = 0;
        int n = 0;
        for (quint32 i = 0; i < s->count(); ++i)
        {
            const double t = s->time()[i] - s->offsetSec;
            if (t < m_t0 || t > m_t1 || !std::isfinite(s->value()[i]))
                continue;
            sum += s->value()[i];
            ++n;
        }
        const double m = n ? sum / n : 0;
        means.append(m);
        lo = std::min(lo, m);
        hi = std::max(hi, m);
    }
    if (hi - lo < 1e-9) hi = lo + 1;
    const double y0 = std::min(0.0, lo);
    const double y1 = std::max(0.0, hi);
    const double bw = plot.width() / std::max(1, int(means.size())) * 0.55;
    p.save();
    p.setClipRect(plot);
    for (int i = 0; i < means.size(); ++i)
    {
        const double x = plot.left() + plot.width() * (i + 0.5) / means.size();
        const double y = plot.bottom() - plot.height() * (means.at(i) - y0) / std::max(1e-9, y1 - y0);
        QRectF bar(x - bw / 2, y, bw, plot.bottom() - y);
        p.fillRect(bar, vis.at(i)->color);
        p.setPen(colorText());
        p.drawText(QRectF(x - bw, y - fm.height() - 2, bw * 2, fm.height()),
                   Qt::AlignCenter, fm.elidedText(vis.at(i)->label, Qt::ElideRight, int(bw * 2)));
    }
    p.restore();
}

void CanvasWidget::paintHistogram(QPainter &p, const QRectF &plot, const QFontMetrics &fm)
{
    const Series *s = nullptr;
    for (const Series &x : m_series)
        if (x.visible && x.count()) { s = &x; break; }
    if (!s)
        return;
    QVector<double> v;
    for (quint32 i = 0; i < s->count(); ++i)
    {
        const double t = s->time()[i] - s->offsetSec;
        if (t >= m_t0 && t <= m_t1 && std::isfinite(s->value()[i]))
            v.append(s->value()[i]);
    }
    if (v.isEmpty())
        return;
    const int bins = 40;
    const double lo = *std::min_element(v.begin(), v.end());
    const double hi = *std::max_element(v.begin(), v.end());
    const double span = std::max(1e-9, hi - lo);
    QVector<int> counts(bins, 0);
    for (double x : v)
        counts[std::clamp(int((x - lo) / span * bins), 0, bins - 1)]++;
    const int maxC = *std::max_element(counts.begin(), counts.end());
    p.save();
    p.setClipRect(plot);
    for (int i = 0; i < bins; ++i)
    {
        const double x = plot.left() + plot.width() * i / bins;
        const double h = plot.height() * counts.at(i) / std::max(1, maxC);
        p.fillRect(QRectF(x + 1, plot.bottom() - h, plot.width() / bins - 2, h), s->color);
    }
    p.setPen(colorText());
    p.drawText(QPointF(plot.left(), plot.bottom() + fm.ascent() + 6), QString::number(lo, 'g', 4));
    p.drawText(QPointF(plot.right() - 40, plot.bottom() + fm.ascent() + 6), QString::number(hi, 'g', 4));
    p.drawText(QPointF(plot.left(), plot.top() - 6), QStringLiteral("N"));
    p.restore();
}

void CanvasWidget::paintSpectrum(QPainter &p, const QRectF &plot, const QFontMetrics &fm)
{
    const Series *s = nullptr;
    for (const Series &x : m_series)
        if (x.visible && x.count()) { s = &x; break; }
    if (!s || s->count() < 4)
        return;
    const int n = std::min(512, int(s->count()));
    QVector<double> x(n);
    const double t0 = s->time()[s->count() - n] - s->offsetSec;
    const double t1 = s->time()[s->count() - 1] - s->offsetSec;
    const double dt = (t1 - t0) / std::max(1, n - 1);
    double mean = 0;
    for (int i = 0; i < n; ++i)
        mean += s->value()[s->count() - n + i];
    mean /= n;
    for (int i = 0; i < n; ++i)
        x[i] = s->value()[s->count() - n + i] - mean;
    const int half = n / 2;
    QVector<double> amp(half, 0);
    for (int k = 0; k < half; ++k)
    {
        double re = 0, im = 0;
        for (int i = 0; i < n; ++i)
        {
            const double a = -2.0 * kPi * k * i / n;
            re += x.at(i) * std::cos(a);
            im += x.at(i) * std::sin(a);
        }
        amp[k] = 2.0 * std::sqrt(re * re + im * im) / n;
    }
    const double fmax = half / std::max(1e-9, n * dt);
    const double amax = *std::max_element(amp.begin(), amp.end());
    p.save();
    p.setClipRect(plot);
    QPolygonF poly;
    for (int k = 0; k < half; ++k)
    {
        poly.append(QPointF(plot.left() + plot.width() * k / std::max(1, half - 1),
                            plot.bottom() - plot.height() * amp.at(k) / std::max(1e-12, amax)));
    }
    p.setPen(QPen(s->color, 1));
    p.drawPolyline(poly);
    p.setPen(colorText());
    p.drawText(QPointF(plot.left(), plot.bottom() + fm.ascent() + 6), QStringLiteral("0"));
    p.drawText(QPointF(plot.right() - 56, plot.bottom() + fm.ascent() + 6),
               QStringLiteral("%1 Hz").arg(fmax, 0, 'g', 3));
    p.drawText(QPointF(plot.left(), plot.top() - 6), QStringLiteral("幅值"));
    p.restore();
}

void CanvasWidget::paintHorizon(QPainter &p, const QRectF &plot, const QFontMetrics &fm)
{
    bool okr = false, okp = false;
    const double roll = lastValue(QStringLiteral("roll"), &okr);
    const double pitch = lastValue(QStringLiteral("pitch"), &okp);
    const double r = okr ? roll : 0.0, pt = okp ? pitch : 0.0;
    const QPointF c = plot.center();
    const double rad = std::min(plot.width(), plot.height()) * 0.42;
    p.save();
    p.translate(c);
    p.rotate(-r);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#1b4f72"));
    p.drawEllipse(QPointF(0, -pt * 2.0), rad, rad);
    p.setBrush(QColor("#6b4f2a"));
    p.drawEllipse(QPointF(0, -pt * 2.0 + rad), rad, rad);
    p.restore();
    p.setPen(QPen(QColor("#f2b134"), 2));
    p.drawEllipse(c, rad, rad);
    p.drawLine(QPointF(c.x() - rad, c.y()), QPointF(c.x() + rad, c.y()));
    p.drawLine(QPointF(c.x(), c.y() - rad), QPointF(c.x(), c.y() + rad));
    p.setPen(colorText());
    p.drawText(QRectF(c.x() - 120, c.y() + rad + 6, 240, fm.height()),
               Qt::AlignCenter, QStringLiteral("roll %1°   pitch %2°")
                                   .arg(r, 0, 'f', 1).arg(pt, 0, 'f', 1));
}

void CanvasWidget::paintCompass(QPainter &p, const QRectF &plot, const QFontMetrics &fm)
{
    bool ok = false;
    const double hdg = lastValue(QStringLiteral("yaw"), &ok);
    const double h = ok ? hdg : lastValue(QStringLiteral("heading"), &ok) ? hdg : 0.0;
    const QPointF c = plot.center();
    const double rad = std::min(plot.width(), plot.height()) * 0.40;
    p.setPen(QPen(colorGrid(), 1));
    p.drawEllipse(c, rad, rad);
    for (int i = 0; i < 12; ++i)
    {
        const double a = i * 30.0 * kPi / 180.0;
        p.drawLine(c + QPointF(std::sin(a) * (rad - 5), -std::cos(a) * (rad - 5)),
                   c + QPointF(std::sin(a) * rad, -std::cos(a) * rad));
    }
    const double a = h * kPi / 180.0;
    p.setPen(QPen(QColor("#f85149"), 2));
    p.drawLine(c, c + QPointF(std::sin(a) * rad * 0.75, -std::cos(a) * rad * 0.75));
    p.setPen(colorText());
    p.drawText(QRectF(c.x() - 100, c.y() + rad + 6, 200, fm.height()),
               Qt::AlignCenter, QStringLiteral("HDG %1°").arg(h, 0, 'f', 1));
}

void CanvasWidget::paintGauge(QPainter &p, const QRectF &plot, const QFontMetrics &fm)
{
    bool ok = false;
    const double v = lastValue(QString(), &ok);
    double lo = 0, hi = 1;
    for (const Series &s : m_series)
    {
        if (!s.visible || !s.count())
            continue;
        for (quint32 i = 0; i < s.count(); ++i)
        {
            const double x = s.value()[i];
            if (!std::isfinite(x))
                continue;
            lo = std::min(lo, x);
            hi = std::max(hi, x);
        }
    }
    if (hi - lo < 1e-9) hi = lo + 1;
    const QPointF c = plot.center();
    const double rad = std::min(plot.width(), plot.height()) * 0.40;
    const QRectF arc(c.x() - rad, c.y() - rad, rad * 2, rad * 2);
    p.setPen(QPen(colorGrid(), 2));
    p.drawArc(arc, 225 * 16, -270 * 16);
    const double frac = std::clamp((v - lo) / (hi - lo), 0.0, 1.0);
    p.setPen(QPen(QColor("#4ea1ff"), 4));
    p.drawArc(arc, 225 * 16, int(-270.0 * frac * 16));
    const double a = (225.0 - 270.0 * frac) * kPi / 180.0;
    p.setPen(QPen(QColor("#f2b134"), 2));
    p.drawLine(c, c + QPointF(std::cos(a) * rad * 0.9, -std::sin(a) * rad * 0.9));
    p.setPen(colorText());
    p.drawText(QRectF(c.x() - 100, c.y() - fm.height() / 2, 200, fm.height()),
               Qt::AlignCenter, ok ? QString::number(v, 'g', 5) : QStringLiteral("--"));
    p.drawText(QRectF(c.x() - 100, c.y() + rad + 6, 200, fm.height()),
               Qt::AlignCenter, QStringLiteral("%1 ~ %2").arg(lo, 0, 'g', 3).arg(hi, 0, 'g', 3));
}

void CanvasWidget::paintGpsMap(QPainter &p, const QRectF &plot, const QFontMetrics &fm)
{
    const Series *latS = nullptr;
    const Series *lonS = nullptr;
    for (const Series &s : m_series)
    {
        if (!s.visible || s.count() < 2)
            continue;
        const QString n = s.name.toLower();
        if (!latS && (n.contains(QStringLiteral("latitude")) || n.endsWith(QStringLiteral("lat")) ||
                      n.contains(QStringLiteral(".lat"))
                      || n.contains(QStringLiteral("lat_"))))
            latS = &s;
        if (!lonS && (n.contains(QStringLiteral("longitude")) || n.contains(QStringLiteral("lng")) ||
                      n.contains(QStringLiteral("lon"))))
            lonS = &s;
    }
    if (!latS || !lonS)
    {
        p.setPen(colorDim());
        p.drawText(plot, Qt::AlignCenter, QStringLiteral("GPS 地图需要同时拖入纬度与经度通道\n（名称含 lat / lon）"));
        return;
    }

    QVector<QPointF> pts;
    const quint32 n = qMin(latS->count(), lonS->count());
    for (quint32 i = 0; i < n; ++i)
    {
        const double lat = latS->value()[i];
        const double lon = lonS->value()[i];
        if (!std::isfinite(lat) || !std::isfinite(lon) || qAbs(lat) > 90.0 || qAbs(lon) > 180.0)
            continue;
        pts.append(QPointF(lon, lat));
    }
    if (pts.isEmpty())
    {
        p.setPen(colorDim());
        p.drawText(plot, Qt::AlignCenter, QStringLiteral("经纬度通道没有有效数据"));
        return;
    }

    double minLon = pts.first().x(), maxLon = minLon;
    double minLat = pts.first().y(), maxLat = minLat;
    for (const QPointF &q : pts)
    {
        minLon = std::min(minLon, q.x());
        maxLon = std::max(maxLon, q.x());
        minLat = std::min(minLat, q.y());
        maxLat = std::max(maxLat, q.y());
    }
    const double padLon = std::max((maxLon - minLon) * 0.08, 1e-5);
    const double padLat = std::max((maxLat - minLat) * 0.08, 1e-5);
    minLon -= padLon; maxLon += padLon;
    minLat -= padLat; maxLat += padLat;
    const auto xOf = [&](double lon) { return plot.left() + plot.width() * (lon - minLon) / (maxLon - minLon); };
    const auto yOf = [&](double lat) { return plot.bottom() - plot.height() * (lat - minLat) / (maxLat - minLat); };

    p.setPen(QPen(colorGrid(), 1));
    for (int i = 0; i <= 4; ++i)
    {
        const double x = plot.left() + plot.width() * i / 4.0;
        const double y = plot.top() + plot.height() * i / 4.0;
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(colorText());
        p.drawText(QPointF(x + 3, plot.bottom() - 4),
                   QString::number(minLon + (maxLon - minLon) * i / 4.0, 'f', 5));
        p.drawText(QPointF(plot.left() + 3, y - 3),
                   QString::number(maxLat - (maxLat - minLat) * i / 4.0, 'f', 5));
        p.setPen(QPen(colorGrid(), 1));
    }
    p.setPen(QPen(QColor("#4ea1ff"), 1.6));
    QPolygonF poly;
    for (const QPointF &q : pts)
        poly.append(QPointF(xOf(q.x()), yOf(q.y())));
    p.drawPolyline(poly);
    p.setBrush(QColor("#7ee787"));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(xOf(pts.first().x()), yOf(pts.first().y())), 4, 4);
    p.setBrush(QColor("#ff7b72"));
    p.drawEllipse(QPointF(xOf(pts.last().x()), yOf(pts.last().y())), 4, 4);
    p.setPen(colorDim());
    p.drawText(QPointF(plot.left() + 6, plot.top() + fm.ascent() + 2),
               QStringLiteral("%1 点 · %2 / %3").arg(pts.size()).arg(latS->label, lonS->label));
}

void CanvasWidget::paintSurface3D(QPainter &p, const QRectF &plot, const QFontMetrics &fm)
{
    QVector<int> ids;
    double lo = 0, hi = 0;
    bool init = false;
    for (int i = 0; i < m_series.size(); ++i)
    {
        const Series &s = m_series.at(i);
        if (!s.visible || s.count() < 2)
            continue;
        ids.append(i);
        for (quint32 k = 0; k < s.count(); ++k)
        {
            const double v = s.value()[k];
            if (!std::isfinite(v))
                continue;
            if (!init) { lo = hi = v; init = true; }
            else { lo = std::min(lo, v); hi = std::max(hi, v); }
        }
    }
    if (ids.isEmpty() || !init)
    {
        p.setPen(colorDim());
        p.drawText(plot, Qt::AlignCenter, QStringLiteral("3D 曲面需要多条可见曲线\n按序列堆叠成瀑布"));
        return;
    }
    const double pad = (hi - lo) * 0.1 + 1e-6;
    lo -= pad;
    hi += pad;
    const double w = plot.width() * 0.74;
    const double h = plot.height() * 0.6;
    const double zStep = qMin(10.0, plot.height() * 0.4 / qMax(1, ids.size()));
    const double x0 = plot.left() + 6;
    const double yBase = plot.bottom() - 8;
    const int n = ids.size();

    p.setPen(QPen(colorGrid(), 1));
    p.drawLine(QPointF(x0, yBase), QPointF(x0 + w, yBase));
    p.drawLine(QPointF(x0, yBase), QPointF(x0, yBase - h));
    p.drawLine(QPointF(x0, yBase), QPointF(x0 + n * zStep * 0.5, yBase + n * zStep * 0.5));

    for (int k = n - 1; k >= 0; --k)
    {
        const Series &s = m_series.at(ids.at(k));
        const double z = (n - 1 - k) * zStep;
        QPolygonF poly;
        const quint32 cnt = s.count();
        const quint32 step = qMax<quint32>(1, cnt / 400);
        for (quint32 i = 0; i < cnt; i += step)
        {
            const double t = (s.time()[i] - s.offsetSec - m_t0) / std::max(1e-12, m_t1 - m_t0);
            const double v = s.value()[i];
            if (!std::isfinite(v) || t < 0 || t > 1)
                continue;
            poly.append(QPointF(x0 + w * t + z * 0.5,
                                yBase - h * (v - lo) / (hi - lo) + z * 0.5));
        }
        if (poly.size() < 2)
            continue;
        QColor c = s.color;
        c.setAlpha(90 + 165 * (k + 1) / n);
        p.setPen(QPen(c, 1.2));
        p.drawPolyline(poly);
    }
    p.setPen(colorText());
    p.drawText(QPointF(x0, yBase - h - 6), QStringLiteral("3D 瀑布 · %1 条").arg(n));
}

void CanvasWidget::paintTraj3D(QPainter &p, const QRectF &plot, const QFontMetrics &fm)
{
    QVector<const Series *> vis;
    for (const Series &s : m_series)
        if (s.visible && s.count())
            vis.append(&s);
    if (vis.size() < 2)
    {
        p.setPen(colorDim());
        p.drawText(plot, Qt::AlignCenter, QStringLiteral("3D 轨迹：至少两条通道（X/Y）"));
        return;
    }
    const int n = std::min(2000, int(vis.at(0)->count()));
    const Series *ax = vis.at(0);
    const Series *ay = vis.at(1);
    const Series *az = vis.size() > 2 ? vis.at(2) : nullptr;
    QVector<QPointF> proj;
    double cx = 0, cy = 0, cz = 0;
    int cnt = 0;
    for (int i = 0; i < n; ++i)
    {
        const double x = ax->value()[i];
        const double y = ay->count() == ax->count() ? ay->value()[i] : ax->value()[i];
        const double z = az && az->count() == ax->count() ? az->value()[i] : 0.0;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            continue;
        cx += x; cy += y; cz += z; ++cnt;
    }
    if (!cnt)
        return;
    cx /= cnt; cy /= cnt; cz /= cnt;
    double span = 1e-9;
    for (int i = 0; i < n; ++i)
    {
        const double x = ax->value()[i], y = ay->count() == ax->count() ? ay->value()[i] : ax->value()[i];
        const double z = az && az->count() == ax->count() ? az->value()[i] : 0.0;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            continue;
        span = std::max(span, std::max(std::abs(x - cx), std::max(std::abs(y - cy), std::abs(z - cz))));
    }
    const double scale = std::min(plot.width(), plot.height()) * 0.36 / span;
    auto prj = [&](double x, double y, double z) {
        const double dx = (x - cx) * scale, dy = (y - cy) * scale, dz = (z - cz) * scale;
        return QPointF(plot.center().x() + (dx - dy) * 0.72,
                       plot.center().y() + dz * 0.9 - (dx + dy) * 0.22);
    };
    for (int i = 0; i < n; ++i)
    {
        const double x = ax->value()[i], y = ay->count() == ax->count() ? ay->value()[i] : ax->value()[i];
        const double z = az && az->count() == ax->count() ? az->value()[i] : 0.0;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            continue;
        proj.append(prj(x, y, z));
    }
    if (proj.size() < 2)
        return;
    // 参考立方体边框
    p.setPen(QPen(colorGrid(), 1));
    const double s = span;
    const QPointF o = prj(cx - s, cy - s, cz - s);
    const QPointF ex = prj(cx + s, cy - s, cz - s);
    const QPointF ey = prj(cx - s, cy + s, cz - s);
    const QPointF ez = prj(cx - s, cy - s, cz + s);
    p.drawLine(o, ex); p.drawLine(o, ey); p.drawLine(o, ez);
    p.drawLine(prj(cx + s, cy + s, cz - s), ex);
    p.drawLine(prj(cx + s, cy + s, cz - s), ey);
    p.drawLine(prj(cx + s, cy + s, cz + s), ez);
    p.save();
    p.setClipRect(plot);
    p.setPen(QPen(ax->color, 1.6));
    p.drawPolyline(QPolygonF(proj));
    p.restore();
    p.setPen(colorDim());
    p.drawText(QPointF(plot.left(), plot.top() - 6),
               QStringLiteral("X %1  Y %2  Z %3")
                   .arg(ax->label, ay->label, az ? az->label : QStringLiteral("-")));
}

void CanvasWidget::paintSpecial(QPainter &p, const QRectF &plot, const QFontMetrics &fm)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    switch (m_mode)
    {
    case Xy: paintXy(p, plot, fm, false); break;
    case Scatter: paintXy(p, plot, fm, true); break;
    case BarChart: paintBar(p, plot, fm); break;
    case Histogram: paintHistogram(p, plot, fm); break;
    case Spectrum: paintSpectrum(p, plot, fm); break;
    case Horizon: paintHorizon(p, plot, fm); break;
    case Compass: paintCompass(p, plot, fm); break;
    case Gauge: paintGauge(p, plot, fm); break;
    case Traj3D: paintTraj3D(p, plot, fm); break;
    case GpsMap: paintGpsMap(p, plot, fm); break;
    case Surface3D: paintSurface3D(p, plot, fm); break;
    default: break;
    }
    p.restore();
}

void CanvasWidget::setPlayhead(double t)
{
    m_hasPlayhead = true;
    m_playhead = t;
    update();
}

void CanvasWidget::clearPlayhead()
{
    m_hasPlayhead = false;
    update();
}

void CanvasWidget::paintEvent(QPaintEvent *)
{
    // 实时源的 PsChannelInfo 指针稳定，point_count 可能已增长；绘制前同步并跟随 X 轴。
    bool liveGrew = false;
    for (Series &s : m_series)
    {
        if (s.info)
        {
            if (s.info->point_count != s.n)
            {
                s.n = s.info->point_count;
                liveGrew = liveGrew || s.srcPath.startsWith(QStringLiteral("live://"));
            }
        }
    }
    if (liveGrew)
    {
        resetView();
        emit activated(m_title);
    }
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

    if (m_hasPlayhead)
    {
        const double x = plot.left() + plot.width() * (m_playhead - m_t0) / (m_t1 - m_t0);
        if (x >= plot.left() && x <= plot.right())
        {
            p.setPen(QPen(QColor("#ffb454"), 1, Qt::DashLine));
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            p.setPen(QColor("#ffb454"));
            p.drawText(QPointF(std::min(x + 4, plot.right() - 40), plot.top() + fm.ascent() + 2),
                       QStringLiteral("视频"));
        }
    }

    if (m_mode != TimeSeries)
    {
        paintSpecial(p, plot, fm);
        return;
    }

    // 刻度与网格分开：grid off 保留坐标读数，只去掉网格线（MATLAB 习惯）
    const int yTicks = 5;
    for (int i = 0; i <= yTicks; ++i)
    {
        const double y = plot.top() + plot.height() * i / yTicks;
        if (m_showGrid)
        {
            p.setPen(QPen(colorGrid(), 1));
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        const double val = m_yMax - (m_yMax - m_yMin) * i / yTicks;
        p.setPen(colorText());
        const QString lbl = QString::number(val, 'g', 4);
        p.drawText(QPointF(plot.left() - 8 - fm.horizontalAdvance(lbl), y + fm.ascent() / 2 - 1), lbl);
    }
    if (hasRightAxis())
    {
        for (int i = 0; i <= yTicks; ++i)
        {
            const double y = plot.top() + plot.height() * i / yTicks;
            const double val = m_y2Max - (m_y2Max - m_y2Min) * i / yTicks;
            p.setPen(colorText());
            const QString lbl = QString::number(val, 'g', 4);
            p.drawText(QPointF(plot.right() + 8, y + fm.ascent() / 2 - 1), lbl);
        }
    }

    const int xTicks = 6;
    for (int i = 0; i <= xTicks; ++i)
    {
        const double x = plot.left() + plot.width() * i / xTicks;
        const double t = m_t0 + (m_t1 - m_t0) * i / xTicks;
        if (m_showGrid)
        {
            p.setPen(QPen(colorGrid(), 1));
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }
        p.setPen(colorText());
        const QString lbl = formatTime(t);
        p.drawText(QPointF(x - fm.horizontalAdvance(lbl) / 2, plot.bottom() + fm.ascent() + 6), lbl);
    }
    if (m_showBox)
    {
        p.setPen(QPen(colorGrid(), 1));
        p.drawRect(plot);
    }

    // 曲线：每像素一个 min/max 桶，画"包络带 + 中线"
    const uint32_t px = static_cast<uint32_t>(std::max(1.0, plot.width()));
    p.save();
    p.setClipRect(plot);
    p.setRenderHint(QPainter::Antialiasing, false);  // 曲线统一关 AA，整段开一次而不是每条切一次
    m_hitPaths.clear();
    m_hitOwner.clear();
    // 同源通道共享 offset，按 offset 分组批量抽稀；同组一次 FFI，内核在通道多时并行。
    QMap<double, QVector<int>> lodGroups;
    for (int si = 0; si < m_series.size(); ++si)
    {
        const Series &s = m_series.at(si);
        if (s.visible && s.count() >= 2)
            lodGroups[s.offsetSec].append(si);
    }

    const auto toX = [&](double t) { return plot.left() + plot.width() * (t - m_t0) / (m_t1 - m_t0); };
    const auto toY = [&](double v, int axis) {
        const double lo = axis == 1 ? m_y2Min : m_yMin;
        const double hi = axis == 1 ? m_y2Max : m_yMax;
        return plot.bottom() - plot.height() * (v - lo) / (hi - lo);
    };

    for (auto git = lodGroups.constBegin(); git != lodGroups.constEnd(); ++git)
    {
        const QVector<int> &ids = git.value();
        const double off = git.key();
        const int count = ids.size();
        const uint32_t stride = px * 3;
        QVector<const double *> times(count);
        QVector<const double *> values(count);
        QVector<uint32_t> points(count);
        QVector<uint32_t> got(count);
        for (int k = 0; k < count; ++k)
        {
            const Series &s = m_series.at(ids.at(k));
            times[k] = s.time();
            values[k] = s.value();
            points[k] = s.count();
        }
        QVector<double> lod(static_cast<int>(stride) * count);
        ps_lod_minmax_many(times.constData(), values.constData(), points.constData(),
                           static_cast<uint32_t>(count), m_t0 + off, m_t1 + off, px,
                           lod.data(), stride, got.data());

        for (int k = 0; k < count; ++k)
        {
            const int si = ids.at(k);
            const Series &s = m_series.at(si);
            const uint32_t n = got.at(k);
            if (n < 1)
                continue;
            const double *buf = lod.constData() + static_cast<int>(stride) * k;

            // 单屏只画中线：包络带留给"缩到很近、一像素含大量样本"的场景（M1）
            // 折线一律关 AA 用 drawPolyline：QPainterPath + AA 描边在 Qt 6.2 上是毫米级的
            // 灾难（20 条 × 1500 段实测 365 ms/帧，同样数据 drawPolyline 只要 1~2 ms）。
            QPolygonF poly;
            poly.reserve(static_cast<int>(n));
            for (uint32_t i = 0; i < n; ++i)
                poly.append(QPointF(toX(buf[i * 3] - s.offsetSec),
                                    toY((buf[i * 3 + 1] + buf[i * 3 + 2]) / 2.0, s.axis)));
            p.setPen(QPen(s.color, s.width));  // 1px 走快速 Bresenham；>1 才走宽线轮廓器
            p.drawPolyline(poly);
            m_hitPaths.append(poly);
            m_hitOwner.append(si);
        }
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
            const QString lbl0 = s.unit.isEmpty() ? s.label : QStringLiteral("%1 [%2]").arg(s.label, s.unit);
            const QString lbl = s.axis == 1 ? lbl0 + QStringLiteral(" →右") : lbl0;
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
    if (m_abEnabled && m_abHasA)
    {
        const auto xOf = [&](double t) { return plot.left() + plot.width() * (t - m_t0) / (m_t1 - m_t0); };
        p.setPen(QPen(QColor("#f2b134"), 1, Qt::DashLine));
        p.drawLine(QPointF(xOf(m_abA), plot.top()), QPointF(xOf(m_abA), plot.bottom()));
        p.setPen(QColor("#f2b134"));
        p.drawText(QPointF(xOf(m_abA) + 4, plot.top() + fm.ascent() + 2), QStringLiteral("A"));
        if (m_abHasB)
        {
            p.setPen(QPen(QColor("#58a6ff"), 1, Qt::DashLine));
            p.drawLine(QPointF(xOf(m_abB), plot.top()), QPointF(xOf(m_abB), plot.bottom()));
            p.setPen(QColor("#58a6ff"));
            p.drawText(QPointF(xOf(m_abB) + 4, plot.top() + fm.ascent() + 2), QStringLiteral("B"));

            const double dt = m_abB - m_abA;
            QStringList rows;
            for (const Series &sr : m_series)
            {
                if (!sr.visible)
                    continue;
                bool oa = false, ob = false;
                const double va = sampleAt(sr, m_abA, &oa);
                const double vb = sampleAt(sr, m_abB, &ob);
                const QString lbl = fm.elidedText(sr.label, Qt::ElideMiddle, 132);
                const QString dy = (oa && ob) ? QString::number(vb - va, 'g', 6) : QStringLiteral("--");
                const QString k = (oa && ob && qAbs(dt) > 1e-12)
                                      ? QString::number((vb - va) / dt, 'g', 5) + QStringLiteral("/s")
                                      : QStringLiteral("--");
                rows << QStringLiteral("%1  Δy %2   k %3").arg(lbl, -10).arg(dy, -9).arg(k);
                if (rows.size() >= 6)
                    break;
            }
            const int lineH = fm.height() + 2;
            const int boxH = 42 + rows.size() * lineH + 6;
            QRectF box(plot.left() + 10, plot.bottom() - boxH - 8, 300, boxH);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(22, 27, 34, 238));
            p.drawRoundedRect(box, 3, 3);
            p.setPen(QColor("#f2b134"));
            p.drawText(QRectF(box.left() + 8, box.top() + 3, box.width() - 16, lineH),
                       Qt::AlignVCenter | Qt::AlignLeft,
                       QStringLiteral("A→B  Δt = %1 s").arg(dt, 0, 'f', 4));
            p.setPen(QColor("#8b949e"));
            p.drawLine(QPointF(box.left() + 7, box.top() + 21),
                       QPointF(box.right() - 7, box.top() + 21));
            int yy = int(box.top()) + 23;
            for (int i = 0; i < rows.size(); ++i)
            {
                p.setPen(i < m_series.size() ? m_series.at(i).color : colorText());
                p.drawText(QRectF(box.left() + 8, yy, box.width() - 16, lineH),
                           Qt::AlignVCenter | Qt::AlignLeft, rows.at(i));
                yy += lineH;
            }
        }
    }

    if (m_hover && plot.contains(m_cursor))
    {
        p.setPen(QPen(QColor(255, 255, 255, 60), 1, Qt::DashLine));
        p.drawLine(QPointF(m_cursor.x(), plot.top()), QPointF(m_cursor.x(), plot.bottom()));
        p.drawLine(QPointF(plot.left(), m_cursor.y()), QPointF(plot.right(), m_cursor.y()));

        const double t = m_t0 + (m_t1 - m_t0) * (m_cursor.x() - plot.left()) / plot.width();

        // 逐曲线读数：按显示时间找最近样本，不做插值；源间时间偏移已在此消掉。
        QStringList rows;
        int shown = 0;
        for (const Series &s : m_series)
        {
            if (!s.visible)
                continue;
            if (shown++ >= 8)
            {
                rows << QStringLiteral("…");
                break;
            }
            const QString lbl = fm.elidedText(s.label, Qt::ElideMiddle, 150);
            bool ok = false;
            const double v = sampleAt(s, t, &ok);
            rows << QStringLiteral("%1  %2").arg(lbl, ok ? QString::number(v, 'g', 6) : QStringLiteral("--"));
        }
        if (rows.isEmpty())
            rows << QStringLiteral("（无可见曲线）");

        const int lineH = fm.height() + 2;
        const int boxW = qMin(int(plot.width() - 16), 280);
        const int boxH = 22 + rows.size() * lineH + 6;
        QRectF box(m_cursor.x() + 12, plot.top() + 8, boxW, boxH);
        if (box.right() > plot.right())
            box.moveLeft(m_cursor.x() - 12 - boxW);
        if (box.bottom() > plot.bottom())
            box.moveTop(plot.bottom() - boxH - 4);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(22, 27, 34, 238));
        p.drawRoundedRect(box, 3, 3);

        p.setPen(QColor("#f2b134"));
        p.drawText(QRectF(box.left() + 8, box.top() + 3, box.width() - 16, lineH),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("t = %1").arg(formatTime(t)));
        p.setPen(QColor("#8b949e"));
        p.drawLine(QPointF(box.left() + 7, box.top() + 21),
                   QPointF(box.right() - 7, box.top() + 21));

        int y = int(box.top()) + 23;
        int colorIdx = 0;
        for (const Series &s : m_series)
        {
            if (!s.visible)
                continue;
            if (colorIdx >= 8)
                break;
            const QString lbl = fm.elidedText(s.label, Qt::ElideMiddle, 150);
            bool ok = false;
            const double v = sampleAt(s, t, &ok);
            const QString val = ok ? QString::number(v, 'g', 6) : QStringLiteral("--");
            p.setPen(s.color);
            p.drawText(QRectF(box.left() + 8, y, 155, lineH), Qt::AlignVCenter | Qt::AlignLeft, lbl);
            p.setPen(colorText());
            p.drawText(QRectF(box.left() + 166, y, box.width() - 174, lineH),
                       Qt::AlignVCenter | Qt::AlignRight, val);
            y += lineH;
            ++colorIdx;
        }
    }

    if (m_rubbering && m_rubberRect.isValid())
    {
        p.setPen(QPen(QColor(78, 161, 255, 220), 1, Qt::DashLine));
        p.setBrush(QColor(78, 161, 255, 40));
        p.drawRect(m_rubberRect);
        p.setBrush(Qt::NoBrush);
    }
}

void CanvasWidget::exportPng()
{
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("导出画布 PNG"), {},
                                                   QStringLiteral("PNG (*.png)"));
    if (f.isEmpty())
        return;
    if (grab().save(f))
        return;
    QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("无法写入 %1").arg(f));
}

void CanvasWidget::exportCsv()
{
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("导出画布 CSV"), {},
                                                   QStringLiteral("CSV (*.csv)"));
    if (f.isEmpty())
        return;
    QSaveFile out(f);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    QTextStream ts(&out);
    ts << "time,source,channel,value,unit\n";
    for (const Series &s : m_series)
    {
        if (!s.visible || !s.count())
            continue;
        for (quint32 i = 0; i < s.count(); ++i)
        {
            const double t = s.time()[i] - s.offsetSec;
            if (t < m_t0 || t > m_t1)
                continue;
            const double v = s.value()[i];
            if (!std::isfinite(v))
                continue;
            ts << QString::number(t, 'g', 10) << ',' << s.label.section(QStringLiteral(" · "), 0, 0)
               << ',' << s.name << ',' << QString::number(v, 'g', 12) << ',' << s.unit << '\n';
        }
    }
    if (!out.commit())
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("无法写入 %1").arg(f));
}

double CanvasWidget::sampleAt(const Series &s, double displayT, bool *ok) const
{
    if (ok)
        *ok = false;
    const quint32 n = s.count();
    if (!n || !s.time() || !s.value())
        return 0.0;
    const double rawT = displayT + s.offsetSec;
    quint32 lo = 0, hi = n;
    while (lo < hi)
    {
        const quint32 mid = (lo + hi) / 2;
        if (s.time()[mid] < rawT)
            lo = mid + 1;
        else
            hi = mid;
    }
    quint32 idx = lo;
    if (idx >= n)
        idx = n - 1;
    else if (idx > 0 && qAbs(s.time()[idx - 1] - rawT) <= qAbs(s.time()[idx] - rawT))
        --idx;
    const double v = s.value()[idx];
    if (!std::isfinite(v))
        return 0.0;
    if (ok)
        *ok = true;
    return v;
}

int CanvasWidget::seriesAt(const QPointF &pos) const
{
    const double lim = 20.0;
    for (int i = 0; i < m_hitPaths.size(); ++i)
    {
        const QPolygonF &poly = m_hitPaths.at(i);
        for (int k = 1; k < poly.size(); ++k)
        {
            const QPointF a = poly.at(k - 1);
            const QPointF b = poly.at(k);
            const QPointF ab = b - a;
            const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
            const QPointF ap = pos - a;
            double t = len2 > 0 ? (ap.x() * ab.x() + ap.y() * ab.y()) / len2 : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            const QPointF q = a + ab * t;
            const double dx = q.x() - pos.x();
            const double dy = q.y() - pos.y();
            if (dx * dx + dy * dy <= lim * lim)
                return m_hitOwner.value(i, -1);
        }
    }
    return -1;
}

void CanvasWidget::wheelEvent(QWheelEvent *event)
{
    m_followLive = false;
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
        m_y2Min = anchor + (m_y2Min - anchor) * factor;
        m_y2Max = anchor + (m_y2Max - anchor) * factor;
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
    if (event->button() == Qt::RightButton && seriesAt(event->position()) >= 0)
    {
        const int idx = seriesAt(event->position());
        Series &s = m_series[idx];
        QMenu menu(this);
        QAction *head = menu.addAction(s.label);
        head->setEnabled(false);
        menu.addSeparator();
        QAction *vis = menu.addAction(s.visible ? QStringLiteral("隐藏曲线") : QStringLiteral("显示曲线"));
        QPixmap swatch(12, 12);
        swatch.fill(s.color);
        QAction *color = menu.addAction(QIcon(swatch), QStringLiteral("修改颜色…"));
        QAction *axisToggle = menu.addAction(s.axis == 1 ? QStringLiteral("绑定左轴")
                                                         : QStringLiteral("绑定右轴"));
        QMenu *widths = menu.addMenu(QStringLiteral("线宽"));
        QAction *w1 = widths->addAction(QStringLiteral("1 px"));
        QAction *w2 = widths->addAction(QStringLiteral("2 px"));
        QAction *w3 = widths->addAction(QStringLiteral("3 px"));
        for (QAction *a : {w1, w2, w3})
            a->setCheckable(true);
        (s.width == 1 ? w1 : (s.width == 2 ? w2 : w3))->setChecked(true);
        menu.addSeparator();
        QAction *del = menu.addAction(QStringLiteral("删除曲线"));
        QAction *chosen = menu.exec(event->globalPosition().toPoint());
        if (chosen == vis)
            toggleChannel(idx);
        else if (chosen == axisToggle)
        {
            s.axis = s.axis == 1 ? 0 : 1;
            resetView();
        }
        else if (chosen == color)
        {
            const QColor c = QColorDialog::getColor(s.color, this, QStringLiteral("曲线颜色"));
            if (c.isValid())
            {
                s.color = c;
                update();
            }
        }
        else if (chosen == w1 || chosen == w2 || chosen == w3)
        {
            s.width = chosen == w1 ? 1 : (chosen == w2 ? 2 : 3);
            update();
        }
        else if (chosen == del)
            removeChannel(idx);
        return;
    }
    if (event->button() == Qt::RightButton)
    {
        QMenu menu(this);
        QAction *addCurve = menu.addAction(QStringLiteral("添加曲线…"));
        menu.addSeparator();
        QAction *reset = menu.addAction(QStringLiteral("重置缩放"));
        QAction *fit = menu.addAction(QStringLiteral("自适应 Y"));
        menu.addSeparator();
        QAction *legend = menu.addAction(QStringLiteral("显示图例"));
        legend->setCheckable(true);
        legend->setChecked(m_showLegend);
        QAction *grid = menu.addAction(QStringLiteral("显示网格"));
        grid->setCheckable(true);
        grid->setChecked(m_showGrid);
        QAction *ab = menu.addAction(QStringLiteral("A/B 游标测量（Δt/Δy）"));
        ab->setCheckable(true);
        ab->setChecked(m_abEnabled);
        menu.addSeparator();
        QAction *splitH = menu.addAction(QStringLiteral("向右拆分画布"));
        QAction *splitV = menu.addAction(QStringLiteral("向下拆分画布"));
        QAction *png = menu.addAction(QStringLiteral("导出 PNG…"));
        QAction *csv = menu.addAction(QStringLiteral("导出当前画布 CSV…"));
        menu.addSeparator();
        QAction *del = menu.addAction(QStringLiteral("删除此画布"));
        QAction *chosen = menu.exec(event->globalPosition().toPoint());
        if (chosen == addCurve)
            emit addChannelRequested();
        else if (chosen == reset)
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
        else if (chosen == ab)
        {
            m_abEnabled = ab->isChecked();
            if (!m_abEnabled)
            {
                m_abHasA = m_abHasB = false;
            }
            update();
        }
        else if (chosen == splitH)
            emit splitRequested(0);
        else if (chosen == splitV)
            emit splitRequested(1);
        else if (chosen == png)
            exportPng();
        else if (chosen == csv)
            exportCsv();
        else if (chosen == del)
            emit removeRequested();
        return;
    }
    if (event->button() == Qt::LeftButton && m_abEnabled && plotRect().contains(event->position()))
    {
        const QRectF plot = plotRect();
        const double t = m_t0 + (m_t1 - m_t0) * (event->position().x() - plot.left()) / plot.width();
        if (!m_abHasA || m_abHasB)
        {
            m_abA = t;
            m_abHasA = true;
            m_abHasB = false;
        }
        else
        {
            m_abB = t;
            m_abHasB = true;
        }
        emit activated(m_title);
        update();
        return;
    }
    if (event->button() == Qt::LeftButton && m_rubberZoom && plotRect().contains(event->position()))
    {
        m_rubbering = true;
        m_rubberStart = event->position();
        m_rubberRect = QRectF(m_rubberStart, m_rubberStart);
        emit activated(m_title);
        update();
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

QJsonObject CanvasWidget::saveState() const
{
    QJsonObject o;
    o[QStringLiteral("title")] = m_title;
    o[QStringLiteral("t0")] = m_t0;
    o[QStringLiteral("t1")] = m_t1;
    o[QStringLiteral("yMin")] = m_yMin;
    o[QStringLiteral("yMax")] = m_yMax;
    o[QStringLiteral("y2Min")] = m_y2Min;
    o[QStringLiteral("y2Max")] = m_y2Max;
    o[QStringLiteral("autoY")] = m_autoY;
    o[QStringLiteral("legend")] = m_showLegend;
    o[QStringLiteral("grid")] = m_showGrid;
    o[QStringLiteral("box")] = m_showBox;
    o[QStringLiteral("rubberZoom")] = m_rubberZoom;
    o[QStringLiteral("mode")] = int(m_mode);
    o[QStringLiteral("modeTag")] = m_modeTag;
    QJsonArray arr;
    for (const Series &s : m_series)
    {
        QJsonObject so;
        so[QStringLiteral("name")] = s.name;
        so[QStringLiteral("label")] = s.label;
        so[QStringLiteral("unit")] = s.unit;
        so[QStringLiteral("srcPath")] = s.srcPath;
        so[QStringLiteral("color")] = s.color.name();
        so[QStringLiteral("width")] = s.width;
        so[QStringLiteral("axis")] = s.axis;
        so[QStringLiteral("visible")] = s.visible;
        so[QStringLiteral("offset")] = s.offsetSec;
        arr.append(so);
    }
    o[QStringLiteral("series")] = arr;
    return o;
}

void CanvasWidget::restoreState(const QJsonObject &o, const QHash<QString, const PsChannelInfo *> &pool)
{
    m_title = o.value(QStringLiteral("title")).toString(m_title);
    m_mode = PlotMode(std::clamp(o.value(QStringLiteral("mode")).toInt(0), 0, int(Surface3D)));
    m_modeTag = o.value(QStringLiteral("modeTag")).toString();
    m_series.clear();
    const QJsonArray arr = o.value(QStringLiteral("series")).toArray();
    for (const QJsonValue &v : arr)
    {
        const QJsonObject so = v.toObject();
        const QString name = so.value(QStringLiteral("name")).toString();
        const QString srcPath = so.value(QStringLiteral("srcPath")).toString();
        const PsChannelInfo *ch = pool.value(srcPath + QLatin1Char('|') + name,
                                            pool.value(name, nullptr));
        if (!ch)
            continue;
        Series s;
        s.name = name;
        s.label = so.value(QStringLiteral("label")).toString(name);
        s.unit = so.value(QStringLiteral("unit")).toString(QString::fromUtf8(ch->unit ? ch->unit : ""));
        s.color = QColor(so.value(QStringLiteral("color")).toString());
        if (!s.color.isValid())
            s.color = kPalette[m_series.size() % 6];
        s.width = so.value(QStringLiteral("width")).toInt(1);
        s.axis = so.value(QStringLiteral("axis")).toInt(0);
        s.visible = so.value(QStringLiteral("visible")).toBool(true);
        s.offsetSec = so.value(QStringLiteral("offset")).toDouble(0.0);
        s.t = ch->time;
        s.v = ch->value;
        s.n = ch->point_count;
        s.srcPath = srcPath;
        m_series.append(s);
    }
    // 布局里的通道在当前数据源已不存在时，清掉旧标题，避免“run1 · roll”空画布误导。
    if (m_series.isEmpty() && !arr.isEmpty())
        m_title = QStringLiteral("空画布 · 拖入通道");
    m_t0 = o.value(QStringLiteral("t0")).toDouble(m_t0);
    m_t1 = o.value(QStringLiteral("t1")).toDouble(m_t1);
    m_yMin = o.value(QStringLiteral("yMin")).toDouble(m_yMin);
    m_yMax = o.value(QStringLiteral("yMax")).toDouble(m_yMax);
    m_y2Min = o.value(QStringLiteral("y2Min")).toDouble(m_y2Min);
    m_y2Max = o.value(QStringLiteral("y2Max")).toDouble(m_y2Max);
    m_autoY = o.value(QStringLiteral("autoY")).toBool(m_autoY);
    m_showLegend = o.value(QStringLiteral("legend")).toBool(true);
    m_showGrid = o.value(QStringLiteral("grid")).toBool(true);
    m_showBox = o.value(QStringLiteral("box")).toBool(true);
    m_rubberZoom = o.value(QStringLiteral("rubberZoom")).toBool(false);
    m_kernelBacked = !m_series.isEmpty();
    update();
}

void CanvasWidget::setTimeRange(double t0, double t1)
{
    if (t1 <= t0)
        return;
    m_t0 = t0;
    m_t1 = t1;
    update();
}

void CanvasWidget::setGridVisible(bool on)
{
    m_showGrid = on;
    update();
}

void CanvasWidget::setLegendVisible(bool on)
{
    m_showLegend = on;
    update();
}

void CanvasWidget::setBoxVisible(bool on)
{
    m_showBox = on;
    update();
}

void CanvasWidget::setRubberZoomEnabled(bool on)
{
    m_rubberZoom = on;
    m_rubbering = false;
    m_rubberRect = QRectF();
    update();
}

void CanvasWidget::zoomBy(double factor)
{
    if (!(factor > 0.0))
        return;
    const double center = 0.5 * (m_t0 + m_t1);
    const double half = 0.5 * (m_t1 - m_t0) * factor;
    m_t0 = center - half;
    m_t1 = center + half;
    emit timeRangeChanged(m_t0, m_t1);
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
    if (m_rubbering)
    {
        m_rubberRect = QRectF(m_rubberStart, event->position()).normalized();
        update();
        return;
    }
    if (m_dragging)
    {
        const QRectF plot = plotRect();
        const double dx = event->position().x() - m_lastPos.x();
        const double dy = event->position().y() - m_lastPos.y();
        if (dx != 0.0)
            m_followLive = false;
        const double tShift = -dx * (m_t1 - m_t0) / plot.width();
        m_t0 += tShift;
        m_t1 += tShift;
        const double yShift = -dy * (m_yMax - m_yMin) / plot.height();
        m_yMin += yShift;
        m_yMax += yShift;
        m_y2Min += yShift;
        m_y2Max += yShift;
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
    if (m_rubbering)
    {
        m_rubbering = false;
        QRectF r = m_rubberRect.intersected(plotRect());
        if (r.width() > 6 && r.height() > 6)
        {
            const QRectF plot = plotRect();
            const double t0 = m_t0 + (m_t1 - m_t0) * (r.left() - plot.left()) / plot.width();
            const double t1 = m_t0 + (m_t1 - m_t0) * (r.right() - plot.left()) / plot.width();
            const double span = m_yMax - m_yMin;
            const double topVal = m_yMax - span * (r.top() - plot.top()) / plot.height();
            const double bottomVal = m_yMax - span * (r.bottom() - plot.top()) / plot.height();
            m_yMax = topVal;
            m_yMin = bottomVal;
            m_t0 = std::min(t0, t1);
            m_t1 = std::max(t0, t1);
            m_autoY = false;
            emit timeRangeChanged(m_t0, m_t1);
        }
        m_rubberRect = QRectF();
        update();
        return;
    }
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
