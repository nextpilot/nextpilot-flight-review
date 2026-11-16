#include "PlottingCanvas.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QWheelEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QApplication>
#include <QKeyEvent>
#include <QClipboard>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QSaveFile>
#include <QTextStream>
#include "SettingsManager.h"
#include "ThemeManager.h"
#include <QDateTime>
#include <QColorDialog>
#include <QJsonArray>
#include <QMap>
#include <QToolButton>
#include <QHBoxLayout>
#include <QSplitter>
#include <QCursor>
#include <QEnterEvent>

#include <algorithm>
#include <cmath>

#include "ps_kernel.h"

namespace {

Qt::PenStyle penStyleFor(int style)
{
    switch (style)
    {
    case 1: return Qt::DashLine;
    case 2: return Qt::DotLine;
    case 3: return Qt::DashDotLine;
    default: return Qt::SolidLine;
    }
}

void drawMarker(QPainter &p, const QPointF &pt, int marker, double size, const QColor &color)
{
    p.setPen(QPen(color, 1));
    p.setBrush(Qt::NoBrush);
    const double h = size * 0.5;
    switch (marker)
    {
    case 1: p.drawEllipse(pt, h, h); break;
    case 2: p.drawRect(QRectF(pt.x() - h, pt.y() - h, size, size)); break;
    case 3:
    {
        QPolygonF poly;
        poly << QPointF(pt.x(), pt.y() - h) << QPointF(pt.x() + h, pt.y())
             << QPointF(pt.x(), pt.y() + h) << QPointF(pt.x() - h, pt.y());
        p.drawPolygon(poly);
        break;
    }
    case 4:
    {
        QPolygonF poly;
        poly << QPointF(pt.x(), pt.y() - h) << QPointF(pt.x() + h, pt.y() + h)
             << QPointF(pt.x() - h, pt.y() + h);
        p.drawPolygon(poly);
        break;
    }
    case 5:
        p.drawLine(QPointF(pt.x() - h, pt.y()), QPointF(pt.x() + h, pt.y()));
        p.drawLine(QPointF(pt.x(), pt.y() - h), QPointF(pt.x(), pt.y() + h));
        break;
    case 6:
        p.drawLine(QPointF(pt.x() - h, pt.y() - h), QPointF(pt.x() + h, pt.y() + h));
        p.drawLine(QPointF(pt.x() - h, pt.y() + h), QPointF(pt.x() + h, pt.y() - h));
        break;
    default: break;
    }
}

constexpr double kPi = 3.14159265358979323846;

QColor colorBg()   { return ThemeManager::color(QStringLiteral("canvasBg")); }
QColor colorGrid() { return ThemeManager::color(QStringLiteral("grid")); }
QColor colorText() { return ThemeManager::color(QStringLiteral("textDim")); }
QColor colorDim()  { return ThemeManager::color(QStringLiteral("textMuted")); }
}

PlottingCanvas::PlottingCanvas(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(240, 140);
    setMouseTracking(true);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    m_title = QStringLiteral("未命名画布");
    m_kernelBacked = false;
    const auto &st = SettingsManager::instance();
    m_showGrid = st.get(ps::settings::kGridDefault);
    m_showLegend = st.get(ps::settings::kLegendDefault);
    m_timeFormat = st.get(ps::settings::kTimeFormat);
    buildHoverBar();
    addDemoCurves();
}

void PlottingCanvas::reloadDisplaySettings()
{
    const auto &st = SettingsManager::instance();
    m_timeFormat = st.get(ps::settings::kTimeFormat);
    update();
}

QString PlottingCanvas::formatTime(double t) const
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

void PlottingCanvas::addDemoCurves()
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
        s.color = ThemeManager::seriesColor(idx++);
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

void PlottingCanvas::buildHoverBar()
{
    m_hoverBar = new QWidget(this);
    m_hoverBar->setObjectName(QStringLiteral("canvasHoverBar"));
    m_hoverBar->setStyleSheet(QStringLiteral(
        "QWidget#canvasHoverBar{background:transparent;border:0;}"
        "QToolButton{background:transparent;border:0;padding:0;}"
        "QToolButton:hover{background:#26313a;border-radius:2px;}"
        "QToolButton:checked{background:#33414d;border-radius:2px;}"
        "QToolButton[danger=true]:hover{background:#3a2427;}"));
    auto *lay = new QHBoxLayout(m_hoverBar);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);

    auto addBtn = [&](const QString &icon, const QString &tip, std::function<void()> fn,
                      const QColor &tint = ThemeManager::color(QStringLiteral("text"))) {
        auto *b = new QToolButton(m_hoverBar);
        // 资源图标偏暗，按目标色重着色，浮动条在深色画布上才看得清。
        QPixmap src = QIcon(icon).pixmap(32, 32);
        QPixmap out(src.size());
        out.fill(Qt::transparent);
        QPainter ip(&out);
        ip.drawPixmap(0, 0, src);
        ip.setCompositionMode(QPainter::CompositionMode_SourceIn);
        ip.fillRect(out.rect(), tint);
        ip.end();
        b->setIcon(QIcon(out));
        b->setIconSize(QSize(14, 14));
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(20, 20);
        b->setStyleSheet(QStringLiteral(
            "QToolButton{background:transparent;border:0;padding:0;}"
            "QToolButton:hover{background:#26313a;border-radius:2px;}"));
        QObject::connect(b, &QToolButton::clicked, m_hoverBar, fn);
        lay->addWidget(b);
        return b;
    };
    addBtn(QStringLiteral(":/icons/plus.svg"), QStringLiteral("新增画布（当前页末尾）"),
           [this] { emit addRequested(); });
    addBtn(QStringLiteral(":/icons/split_h.svg"), QStringLiteral("左右拆分"),
           [this] { emit splitRequested(0); });
    addBtn(QStringLiteral(":/icons/split_v.svg"), QStringLiteral("上下拆分"),
           [this] { emit splitRequested(1); });
    m_maxBtn = addBtn(QStringLiteral(":/icons/maximize.svg"), QStringLiteral("最大化本画布"),
                      [this] { emit maximizeRequested(); });
    m_dragBtn = addBtn(QStringLiteral(":/icons/cursor.svg"), QStringLiteral("拖拽：按住移到同页其他画布完成重排"),
                       [] {});
    m_dragBtn->installEventFilter(this);
    m_dragBtn->setCursor(Qt::OpenHandCursor);
    m_abBtn = addBtn(QStringLiteral(":/icons/ruler.svg"),
                     QStringLiteral("双游标测量：点两下设置 A/B，拖动游标看 Δt/Δy"),
                     [] {}, ThemeManager::color(QStringLiteral("accent")));
    m_abBtn->setCheckable(true);
    QObject::connect(m_abBtn, &QToolButton::toggled, this, [this](bool on) {
        m_abEnabled = on;
        m_abDrag = 0;
        if (!on)
            m_abHasA = m_abHasB = false;
        setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
        update();
    });
    auto *sep = new QWidget(m_hoverBar);
    sep->setFixedWidth(4);
    lay->addWidget(sep);
    auto *del = addBtn(QStringLiteral(":/icons/trash.svg"), QStringLiteral("删除本画布"),
                       [this] { emit removeRequested(); }, ThemeManager::color(QStringLiteral("danger")));
    del->setProperty("danger", true);
    m_hoverBar->installEventFilter(this);
    for (auto *b : m_hoverBar->findChildren<QToolButton *>())
        b->installEventFilter(this);
    m_hoverBar->hide();
    positionHoverBar();
}

void PlottingCanvas::positionHoverBar()
{
    if (!m_hoverBar)
        return;
    m_hoverBar->adjustSize();
    const int w = m_hoverBar->sizeHint().width();
    m_hoverBar->move(std::max(6, width() - w - 8), 3);
    m_hoverBar->raise();
}

void PlottingCanvas::reorderByCursor(const QPoint &globalPos)
{
    auto *sp = qobject_cast<QSplitter *>(parentWidget());
    if (!sp || sp->count() < 2)
        return;
    for (int i = 0; i < sp->count(); ++i)
    {
        QWidget *w = sp->widget(i);
        if (!w || w == this)
            continue;
        const QRect g(w->mapToGlobal(QPoint(0, 0)), w->size());
        if (g.contains(globalPos))
        {
            sp->insertWidget(i, this);
            break;
        }
    }
}

void PlottingCanvas::enterEvent(QEnterEvent *event)
{
    if (m_hoverBar)
    {
        positionHoverBar();
        m_hoverBar->show();
        m_hoverBar->raise();
        update();
    }
    QWidget::enterEvent(event);
}

void PlottingCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    positionHoverBar();
}

bool PlottingCanvas::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_dragBtn)
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            m_reordering = true;
            m_dragBtn->setCursor(Qt::ClosedHandCursor);
            return true;
        }
        if (event->type() == QEvent::MouseMove && m_reordering)
        {
            reorderByCursor(QCursor::pos());
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease)
        {
            m_reordering = false;
            m_dragBtn->setCursor(Qt::OpenHandCursor);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PlottingCanvas::setTitle(const QString &title)
{
    m_title = title;
    update();
}

void PlottingCanvas::setMaximizedState(bool on)
{
    m_maximized = on;
    if (!m_maxBtn)
        return;
    m_maxBtn->setIcon(QIcon(on ? QStringLiteral(":/icons/restore.svg")
                              : QStringLiteral(":/icons/maximize.svg")));
    m_maxBtn->setToolTip(on ? QStringLiteral("还原画布") : QStringLiteral("最大化本画布"));
}

void PlottingCanvas::clearChannels()
{
    m_series.clear();
    m_kernelBacked = false;
    m_t0 = 0.0;
    m_t1 = 30.0;
    m_autoY = true;
    resetView();
}

void PlottingCanvas::setChannels(const QVector<const PsChannelInfo *> &channels)
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
        s.color = ThemeManager::seriesColor(idx++);
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

QVector<PlottingCanvas::SampleSeries> PlottingCanvas::sampleSeries() const
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

void PlottingCanvas::addGeneratedSeries(const QString &name, const QVector<double> &time,
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
    s.color = ThemeManager::seriesColor(int(m_series.size()));
    s.tOwn = time;
    s.vOwn = value;
    m_series.append(s);
    m_kernelBacked = false;
    m_autoY = true;
    resetView();
    update();
    emit activated(m_title);
}

bool PlottingCanvas::replaceGeneratedSeries(const QString &oldLabel, const QString &name,
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

void PlottingCanvas::removeGeneratedSeries(const QString &name)
{
    for (int i = m_series.size() - 1; i >= 0; --i)
        if (m_series.at(i).name == name && !m_series.at(i).t)
            m_series.removeAt(i);
    update();
    emit activated(m_title);
}

void PlottingCanvas::setInputSource(int id, const QString &path, const QString &alias, double offsetSec)
{
    m_inputSrcId = id;
    m_inputSrcPath = path;
    m_inputSrcAlias = alias;
    m_inputOffset = offsetSec;
}

void PlottingCanvas::renameSource(const QString &path, const QString &alias)
{
    for (Series &s : m_series)
        if (s.srcPath == path)
            s.label = alias + QStringLiteral(" · ") + s.name;
    update();
}

void PlottingCanvas::removeChannelsFromSource(const QString &path)
{
    for (int i = m_series.size() - 1; i >= 0; --i)
    {
        if (m_series.at(i).srcPath == path)
            m_series.remove(i);
    }
    m_kernelBacked = !m_series.isEmpty();
    update();
}

void PlottingCanvas::addChannel(const PsChannelInfo *ch)
{
    addChannelEx(ch, m_inputSrcId, m_inputSrcPath, m_inputSrcAlias, m_inputOffset);
}

void PlottingCanvas::addChannelEx(const PsChannelInfo *ch, int srcId, const QString &path,
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
    s.color = ThemeManager::seriesColor(int(m_series.size()));
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

void PlottingCanvas::removeChannel(const QString &name)
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

QJsonObject PlottingCanvas::seriesState(int index) const
{
    QJsonObject o;
    if (index < 0 || index >= m_series.size())
        return o;
    const Series &s = m_series[index];
    o[QStringLiteral("name")] = s.name;
    o[QStringLiteral("label")] = s.label;
    o[QStringLiteral("unit")] = s.unit;
    o[QStringLiteral("srcId")] = s.srcId;
    o[QStringLiteral("srcPath")] = s.srcPath;
    o[QStringLiteral("color")] = s.color.name();
    o[QStringLiteral("width")] = s.width;
    o[QStringLiteral("style")] = s.lineStyle;
    o[QStringLiteral("marker")] = s.marker;
    o[QStringLiteral("markerSize")] = s.markerSize;
    o[QStringLiteral("axis")] = s.axis;
    o[QStringLiteral("visible")] = s.visible;
    o[QStringLiteral("offset")] = s.offsetSec;
    o[QStringLiteral("points")] = int(s.count());
    return o;
}

bool PlottingCanvas::patchSeries(int index, const QJsonObject &patch)
{
    if (index < 0 || index >= m_series.size())
        return false;
    Series &s = m_series[index];
    if (patch.contains(QStringLiteral("label")))
        s.label = patch.value(QStringLiteral("label")).toString(s.label);
    if (patch.contains(QStringLiteral("color")))
    {
        const QColor c(patch.value(QStringLiteral("color")).toString());
        if (c.isValid())
            s.color = c;
    }
    if (patch.contains(QStringLiteral("width")))
        s.width = std::clamp(patch.value(QStringLiteral("width")).toInt(s.width), 1, 6);
    if (patch.contains(QStringLiteral("style")))
        s.lineStyle = std::clamp(patch.value(QStringLiteral("style")).toInt(s.lineStyle), 0, 3);
    if (patch.contains(QStringLiteral("marker")))
        s.marker = std::clamp(patch.value(QStringLiteral("marker")).toInt(s.marker), 0, 6);
    if (patch.contains(QStringLiteral("markerSize")))
        s.markerSize = std::clamp(patch.value(QStringLiteral("markerSize")).toInt(s.markerSize), 1, 12);
    if (patch.contains(QStringLiteral("axis")))
        s.axis = patch.value(QStringLiteral("axis")).toInt(s.axis) == 1 ? 1 : 0;
    if (patch.contains(QStringLiteral("visible")))
        s.visible = patch.value(QStringLiteral("visible")).toBool(s.visible);
    if (patch.contains(QStringLiteral("offset")))
        s.offsetSec = patch.value(QStringLiteral("offset")).toDouble(s.offsetSec);
    m_autoY = true;
    resetView();
    update();
    emit activated(m_title);
    return true;
}

bool PlottingCanvas::setSeriesChannel(int index, const PsChannelInfo *ch, int srcId,
                                    const QString &srcPath, const QString &alias, double offsetSec)
{
    if (index < 0 || index >= m_series.size() || !ch || ch->point_count < 2 ||
        !ch->time || !ch->value)
        return false;
    Series &s = m_series[index];
    s.name = QString::fromUtf8(ch->name);
    s.label = alias.isEmpty() ? s.name : alias + QStringLiteral(" · ") + s.name;
    s.unit = QString::fromUtf8(ch->unit ? ch->unit : "");
    s.srcId = srcId;
    s.srcPath = srcPath;
    s.offsetSec = offsetSec;
    s.t = ch->time;
    s.v = ch->value;
    s.n = ch->point_count;
    s.tOwn.clear();
    s.vOwn.clear();
    m_kernelBacked = true;
    m_autoY = true;
    resetView();
    update();
    emit activated(m_title);
    return true;
}

bool PlottingCanvas::setSeriesSamples(int index, const QVector<double> &time,
                                    const QVector<double> &value, const QString &label)
{
    if (index < 0 || index >= m_series.size() || time.isEmpty() ||
        time.size() != value.size())
        return false;
    Series &s = m_series[index];
    s.tOwn = time;
    s.vOwn = value;
    s.t = nullptr;
    s.v = nullptr;
    s.n = 0;
    if (!label.isEmpty())
        s.label = label;
    s.offsetSec = 0.0;
    m_kernelBacked = false;
    m_autoY = true;
    resetView();
    update();
    emit activated(m_title);
    return true;
}

bool PlottingCanvas::replaceChannel(const QString &oldName, const PsChannelInfo *ch)
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

void PlottingCanvas::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-plotscope-channel")))
        event->acceptProposedAction();
}

void PlottingCanvas::dropEvent(QDropEvent *event)
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

bool PlottingCanvas::hasRightAxis() const
{
    for (const Series &s : m_series)
        if (s.axis == 1)
            return true;
    return false;
}

int PlottingCanvas::seriesAxisCount() const
{
    int n = 0;
    for (const Series &s : m_series)
        n += (s.axis == 1 ? 1 : 0);
    return n;
}

QRectF PlottingCanvas::plotRect() const
{
    return QRectF(rect()).adjusted(m_yLabel.isEmpty() ? 62 : 78, 30,
                                   hasRightAxis() ? -60 : -16,
                                   m_xLabel.isEmpty() ? -26 : -42);
}

void PlottingCanvas::setFollowLive(bool on, double windowSec)
{
    m_followLive = on;
    m_liveWindow = qMax(0.5, windowSec);
    if (on)
    {
        m_autoY = true;
        resetView();
    }
}

void PlottingCanvas::ensureTimeRange()
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

void PlottingCanvas::setSourceOffset(const QString &path, double offsetSec, bool refit)
{
    int hit = 0;
    for (Series &s : m_series)
        if (s.srcPath == path)
        { s.offsetSec = offsetSec; ++hit; }
    if (refit)
        resetView();
    repaint();
}

void PlottingCanvas::resetView()
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


void PlottingCanvas::setPlotMode(PlotMode mode, const QString &tag)
{
    m_mode = mode;
    m_modeTag = tag;
    m_abEnabled = false;
    m_abHasA = m_abHasB = false;
    update();
}

double PlottingCanvas::lastValue(const QString &namePart, bool *ok) const
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

void PlottingCanvas::paintXy(QPainter &p, const QRectF &plot, const QFontMetrics &fm, bool scatter)
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

void PlottingCanvas::paintBar(QPainter &p, const QRectF &plot, const QFontMetrics &fm)
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

void PlottingCanvas::paintHistogram(QPainter &p, const QRectF &plot, const QFontMetrics &fm)
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

void PlottingCanvas::paintSpectrum(QPainter &p, const QRectF &plot, const QFontMetrics &fm)
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

void PlottingCanvas::paintHorizon(QPainter &p, const QRectF &plot, const QFontMetrics &fm)
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
    p.setBrush(ThemeManager::seriesColor(0));
    p.drawEllipse(QPointF(0, -pt * 2.0), rad, rad);
    p.setBrush(ThemeManager::seriesColor(1));
    p.drawEllipse(QPointF(0, -pt * 2.0 + rad), rad, rad);
    p.restore();
    p.setPen(QPen(ThemeManager::color(QStringLiteral("accent")), 2));
    p.drawEllipse(c, rad, rad);
    p.drawLine(QPointF(c.x() - rad, c.y()), QPointF(c.x() + rad, c.y()));
    p.drawLine(QPointF(c.x(), c.y() - rad), QPointF(c.x(), c.y() + rad));
    p.setPen(colorText());
    p.drawText(QRectF(c.x() - 120, c.y() + rad + 6, 240, fm.height()),
               Qt::AlignCenter, QStringLiteral("roll %1°   pitch %2°")
                                   .arg(r, 0, 'f', 1).arg(pt, 0, 'f', 1));
}

void PlottingCanvas::paintCompass(QPainter &p, const QRectF &plot, const QFontMetrics &fm)
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
    p.setPen(QPen(ThemeManager::color(QStringLiteral("danger")), 2));
    p.drawLine(c, c + QPointF(std::sin(a) * rad * 0.75, -std::cos(a) * rad * 0.75));
    p.setPen(colorText());
    p.drawText(QRectF(c.x() - 100, c.y() + rad + 6, 200, fm.height()),
               Qt::AlignCenter, QStringLiteral("HDG %1°").arg(h, 0, 'f', 1));
}

void PlottingCanvas::paintGauge(QPainter &p, const QRectF &plot, const QFontMetrics &fm)
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
    p.setPen(QPen(ThemeManager::seriesColor(0), 4));
    p.drawArc(arc, 225 * 16, int(-270.0 * frac * 16));
    const double a = (225.0 - 270.0 * frac) * kPi / 180.0;
    p.setPen(QPen(ThemeManager::color(QStringLiteral("accent")), 2));
    p.drawLine(c, c + QPointF(std::cos(a) * rad * 0.9, -std::sin(a) * rad * 0.9));
    p.setPen(colorText());
    p.drawText(QRectF(c.x() - 100, c.y() - fm.height() / 2, 200, fm.height()),
               Qt::AlignCenter, ok ? QString::number(v, 'g', 5) : QStringLiteral("--"));
    p.drawText(QRectF(c.x() - 100, c.y() + rad + 6, 200, fm.height()),
               Qt::AlignCenter, QStringLiteral("%1 ~ %2").arg(lo, 0, 'g', 3).arg(hi, 0, 'g', 3));
}

void PlottingCanvas::paintGpsMap(QPainter &p, const QRectF &plot, const QFontMetrics &fm)
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
    p.setPen(QPen(ThemeManager::seriesColor(0), 1.6));
    QPolygonF poly;
    for (const QPointF &q : pts)
        poly.append(QPointF(xOf(q.x()), yOf(q.y())));
    p.drawPolyline(poly);
    p.setBrush(ThemeManager::color(QStringLiteral("success")));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(xOf(pts.first().x()), yOf(pts.first().y())), 4, 4);
    p.setBrush(ThemeManager::color(QStringLiteral("danger")));
    p.drawEllipse(QPointF(xOf(pts.last().x()), yOf(pts.last().y())), 4, 4);
    p.setPen(colorDim());
    p.drawText(QPointF(plot.left() + 6, plot.top() + fm.ascent() + 2),
               QStringLiteral("%1 点 · %2 / %3").arg(pts.size()).arg(latS->label, lonS->label));
}

void PlottingCanvas::paintSurface3D(QPainter &p, const QRectF &plot, const QFontMetrics &fm)
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

void PlottingCanvas::paintTraj3D(QPainter &p, const QRectF &plot, const QFontMetrics &fm)
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

void PlottingCanvas::paintSpecial(QPainter &p, const QRectF &plot, const QFontMetrics &fm)
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

void PlottingCanvas::setPlayhead(double t)
{
    m_hasPlayhead = true;
    m_playhead = t;
    update();
}

void PlottingCanvas::clearPlayhead()
{
    m_hasPlayhead = false;
    update();
}

void PlottingCanvas::setTimeMarkers(const QVector<ps::TimeBookmark> &markers)
{
    m_timeMarkers = markers;
    if (m_hasSelectedBookmark)
    {
        bool found = false;
        for (const auto &mk : m_timeMarkers)
            if (mk.id == m_selectedBookmarkId)
                found = true;
        if (!found)
        {
            m_hasSelectedBookmark = false;
            m_selectedBookmarkId.clear();
        }
    }
    update();
}

void PlottingCanvas::paintEvent(QPaintEvent *)
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
    p.setPen(ThemeManager::color(QStringLiteral("line")));
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    const QRectF plot = plotRect();
    if (plot.width() <= 1 || plot.height() <= 1)
        return;

    QFont f = p.font();
    f.setPixelSize(13);
    f.setBold(true);
    p.setFont(f);
    p.setPen(colorText());
    const int rightLimit = (m_hoverBar && m_hoverBar->isVisible()) ? m_hoverBar->x() - 8 : width() - 10;
    const QString title = p.fontMetrics().elidedText(m_title, Qt::ElideRight,
                                                     std::max(40, rightLimit - 14));
    const int titleWidth = p.fontMetrics().horizontalAdvance(title);
    p.drawText(QPointF(14, 21), title);

    f.setBold(false);
    f.setPixelSize(m_axisFontSize);
    p.setFont(f);
    const QFontMetrics fm(p.font());
    p.setPen(colorDim());
    const QString sub = m_series.isEmpty()
                            ? QStringLiteral("空画布 · 拖字段到这里，或「文件 → 打开文件」")
                            : QStringLiteral("%1 通道 · 抽稀在 Rust 侧完成").arg(m_series.size());
    const int subX = 14 + titleWidth + 14;
    if (subX < rightLimit)
        p.drawText(QPointF(subX, 21),
                   fm.elidedText(sub, Qt::ElideRight, rightLimit - subX));

    if (m_hasPlayhead)
    {
        const double x = plot.left() + plot.width() * (m_playhead - m_t0) / (m_t1 - m_t0);
        if (x >= plot.left() && x <= plot.right())
        {
            p.setPen(QPen(ThemeManager::seriesColor(1), 1, Qt::DashLine));
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            p.setPen(ThemeManager::seriesColor(1));
            p.drawText(QPointF(std::min(x + 4, plot.right() - 40), plot.top() + fm.ascent() + 2),
                       QStringLiteral("视频"));
        }
    }

    if (m_mode != TimeSeries)
    {
        paintSpecial(p, plot, fm);
        return;
    }

    p.setPen(axisInk());
    if (!m_xLabel.isEmpty())
        p.drawText(QPointF(plot.center().x() - fm.horizontalAdvance(m_xLabel) / 2,
                           plot.bottom() + fm.ascent() + 22), m_xLabel);
    if (!m_yLabel.isEmpty())
    {
        p.save();
        p.translate(12, plot.center().y());
        p.rotate(-90);
        p.drawText(QPointF(-fm.horizontalAdvance(m_yLabel) / 2, fm.ascent() / 2), m_yLabel);
        p.restore();
    }

    // 空画布：只留网格和边框，不画无数据支撑的假刻度。
    if (m_series.isEmpty())
    {
        if (m_gridY)
        {
            p.setPen(QPen(colorGrid(), 1));
            for (int i = 1; i < 5; ++i)
            {
                const double y = plot.top() + plot.height() * i / 5.0;
                p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            }
        }
        if (m_gridX)
        {
            p.setPen(QPen(colorGrid(), 1));
            for (int i = 1; i < 6; ++i)
            {
                const double x = plot.left() + plot.width() * i / 6.0;
                p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            }
        }
        if (m_showBox)
        {
            p.setPen(QPen(axisInk(), m_axisLineWidth));
            p.drawRect(plot);
        }
        return;
    }

    // 刻度与网格分开：grid off 保留坐标读数，只去掉网格线（MATLAB 习惯）
    const int yTicks = 5;
    for (int i = 0; i <= yTicks; ++i)
    {
        const double y = plot.top() + plot.height() * i / yTicks;
        if (m_gridY)
        {
            p.setPen(QPen(colorGrid(), 1));
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        const double val = m_flipY ? m_yMin + (m_yMax - m_yMin) * i / yTicks
                                   : m_yMax - (m_yMax - m_yMin) * i / yTicks;
        p.setPen(axisInk());
        const QString lbl = QString::number(val, 'g', 4);
        p.drawText(QPointF(plot.left() - 8 - fm.horizontalAdvance(lbl), y + fm.ascent() / 2 - 1), lbl);
    }
    if (hasRightAxis())
    {
        for (int i = 0; i <= yTicks; ++i)
        {
            const double y = plot.top() + plot.height() * i / yTicks;
            const double val = m_flipY ? m_y2Min + (m_y2Max - m_y2Min) * i / yTicks
                                       : m_y2Max - (m_y2Max - m_y2Min) * i / yTicks;
            p.setPen(axisInk());
            const QString lbl = QString::number(val, 'g', 4);
            p.drawText(QPointF(plot.right() + 8, y + fm.ascent() / 2 - 1), lbl);
        }
    }

    const int xTicks = 6;
    for (int i = 0; i <= xTicks; ++i)
    {
        const double x = plot.left() + plot.width() * i / xTicks;
        const double t = m_t0 + (m_t1 - m_t0) * i / xTicks;
        if (m_gridX)
        {
            p.setPen(QPen(colorGrid(), 1));
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }
        p.setPen(axisInk());
        const QString lbl = formatTime(t);
        p.drawText(QPointF(x - fm.horizontalAdvance(lbl) / 2, plot.bottom() + fm.ascent() + 6), lbl);
    }
    if (m_minorGrid)
    {
        p.setPen(QPen(colorGrid(), 1, Qt::DotLine));
        for (int i = 0; i < yTicks; ++i)
        {
            const double y = plot.top() + plot.height() * (2 * i + 1) / (2.0 * yTicks);
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        for (int i = 0; i < xTicks; ++i)
        {
            const double x = plot.left() + plot.width() * (2 * i + 1) / (2.0 * xTicks);
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }
    }
    if (m_showBox)
    {
        p.setPen(QPen(axisInk(), m_axisLineWidth));
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
        return m_flipY ? plot.top() + plot.height() * (v - lo) / (hi - lo)
                       : plot.bottom() - plot.height() * (v - lo) / (hi - lo);
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
            p.setPen(QPen(s.color, s.width + (si == m_selectedSeries ? 2 : 0),
                          penStyleFor(s.lineStyle)));  // 1px 走快速 Bresenham；>1 才走宽线轮廓器
            p.drawPolyline(poly);
            if (s.marker > 0 && s.count() <= 4000)
            {
                p.setRenderHint(QPainter::Antialiasing, true);
                for (quint32 i = 0; i < s.count(); ++i)
                    drawMarker(p, QPointF(toX(s.time()[i] - s.offsetSec),
                                          toY(s.value()[i], s.axis)),
                               s.marker, s.markerSize, s.color);
                p.setRenderHint(QPainter::Antialiasing, false);
            }
            m_hitPaths.append(poly);
            m_hitOwner.append(si);
        }
    }
    p.restore();
    p.setRenderHint(QPainter::Antialiasing, true);

    if (m_showLegend)
    {
        // 图例（带单位）：点击切换显隐，右键删除
        m_legendRects.clear();
        double totalW = 0.0;
        for (const Series &s : m_series)
        {
            const QString lbl0 = s.unit.isEmpty() ? s.label : QStringLiteral("%1 [%2]").arg(s.label, s.unit);
            const QString lbl = s.axis == 1 ? lbl0 + QStringLiteral(" →右") : lbl0;
            totalW += 19 + fm.horizontalAdvance(lbl) + 20;
        }
        double lx = (m_legendPos == 1 || m_legendPos == 3)
                        ? std::max(plot.left() + 6.0, plot.right() - totalW - 10)
                        : plot.left() + 6;
        const double ly = (m_legendPos == 2 || m_legendPos == 3)
                              ? plot.bottom() - 8
                              : plot.top() + 14;
        for (int i = 0; i < m_series.size(); ++i)
        {
            const Series &s = m_series.at(i);
            const QString lbl0 = s.unit.isEmpty() ? s.label : QStringLiteral("%1 [%2]").arg(s.label, s.unit);
            const QString lbl = s.axis == 1 ? lbl0 + QStringLiteral(" →右") : lbl0;
            const double w = 19 + fm.horizontalAdvance(lbl) + 12;
            m_legendRects.append(QRectF(lx - 2, ly - 12, w, 18));
            p.setPen(QPen(s.color, 1.2, penStyleFor(s.lineStyle)));
            p.drawLine(QPointF(lx, ly - 4), QPointF(lx + 14, ly - 4));
            p.setPen(s.visible ? colorText() : colorDim());
            p.drawText(QPointF(lx + 19, ly), lbl);
            lx += w + 8;
        }

    }

    // 双游标测量：两条竖线 + 区间带，逐曲线给出 Δt / Δy / 斜率
    if (m_abEnabled)
    {
        const auto xOf = [&](double t) { return plot.left() + plot.width() * (t - m_t0) / (m_t1 - m_t0); };
        if (!m_abHasA)
        {
            const QString hint = QStringLiteral("点击图表设置游标 A，再点一下设置 B");
            const QRectF hr(plot.left() + 10, plot.top() + 4, 250, 18);
            p.setPen(Qt::NoPen);
            p.setBrush(ThemeManager::alphaColor(QStringLiteral("panel"), 210));
            p.drawRoundedRect(hr, 3, 3);
            p.setPen(ThemeManager::color(QStringLiteral("accent")));
            p.drawText(hr.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, hint);
        }
        else
        {
            const double xa = xOf(m_abA);
            const double xb = m_abHasB ? xOf(m_abB) : xa;
            if (m_abHasB)
            {
                p.setPen(Qt::NoPen);
                p.setBrush(ThemeManager::alphaColor(QStringLiteral("accent"), 24));
                p.drawRect(QRectF(QPointF(qMin(xa, xb), plot.top()),
                                  QPointF(qMax(xa, xb), plot.bottom())));

                const double dt = m_abB - m_abA;
                const QString dtText = QStringLiteral("ΔX %1 s").arg(dt, 0, 'f', 4);
                const int tw = fm.horizontalAdvance(dtText) + 20;
                QRectF badge(QPointF((xa + xb) * 0.5 - tw * 0.5, plot.top() + 5), QSizeF(tw, 20));
                if (badge.left() < plot.left() + 4)
                    badge.moveLeft(plot.left() + 4);
                if (badge.right() > plot.right() - 4)
                    badge.moveRight(plot.right() - 4);
                p.setPen(Qt::NoPen);
                p.setBrush(ThemeManager::alphaColor(QStringLiteral("accent"), 235));
                p.drawRoundedRect(badge, 3, 3);
                p.setPen(ThemeManager::color(QStringLiteral("bg")));
                p.drawText(badge, Qt::AlignCenter, dtText);
            }

            p.setPen(QPen(ThemeManager::color(QStringLiteral("accent")), 1.4, Qt::DashLine));
            p.drawLine(QPointF(xa, plot.top()), QPointF(xa, plot.bottom()));
            p.setBrush(ThemeManager::color(QStringLiteral("accent")));
            p.drawEllipse(QPointF(xa, plot.top() + 1), 3, 3);
            p.setPen(ThemeManager::color(QStringLiteral("accent")));
            {
                const QString al = QStringLiteral("A  %1 s").arg(m_abA, 0, 'f', 3);
                QRectF ar(xa + 4, plot.top() + 26, fm.horizontalAdvance(al) + 10, fm.height() + 2);
                if (ar.right() > plot.right() - 4)
                    ar.moveRight(xa - 4);
                p.setPen(Qt::NoPen);
                p.setBrush(ThemeManager::alphaColor(QStringLiteral("panel"), 220));
                p.drawRoundedRect(ar, 2, 2);
                p.setPen(ThemeManager::color(QStringLiteral("accent")));
                p.drawText(ar, Qt::AlignCenter, al);
            }

            if (m_abHasB)
            {
                const double xb = xOf(m_abB);
                p.setPen(QPen(ThemeManager::seriesColor(0), 1.4, Qt::DashLine));
                p.drawLine(QPointF(xb, plot.top()), QPointF(xb, plot.bottom()));
                p.setBrush(ThemeManager::seriesColor(0));
                p.drawEllipse(QPointF(xb, plot.top() + 1), 3, 3);
                p.setPen(ThemeManager::seriesColor(0));
                {
                    const QString bl = QStringLiteral("B  %1 s").arg(m_abB, 0, 'f', 3);
                    QRectF br(xb + 4, plot.top() + 26, fm.horizontalAdvance(bl) + 10, fm.height() + 2);
                    if (br.right() > plot.right() - 4)
                        br.moveRight(xb - 4);
                    p.setPen(Qt::NoPen);
                    p.setBrush(ThemeManager::alphaColor(QStringLiteral("panel"), 220));
                    p.drawRoundedRect(br, 2, 2);
                    p.setPen(ThemeManager::seriesColor(0));
                    p.drawText(br, Qt::AlignCenter, bl);
                }

                // 取第一条可见曲线，画出两点之间的直角三角形，Δy 一眼可见。
                for (const Series &sr : m_series)
                {
                    if (!sr.visible)
                        continue;
                    bool oa = false, ob = false;
                    const double va = sampleAt(sr, m_abA, &oa);
                    const double vb = sampleAt(sr, m_abB, &ob);
                    if (!oa || !ob)
                        break;
                    const double lo = sr.axis == 1 ? m_y2Min : m_yMin;
                    const double hi = sr.axis == 1 ? m_y2Max : m_yMax;
                    const auto yOf = [&](double v) {
                        return plot.bottom() - plot.height() * (v - lo) / (hi - lo);
                    };
                    const QPointF pa(xa, yOf(va));
                    const QPointF pb(xb, yOf(vb));
                    p.setPen(QPen(ThemeManager::alphaColor(QStringLiteral("accent"), 150), 1, Qt::DashLine));
                    p.drawLine(pa, QPointF(xb, pa.y()));
                    p.drawLine(QPointF(xb, pa.y()), pb);
                    p.drawLine(pa, pb);
                    p.setPen(ThemeManager::color(QStringLiteral("accent")));
                    const QString dyl = QStringLiteral("Δy %1").arg(vb - va, 0, 'g', 5);
                    p.drawText(QPointF(xb + 4, (pa.y() + pb.y()) * 0.5), dyl);
                    break;
                }

                const double dt = m_abB - m_abA;
                QStringList rows;
                QList<QColor> colors;
                for (const Series &sr : m_series)
                {
                    if (!sr.visible)
                        continue;
                    bool oa = false, ob = false;
                    const double va = sampleAt(sr, m_abA, &oa);
                    const double vb = sampleAt(sr, m_abB, &ob);
                    const QString lbl = fm.elidedText(sr.label, Qt::ElideMiddle, 118);
                    const QString unit = sr.unit.isEmpty() ? QString() : QStringLiteral(" ") + sr.unit;
                    const QString dy = (oa && ob) ? QString::number(vb - va, 'g', 6) : QStringLiteral("--");
                    const QString k = (oa && ob && qAbs(dt) > 1e-12)
                                          ? QString::number((vb - va) / dt, 'g', 5) + QStringLiteral("/s")
                                          : QStringLiteral("--");
                    rows << QStringLiteral("%1  Δy %2%3   k %4").arg(lbl, -12).arg(dy, -8).arg(unit, k);
                    colors << sr.color;
                    if (rows.size() >= 6)
                        break;
                }
                const int lineH = fm.height() + 2;
                const int boxH = 40 + rows.size() * lineH + 6;
                QRectF box(plot.left() + 10, plot.bottom() - boxH - 8, 320, boxH);
                p.setPen(Qt::NoPen);
                p.setBrush(ThemeManager::alphaColor(QStringLiteral("panel"), 238));
                p.drawRoundedRect(box, 3, 3);
                p.setPen(ThemeManager::color(QStringLiteral("accent")));
                p.drawText(QRectF(box.left() + 8, box.top() + 3, box.width() - 16, lineH),
                           Qt::AlignVCenter | Qt::AlignLeft,
                           QStringLiteral("A→B  Δx = %1 s（时间轴）").arg(dt, 0, 'f', 4));
                p.setPen(ThemeManager::color(QStringLiteral("textDim")));
                p.drawLine(QPointF(box.left() + 7, box.top() + 21),
                           QPointF(box.right() - 7, box.top() + 21));
                int yy = int(box.top()) + 22;
                for (int i = 0; i < rows.size(); ++i)
                {
                    p.setPen(colors.value(i, colorText()));
                    p.drawText(QRectF(box.left() + 8, yy, box.width() - 16, lineH),
                               Qt::AlignVCenter | Qt::AlignLeft, rows.at(i));
                    yy += lineH;
                }
            }
        }
    }

    if (m_hover && plot.contains(m_cursor))
    {
        p.setPen(QPen(ThemeManager::alphaColor(QStringLiteral("line"), 60), 1, Qt::DashLine));
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
        p.setBrush(ThemeManager::alphaColor(QStringLiteral("panel"), 238));
        p.drawRoundedRect(box, 3, 3);

        p.setPen(ThemeManager::color(QStringLiteral("accent")));
        p.drawText(QRectF(box.left() + 8, box.top() + 3, box.width() - 16, lineH),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("t = %1").arg(formatTime(t)));
        p.setPen(ThemeManager::color(QStringLiteral("textDim")));
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

    // 时间书签：所有页共用一组；视窗内画竖线+顶部书签，视窗外在左右边缘给最近书签提示。
    m_markerRects.clear();
    if (!m_timeMarkers.isEmpty())
    {
        int lane = 0;
        double nearestLeft = -1e300;
        double nearestRight = 1e300;
        QString nearestLeftId;
        QString nearestRightId;
        for (const auto &mk : m_timeMarkers)
        {
            const double t = mk.t;
            const double x = plot.left() + plot.width() * (t - m_t0) / (m_t1 - m_t0);
            if (x < plot.left())
            {
                if (t > nearestLeft)
                {
                    nearestLeft = t;
                    nearestLeftId = mk.id;
                }
                continue;
            }
            if (x > plot.right())
            {
                if (t < nearestRight)
                {
                    nearestRight = t;
                    nearestRightId = mk.id;
                }
                continue;
            }
            const QString label = mk.label.trimmed().isEmpty() ? formatTime(t) : mk.label;
            const QString text = fm.elidedText(QStringLiteral("%1  %2").arg(label, formatTime(t)),
                                               Qt::ElideRight, 180);
            p.setPen(QPen(ThemeManager::alphaColor(QStringLiteral("accent"), 150), 1, Qt::DashLine));
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            const int tw = fm.horizontalAdvance(text) + 10;
            QRectF chip(x + 3, plot.top() + 2 + (lane % 3) * 17, tw, 16);
            if (chip.right() > plot.right())
                chip.moveLeft(x - 3 - tw);
            if (chip.left() < plot.left())
                chip.moveLeft(plot.left() + 3);
            const bool selected = m_hasSelectedBookmark && mk.id == m_selectedBookmarkId;
            p.setPen(QPen(ThemeManager::color(QStringLiteral("border")), selected ? 2 : 1));
            p.setBrush(ThemeManager::alphaColor(QStringLiteral("panel"), selected ? 255 : 235));
            p.drawRoundedRect(chip, 2, 2);
            if (selected)
            {
                p.setPen(QPen(ThemeManager::color(QStringLiteral("accent")), 1));
                p.drawRoundedRect(chip.adjusted(1, 1, -1, -1), 2, 2);
            }
            p.setPen(ThemeManager::color(QStringLiteral("accent")));
            p.drawText(chip.adjusted(5, 0, -5, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
            m_markerRects.append({mk.id, chip});
            ++lane;
        }
        const auto edgeChip = [&](const QString &id, double t, bool left) {
            if (t < -1e299 || t > 1e299 || id.isEmpty())
                return;
            const QString label = formatTime(t);
            const int tw = fm.horizontalAdvance(label) + 24;
            QRectF chip(left ? QRectF(plot.left() + 2, plot.bottom() - 19, tw, 16)
                             : QRectF(plot.right() - tw - 2, plot.bottom() - 19, tw, 16));
            p.setPen(QPen(ThemeManager::color(QStringLiteral("border")), 1));
            p.setBrush(ThemeManager::alphaColor(QStringLiteral("panel"), 220));
            p.drawRoundedRect(chip, 2, 2);
            p.setPen(ThemeManager::alphaColor(QStringLiteral("accent"), 200));
            p.drawText(chip, Qt::AlignCenter, left ? QStringLiteral("◂ ") + label : label + QStringLiteral(" ▸"));
            m_markerRects.append({id, chip});
        };
        edgeChip(nearestLeftId, nearestLeft, true);
        edgeChip(nearestRightId, nearestRight, false);
    }

    if (m_rubbering && m_rubberRect.isValid())
    {
        p.setPen(QPen(ThemeManager::alphaColor(QStringLiteral("accent"), 220), 1, Qt::DashLine));
        p.setBrush(ThemeManager::alphaColor(QStringLiteral("accent"), 40));
        p.drawRect(m_rubberRect);
        p.setBrush(Qt::NoBrush);
    }

    if (m_rightBoxing && m_rightRect.isValid())
    {
        p.setPen(QPen(ThemeManager::alphaColor(QStringLiteral("accent"), 230), 1, Qt::DashLine));
        p.setBrush(ThemeManager::alphaColor(QStringLiteral("accent"), 45));
        p.drawRect(m_rightRect);
        p.setBrush(Qt::NoBrush);
    }
}

void PlottingCanvas::exportPng()
{
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("导出画布 PNG"), {},
                                                   QStringLiteral("PNG (*.png)"));
    if (f.isEmpty())
        return;
    if (grab().save(f))
        return;
    QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("无法写入 %1").arg(f));
}

void PlottingCanvas::exportCsv()
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

double PlottingCanvas::sampleAt(const Series &s, double displayT, bool *ok) const
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

int PlottingCanvas::seriesAt(const QPointF &pos) const
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

PlottingCanvas::AxisZone PlottingCanvas::axisZoneAt(const QPointF &pos) const
{
    const QRectF plot = plotRect();
    if (plot.contains(pos))
        return NoAxis;
    if (pos.x() >= plot.left() && pos.x() <= plot.right()
        && pos.y() > plot.bottom() && pos.y() <= rect().bottom() + 1)
        return XAxis;
    if (pos.y() >= plot.top() && pos.y() <= plot.bottom())
    {
        if (pos.x() >= rect().left() && pos.x() < plot.left())
            return YAxisLeft;
        if (hasRightAxis() && pos.x() > plot.right() && pos.x() <= rect().right())
            return YAxisRight;
    }
    return NoAxis;
}

double PlottingCanvas::timeAtX(double x) const
{
    const QRectF plot = plotRect();
    const double t = m_t0 + (m_t1 - m_t0) * (x - plot.left()) / qMax(1.0, plot.width());
    return qBound(m_t0, t, m_t1);
}

int PlottingCanvas::markerAt(const QPointF &pos) const
{
    for (const auto &hit : m_markerRects)
    {
        if (!hit.second.contains(pos))
            continue;
        for (int i = 0; i < m_timeMarkers.size(); ++i)
            if (m_timeMarkers.at(i).id == hit.first)
                return i;
    }
    return -1;
}

void PlottingCanvas::selectBookmark(int index)
{
    if (index < 0 || index >= m_timeMarkers.size())
        return;
    m_hasSelectedBookmark = true;
    m_selectedBookmarkId = m_timeMarkers.at(index).id;
    update();
}

void PlottingCanvas::clearBookmarkSelection()
{
    m_hasSelectedBookmark = false;
    m_selectedBookmarkId.clear();
    update();
}

void PlottingCanvas::jumpToBookmark(int index)
{
    if (index < 0 || index >= m_timeMarkers.size())
        return;
    const double span = qMax(1e-6, m_t1 - m_t0);
    const double t = m_timeMarkers.at(index).t;
    m_t0 = t - span / 2.0;
    m_t1 = t + span / 2.0;
    selectBookmark(index);
    emit timeRangeChanged(m_t0, m_t1);
    update();
}

void PlottingCanvas::createMeasurementAt(double t)
{
    const double span = qMax(1e-6, m_t1 - m_t0);
    const double gap = span * 0.1;
    double a = t - gap / 2.0;
    double b = t + gap / 2.0;
    if (a < m_t0)
    {
        a = m_t0;
        b = qMin(m_t1, a + gap);
    }
    if (b > m_t1)
    {
        b = m_t1;
        a = qMax(m_t0, b - gap);
    }
    m_abA = a;
    m_abB = b;
    m_abEnabled = true;
    m_abHasA = true;
    m_abHasB = true;
    m_abDrag = 0;
    if (m_abBtn)
        m_abBtn->setChecked(true);
    update();
}

void PlottingCanvas::clearMeasurement()
{
    m_abEnabled = false;
    m_abHasA = false;
    m_abHasB = false;
    m_abDrag = 0;
    if (m_abBtn)
        m_abBtn->setChecked(false);
    update();
}

void PlottingCanvas::resetXAxis()
{
    ensureTimeRange();
    emit timeRangeChanged(m_t0, m_t1);
    update();
}

void PlottingCanvas::resetYAxis()
{
    if (m_series.isEmpty())
        return;
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
    m_autoY = true;
    update();
}

void PlottingCanvas::applyBoxZoom(const QRectF &r, AxisZone zone, const QPointF &start,
                                  const QPointF &end)
{
    const QRectF plot = plotRect();
    const QRectF rr = r.normalized();
    const bool xOnly = zone == XAxis;
    const bool yOnly = zone == YAxisLeft || zone == YAxisRight;
    const double x0 = qMax(plot.left(), qMin(rr.left(), plot.right()));
    const double x1 = qMax(plot.left(), qMin(rr.right(), plot.right()));
    const double y0 = qMax(plot.top(), qMin(rr.top(), plot.bottom()));
    const double y1 = qMax(plot.top(), qMin(rr.bottom(), plot.bottom()));
    if (xOnly && x1 - x0 <= 6.0)
        return;
    if (yOnly && y1 - y0 <= 6.0)
        return;
    if (!xOnly && !yOnly && (x1 - x0 <= 6.0 || y1 - y0 <= 6.0))
        return;

    const bool revX = end.x() < start.x();
    const bool revY = end.y() < start.y();
    m_followLive = false;

    if (xOnly)
    {
        if (revX && x1 - x0 > plot.width() * 0.7)
        {
            resetXAxis();
            return;
        }
        const double tcx = timeAtX((x0 + x1) * 0.5);
        const double factor = revX ? plot.width() / (x1 - x0) : (x1 - x0) / plot.width();
        const double tSpan = (m_t1 - m_t0) * factor;
        m_t0 = tcx - tSpan / 2.0;
        m_t1 = tcx + tSpan / 2.0;
        emit timeRangeChanged(m_t0, m_t1);
        update();
        return;
    }

    if (yOnly)
    {
        if (revY && y1 - y0 > plot.height() * 0.7)
        {
            resetYAxis();
            return;
        }
        const double cy = (y0 + y1) * 0.5;
        const double ycy = m_yMax - (m_yMax - m_yMin) * (cy - plot.top()) / plot.height();
        const double factor = revY ? plot.height() / (y1 - y0) : (y1 - y0) / plot.height();
        const double ySpan = (m_yMax - m_yMin) * factor;
        m_yMin = ycy - ySpan / 2.0;
        m_yMax = ycy + ySpan / 2.0;
        if (hasRightAxis())
        {
            const double y2cy = m_y2Max - (m_y2Max - m_y2Min) * (cy - plot.top()) / plot.height();
            const double y2Span = (m_y2Max - m_y2Min) * factor;
            m_y2Min = y2cy - y2Span / 2.0;
            m_y2Max = y2cy + y2Span / 2.0;
        }
        m_autoY = false;
        update();
        return;
    }

    if (revX && revY && (x1 - x0) * (y1 - y0) > plot.width() * plot.height() * 0.7)
    {
        m_autoY = true;
        resetView();
        update();
        return;
    }

    const bool out = revX && revY;
    const double tcx = timeAtX((x0 + x1) * 0.5);
    const double tFactor = out ? plot.width() / (x1 - x0) : (x1 - x0) / plot.width();
    const double tSpan = (m_t1 - m_t0) * tFactor;
    m_t0 = tcx - tSpan / 2.0;
    m_t1 = tcx + tSpan / 2.0;

    const double cy = (y0 + y1) * 0.5;
    const double ycy = m_yMax - (m_yMax - m_yMin) * (cy - plot.top()) / plot.height();
    const double yFactor = out ? plot.height() / (y1 - y0) : (y1 - y0) / plot.height();
    const double ySpan = (m_yMax - m_yMin) * yFactor;
    m_yMin = ycy - ySpan / 2.0;
    m_yMax = ycy + ySpan / 2.0;
    if (hasRightAxis())
    {
        const double y2cy = m_y2Max - (m_y2Max - m_y2Min) * (cy - plot.top()) / plot.height();
        const double y2Span = (m_y2Max - m_y2Min) * yFactor;
        m_y2Min = y2cy - y2Span / 2.0;
        m_y2Max = y2cy + y2Span / 2.0;
    }
    m_autoY = false;
    emit timeRangeChanged(m_t0, m_t1);
    update();
}

void PlottingCanvas::showLegendMenu(int index, const QPoint &globalPos)
{
    if (index < 0 || index >= m_series.size())
        return;
    QMenu menu(this);
    QAction *hide = menu.addAction(m_series.at(index).visible ? QStringLiteral("隐藏曲线")
                                                               : QStringLiteral("显示曲线"));
    QAction *del = menu.addAction(QStringLiteral("删除曲线"));
    QAction *chosen = menu.exec(globalPos);
    if (chosen == hide)
        toggleChannel(index);
    else if (chosen == del)
        removeChannel(index);
}

void PlottingCanvas::showSeriesMenu(int index, const QPoint &globalPos)
{
    if (index < 0 || index >= m_series.size())
        return;
    Series &s = m_series[index];
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
    QAction *chosen = menu.exec(globalPos);
    if (chosen == vis)
        toggleChannel(index);
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
        removeChannel(index);
}

void PlottingCanvas::showCanvasMenu(double t, int markerIndex, AxisZone zone,
                                    const QPoint &globalPos)
{
    QMenu menu(this);
    QAction *addMarker = menu.addAction(
        QStringLiteral("在 %1 添加时间书签…").arg(formatTime(t)));
    QAction *delMarker = nullptr;
    if (markerIndex >= 0 && markerIndex < m_timeMarkers.size())
    {
        QString label = m_timeMarkers.at(markerIndex).label.trimmed();
        if (label.isEmpty())
            label = formatTime(m_timeMarkers.at(markerIndex).t);
        delMarker = menu.addAction(QStringLiteral("删除书签「%1」").arg(label));
    }
    menu.addSeparator();
    QAction *addCurve = menu.addAction(QStringLiteral("添加曲线…"));
    menu.addSeparator();
    QAction *reset = menu.addAction(QStringLiteral("重置缩放"));
    QAction *fit = menu.addAction(QStringLiteral("自适应 Y"));
    QAction *axisReset = nullptr;
    if (zone == XAxis)
        axisReset = menu.addAction(QStringLiteral("重置 X 轴"));
    else if (zone == YAxisLeft || zone == YAxisRight)
        axisReset = menu.addAction(QStringLiteral("自适应 Y 轴"));
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
    QAction *clearAb = nullptr;
    if (m_abEnabled || m_abHasA)
        clearAb = menu.addAction(QStringLiteral("清除 A/B 测量"));
    menu.addSeparator();
    QAction *splitH = menu.addAction(QStringLiteral("向右拆分画布"));
    QAction *splitV = menu.addAction(QStringLiteral("向下拆分画布"));
    QAction *png = menu.addAction(QStringLiteral("导出 PNG…"));
    QAction *csv = menu.addAction(QStringLiteral("导出当前画布 CSV…"));
    menu.addSeparator();
    QAction *del = menu.addAction(QStringLiteral("删除此画布"));
    QAction *chosen = menu.exec(globalPos);
    if (chosen == addMarker)
        emit markerRequested(t);
    else if (delMarker && chosen == delMarker)
        emit markerRemoveRequested(m_timeMarkers.at(markerIndex).id);
    else if (chosen == addCurve)
        emit addChannelRequested();
    else if (chosen == reset)
    {
        m_autoY = true;
        resetView();
        update();
    }
    else if (chosen == fit)
        requestAutoY();
    else if (axisReset && chosen == axisReset)
    {
        if (zone == XAxis)
            resetXAxis();
        else
            resetYAxis();
    }
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
        if (ab->isChecked())
            m_abEnabled = true;
        else
            clearMeasurement();
        if (m_abBtn)
            m_abBtn->setChecked(m_abEnabled);
        update();
    }
    else if (clearAb && chosen == clearAb)
        clearMeasurement();
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
}

void PlottingCanvas::showAxisMenu(AxisZone zone, const QPoint &globalPos)
{
    QMenu menu(this);
    QAction *reset = menu.addAction(zone == XAxis ? QStringLiteral("重置 X 轴")
                                                  : QStringLiteral("自适应 Y 轴"));
    QAction *resetAll = menu.addAction(QStringLiteral("重置整个视图"));
    menu.addSeparator();
    QAction *addMarker = menu.addAction(QStringLiteral("添加时间书签…"));
    QAction *chosen = menu.exec(globalPos);
    if (chosen == reset)
    {
        if (zone == XAxis)
            resetXAxis();
        else
            resetYAxis();
    }
    else if (chosen == resetAll)
    {
        m_autoY = true;
        resetView();
        update();
    }
    else if (chosen == addMarker)
    {
        const QRectF plot = plotRect();
        emit markerRequested(timeAtX((plot.left() + plot.right()) * 0.5));
    }
}


void PlottingCanvas::wheelEvent(QWheelEvent *event)
{
    m_followLive = false;
    const QRectF plot = plotRect();
    const AxisZone zone = axisZoneAt(event->position());
    if (zone == NoAxis && !plot.contains(event->position()))
        return;
    const int steps = event->angleDelta().y();
    if (steps == 0)
        return;

    const double factor = std::pow(0.85, steps / 120.0);
    const QPointF pos = event->position();
    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    const bool zoomY = zone == YAxisLeft || zone == YAxisRight
                       || (zone == NoAxis && ctrl);
    if (zoomY)
    {
        const double y = qBound(plot.top(), pos.y(), plot.bottom());
        const double anchor = m_yMax - (m_yMax - m_yMin) * (y - plot.top()) / plot.height();
        m_yMin = anchor + (m_yMin - anchor) * factor;
        m_yMax = anchor + (m_yMax - anchor) * factor;
        if (hasRightAxis())
        {
            m_y2Min = anchor + (m_y2Min - anchor) * factor;
            m_y2Max = anchor + (m_y2Max - anchor) * factor;
        }
        m_autoY = false;
    }
    else
    {
        const double x = qBound(plot.left(), pos.x(), plot.right());
        const double tc = timeAtX(x);
        const double span = std::clamp((m_t1 - m_t0) * factor, 1e-6, 1e9);
        const double ratio = span / std::max(1e-12, m_t1 - m_t0);
        m_t0 = tc - (tc - m_t0) * ratio;
        m_t1 = m_t0 + span;
        emit timeRangeChanged(m_t0, m_t1);
    }
    update();
    event->accept();
}

void PlottingCanvas::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    const QRectF plot = plotRect();

    // 右键只记录命中，菜单和框选都等到 release 再决定。
    if (event->button() == Qt::RightButton)
    {
        m_rightPressed = true;
        m_rightBoxing = false;
        m_rightStart = event->position();
        m_rightRect = QRectF(m_rightStart, m_rightStart);
        m_rightHit = RightBlank;
        m_rightHitIndex = -1;
        m_rightMarkerIndex = markerAt(event->position());
        m_rightAxis = axisZoneAt(event->position());
        int legend = -1;
        for (int i = 0; i < m_legendRects.size(); ++i)
            if (m_legendRects.at(i).contains(event->position()))
                legend = i;
        bool abHit = false;
        if (m_abEnabled && plot.contains(event->position()))
        {
            const auto xOf = [&](double t) {
                return plot.left() + plot.width() * (t - m_t0) / (m_t1 - m_t0);
            };
            abHit = (m_abHasA && qAbs(event->position().x() - xOf(m_abA)) <= 10)
                    || (m_abHasB && qAbs(event->position().x() - xOf(m_abB)) <= 10);
        }
        if (m_rightMarkerIndex >= 0)
            m_rightHit = RightMarker;
        else if (abHit)
            m_rightHit = RightAb;
        else
        {
            const int series = seriesAt(event->position());
            if (series >= 0)
            {
                m_rightHit = RightSeries;
                m_rightHitIndex = series;
            }
            else if (legend >= 0)
            {
                m_rightHit = RightLegend;
                m_rightHitIndex = legend;
            }
            else if (m_rightAxis != NoAxis)
                m_rightHit = RightAxis;
        }
        m_rightCanBox = m_rightHit != RightLegend
                        && (plot.contains(event->position()) || m_rightAxis != NoAxis);
        return;
    }

    // 标题栏按住可拖动重排。
    if (event->button() == Qt::LeftButton && event->position().y() < 30)
    {
        m_titleDrag = true;
        m_titleDragStart = event->position();
        if (m_hoverBar)
            m_hoverBar->hide();
        update();
        return;
    }

    // 中键、空格+左键：统一进入平移；轴区只平移对应轴。
    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && m_spacePan))
    {
        const AxisZone zone = axisZoneAt(event->position());
        if (plot.contains(event->position()) || zone != NoAxis)
        {
            m_dragging = true;
            m_dragAxis = zone;
            m_lastPos = event->position();
            setCursor(Qt::ClosedHandCursor);
            emit activated(m_title);
            return;
        }
    }

    int legend = -1;
    for (int i = 0; i < m_legendRects.size(); ++i)
        if (m_legendRects.at(i).contains(event->position()))
            legend = i;
    if (event->button() == Qt::LeftButton && legend >= 0)
    {
        m_selectedSeries = legend;
        emit seriesSelected(legend);
        emit activated(m_title);
        update();
        return;
    }

    const int marker = markerAt(event->position());

    // Shift+左键：时间书签。命中已有书签则选中，不重复添加。
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ShiftModifier)
        && plot.contains(event->position()) && !m_spacePan)
    {
        if (marker >= 0)
            selectBookmark(marker);
        else
            emit markerRequested(timeAtX(event->position().x()));
        emit activated(m_title);
        return;
    }

    // Ctrl+左键：直接生成一组 A/B。
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier)
        && plot.contains(event->position()) && !m_spacePan)
    {
        createMeasurementAt(timeAtX(event->position().x()));
        emit activated(m_title);
        return;
    }

    // 左键按住时间书签：选中，移动由 mouseMoveEvent 处理。
    if (event->button() == Qt::LeftButton && marker >= 0 && !m_spacePan)
    {
        m_bookmarkDragIndex = marker;
        m_bookmarkDragId = m_timeMarkers.at(marker).id;
        m_bookmarkDragStart = event->position();
        m_bookmarkDragActive = false;
        selectBookmark(marker);
        setCursor(Qt::ClosedHandCursor);
        emit activated(m_title);
        return;
    }

    // A/B 线可见时，左键命中线即进入拖动，不需要再按 Ctrl。
    if (event->button() == Qt::LeftButton && m_abEnabled && plot.contains(event->position()))
    {
        const auto xOf = [&](double t) {
            return plot.left() + plot.width() * (t - m_t0) / (m_t1 - m_t0);
        };
        if (m_abHasA && qAbs(event->position().x() - xOf(m_abA)) <= 10)
        {
            m_abDrag = 1;
            setCursor(Qt::SizeHorCursor);
            return;
        }
        if (m_abHasB && qAbs(event->position().x() - xOf(m_abB)) <= 10)
        {
            m_abDrag = 2;
            setCursor(Qt::SizeHorCursor);
            return;
        }
    }

    if (event->button() == Qt::LeftButton && plot.contains(event->position())
        && (m_rubberZoom || m_tool == ZoomInTool || m_tool == ZoomOutTool))
    {
        m_rubbering = true;
        m_rubberStart = event->position();
        m_rubberRect = QRectF(m_rubberStart, m_rubberStart);
        emit activated(m_title);
        update();
        return;
    }

    if (event->button() == Qt::LeftButton && m_tool == PanTool)
    {
        m_dragging = true;
        m_dragAxis = axisZoneAt(event->position());
        m_lastPos = event->position();
        setCursor(Qt::ClosedHandCursor);
        emit activated(m_title);
        return;
    }

    if (event->button() == Qt::LeftButton)
    {
        if (plot.contains(event->position()))
        {
            clearBookmarkSelection();
            const int series = seriesAt(event->position());
            m_selectedSeries = series;
            if (series >= 0)
                emit seriesSelected(series);
            update();
        }
        emit activated(m_title);
    }
    QWidget::mousePressEvent(event);
}

QJsonObject PlottingCanvas::saveState() const
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
    o[QStringLiteral("gridX")] = m_gridX;
    o[QStringLiteral("gridY")] = m_gridY;
    o[QStringLiteral("minorGrid")] = m_minorGrid;
    o[QStringLiteral("legendPos")] = m_legendPos;
    o[QStringLiteral("axisFontSize")] = m_axisFontSize;
    o[QStringLiteral("axisLineWidth")] = m_axisLineWidth;
    o[QStringLiteral("axisColor")] = m_axisColor.isValid()
        ? m_axisColor.name(QColor::HexArgb) : QString();
    o[QStringLiteral("box")] = m_showBox;
    o[QStringLiteral("flipY")] = m_flipY;
    o[QStringLiteral("xlabel")] = m_xLabel;
    o[QStringLiteral("ylabel")] = m_yLabel;
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
        so[QStringLiteral("style")] = s.lineStyle;
        so[QStringLiteral("marker")] = s.marker;
        so[QStringLiteral("markerSize")] = s.markerSize;
        so[QStringLiteral("visible")] = s.visible;
        so[QStringLiteral("offset")] = s.offsetSec;
        so[QStringLiteral("points")] = int(s.count());
        arr.append(so);
    }
    o[QStringLiteral("series")] = arr;
    return o;
}

void PlottingCanvas::restoreState(const QJsonObject &o, const QHash<QString, const PsChannelInfo *> &pool)
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
            s.color = ThemeManager::seriesColor(int(m_series.size()));
        s.width = so.value(QStringLiteral("width")).toInt(1);
        s.axis = so.value(QStringLiteral("axis")).toInt(0);
        s.lineStyle = so.value(QStringLiteral("style")).toInt(0);
        s.marker = so.value(QStringLiteral("marker")).toInt(0);
        s.markerSize = so.value(QStringLiteral("markerSize")).toInt(5);
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
    m_gridX = o.value(QStringLiteral("gridX")).toBool(m_showGrid);
    m_gridY = o.value(QStringLiteral("gridY")).toBool(m_showGrid);
    m_minorGrid = o.value(QStringLiteral("minorGrid")).toBool(false);
    m_legendPos = o.value(QStringLiteral("legendPos")).toInt(0);
    m_axisFontSize = o.value(QStringLiteral("axisFontSize")).toInt(11);
    m_axisLineWidth = o.value(QStringLiteral("axisLineWidth")).toInt(1);
    m_axisColor = QColor(o.value(QStringLiteral("axisColor")).toString());
    m_showBox = o.value(QStringLiteral("box")).toBool(true);
    m_flipY = o.value(QStringLiteral("flipY")).toBool(false);
    m_xLabel = o.value(QStringLiteral("xlabel")).toString();
    m_yLabel = o.value(QStringLiteral("ylabel")).toString();
    m_rubberZoom = o.value(QStringLiteral("rubberZoom")).toBool(false);
    m_kernelBacked = !m_series.isEmpty();
    update();
}

void PlottingCanvas::setTimeRange(double t0, double t1)
{
    if (t1 <= t0)
        return;
    m_t0 = t0;
    m_t1 = t1;
    update();
}

void PlottingCanvas::setFlipY(bool on)
{
    m_flipY = on;
    update();
}

void PlottingCanvas::setAxisLabels(const QString &xLabel, const QString &yLabel)
{
    m_xLabel = xLabel;
    m_yLabel = yLabel;
    update();
}

void PlottingCanvas::setGridVisible(bool on)
{
    m_showGrid = on;
    m_gridX = on;
    m_gridY = on;
    update();
}

void PlottingCanvas::setGridAxes(bool xGrid, bool yGrid, bool minorGrid)
{
    m_gridX = xGrid;
    m_gridY = yGrid;
    m_minorGrid = minorGrid;
    m_showGrid = xGrid || yGrid;
    update();
}

void PlottingCanvas::setLegendPosition(int pos)
{
    m_legendPos = std::clamp(pos, 0, 3);
    update();
}

void PlottingCanvas::setAxisFontSize(int px)
{
    m_axisFontSize = std::clamp(px, 7, 20);
    update();
}

void PlottingCanvas::setAxisLineWidth(int px)
{
    m_axisLineWidth = std::clamp(px, 1, 4);
    update();
}

void PlottingCanvas::setAxisColor(const QColor &c)
{
    m_axisColor = c;
    update();
}

QColor PlottingCanvas::axisInk() const
{
    return m_axisColor.isValid() ? m_axisColor : colorText();
}

void PlottingCanvas::setLegendVisible(bool on)
{
    m_showLegend = on;
    update();
}

void PlottingCanvas::setBoxVisible(bool on)
{
    m_showBox = on;
    update();
}

QCursor PlottingCanvas::toolCursor() const
{
    if (m_tool == NoTool)
        return QCursor(Qt::ArrowCursor);
    if (m_tool == ZoomInTool || m_tool == ZoomOutTool)
    {
        const QIcon ico(QStringLiteral(":/icons/%1.svg")
                            .arg(m_tool == ZoomInTool ? QStringLiteral("zoom_in") : QStringLiteral("zoom_out")));
        const QPixmap pm = ico.pixmap(22, 22);
        return QCursor(pm, 11, 11);
    }
    return QCursor(Qt::OpenHandCursor);
}

void PlottingCanvas::setTool(int tool)
{
    m_tool = tool;
    m_rubbering = false;
    m_rubberRect = QRectF();
    m_dragging = false;
    if (m_hover && plotRect().contains(m_cursor))
        setCursor(toolCursor());
    else
        unsetCursor();
    update();
}

void PlottingCanvas::setRubberZoomEnabled(bool on)
{
    m_rubberZoom = on;
    m_rubbering = false;
    m_rubberRect = QRectF();
    update();
}

void PlottingCanvas::zoomBy(double factor)
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

void PlottingCanvas::requestAutoY()
{
    m_autoY = true;
    resetYAxis();
    update();
}

void PlottingCanvas::toggleChannel(int index)
{
    if (index < 0 || index >= m_series.size())
        return;
    m_series[index].visible = !m_series[index].visible;
    update();
}

void PlottingCanvas::removeChannel(int index)
{
    if (index < 0 || index >= m_series.size())
        return;
    m_series.removeAt(index);
    if (m_selectedSeries == index)
        m_selectedSeries = -1;
    else if (m_selectedSeries > index)
        --m_selectedSeries;
    resetView();
    update();
    emit activated(m_title);
}

void PlottingCanvas::mouseMoveEvent(QMouseEvent *event)
{
    m_cursor = event->position();
    m_hover = true;

    // 右键拖动：超过阈值才进入框选，未拖动前不弹菜单。
    if (m_rightPressed && (event->buttons() & Qt::RightButton) && m_rightCanBox)
    {
        if (!m_rightBoxing
            && (event->position() - m_rightStart).manhattanLength()
                   > QApplication::startDragDistance())
            m_rightBoxing = true;
        if (m_rightBoxing)
        {
            m_rightRect = QRectF(m_rightStart, event->position()).normalized();
            setCursor(Qt::CrossCursor);
            update();
            return;
        }
    }

    // 书签拖动：小距离保持“单击跳转”，超过阈值才移动。
    if (m_bookmarkDragIndex >= 0 && (event->buttons() & Qt::LeftButton)
        && m_bookmarkDragIndex < m_timeMarkers.size())
    {
        if (!m_bookmarkDragActive
            && (event->position() - m_bookmarkDragStart).manhattanLength()
                   > QApplication::startDragDistance())
            m_bookmarkDragActive = true;
        if (m_bookmarkDragActive)
        {
            m_timeMarkers[m_bookmarkDragIndex].t = timeAtX(event->position().x());
            setCursor(Qt::ClosedHandCursor);
            update();
            return;
        }
    }

    // A/B 线拖动。
    if (m_abDrag && (event->buttons() & Qt::LeftButton) && plotRect().contains(event->position()))
    {
        const double tc = timeAtX(event->position().x());
        if (m_abDrag == 1)
            m_abA = tc;
        else
            m_abB = tc;
        update();
        return;
    }

    if (m_titleDrag && (event->buttons() & Qt::LeftButton))
    {
        if ((event->position() - m_titleDragStart).manhattanLength() > 6)
        {
            m_reordering = true;
            setCursor(Qt::ClosedHandCursor);
            reorderByCursor(event->globalPosition().toPoint());
        }
        return;
    }

    if (!(event->buttons() & Qt::LeftButton) && !m_titleDrag && !m_abDrag
        && !m_bookmarkDragActive)
    {
        const QRectF plot = plotRect();
        const AxisZone zone = axisZoneAt(m_cursor);
        if (zone == XAxis)
            setCursor(Qt::SizeHorCursor);
        else if (zone == YAxisLeft || zone == YAxisRight)
            setCursor(Qt::SizeVerCursor);
        else if (plot.contains(m_cursor))
        {
            if (m_spacePan)
                setCursor(Qt::OpenHandCursor);
            else if (m_abEnabled)
            {
                const auto xOf = [&](double t) {
                    return plot.left() + plot.width() * (t - m_t0) / (m_t1 - m_t0);
                };
                if ((m_abHasA && qAbs(m_cursor.x() - xOf(m_abA)) <= 10)
                    || (m_abHasB && qAbs(m_cursor.x() - xOf(m_abB)) <= 10))
                    setCursor(Qt::SizeHorCursor);
                else
                    setCursor(toolCursor());
            }
            else
                setCursor(toolCursor());
        }
        else
            unsetCursor();
    }

    if (m_hoverBar && !m_hoverBar->isVisible() && !m_reordering)
    {
        positionHoverBar();
        m_hoverBar->show();
        m_hoverBar->raise();
        update();
    }
    if (m_rubbering)
    {
        m_rubberRect = QRectF(m_rubberStart, event->position()).normalized();
        update();
        return;
    }
    if (m_dragging)
    {
        setCursor(Qt::ClosedHandCursor);
        const QRectF plot = plotRect();
        const double dx = event->position().x() - m_lastPos.x();
        const double dy = event->position().y() - m_lastPos.y();
        if (dx != 0.0 || dy != 0.0)
            m_followLive = false;
        const bool doX = m_dragAxis != YAxisLeft && m_dragAxis != YAxisRight;
        const bool doY = m_dragAxis != XAxis;
        if (doX && dx != 0.0)
        {
            const double tShift = -dx * (m_t1 - m_t0) / qMax(1.0, plot.width());
            m_t0 += tShift;
            m_t1 += tShift;
        }
        if (doY && dy != 0.0)
        {
            const double yShift = -dy * (m_yMax - m_yMin) / qMax(1.0, plot.height());
            m_yMin += yShift;
            m_yMax += yShift;
            m_y2Min += yShift;
            m_y2Max += yShift;
        }
        m_lastPos = event->position();
        if (doX || doY)
            m_autoY = false;
        if (doX && dx != 0.0)
            emit timeRangeChanged(m_t0, m_t1);
    }
    update();
    QWidget::mouseMoveEvent(event);
}

void PlottingCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton && m_rightPressed)
    {
        const bool boxed = m_rightBoxing;
        const QRectF box = m_rightRect;
        const QPoint globalPos = event->globalPosition().toPoint();
        m_rightPressed = false;
        m_rightBoxing = false;
        if (boxed)
        {
            m_rightRect = QRectF();
            applyBoxZoom(box, m_rightAxis, m_rightStart, event->position());
            update();
            return;
        }
        if (m_rightHit == RightLegend)
            showLegendMenu(m_rightHitIndex, globalPos);
        else if (m_rightHit == RightSeries)
            showSeriesMenu(m_rightHitIndex, globalPos);
        else if (m_rightHit == RightAxis)
            showAxisMenu(m_rightAxis, globalPos);
        else
            showCanvasMenu(timeAtX(m_rightStart.x()), m_rightMarkerIndex, m_rightAxis, globalPos);
        update();
        return;
    }

    if (m_abDrag)
    {
        m_abDrag = 0;
        unsetCursor();
        update();
        return;
    }

    if (m_bookmarkDragIndex >= 0)
    {
        const int index = m_bookmarkDragIndex;
        const QString id = m_bookmarkDragId;
        const bool moved = m_bookmarkDragActive;
        m_bookmarkDragIndex = -1;
        m_bookmarkDragId.clear();
        m_bookmarkDragActive = false;
        unsetCursor();
        if (moved && index < m_timeMarkers.size())
            emit markerMoveRequested(id, m_timeMarkers.at(index).t);
        else
            jumpToBookmark(index);
        update();
        return;
    }

    if (m_titleDrag)
    {
        m_titleDrag = false;
        m_reordering = false;
        unsetCursor();
        return;
    }
    if (m_rubbering)
    {
        m_rubbering = false;
        QRectF r = m_rubberRect.intersected(plotRect());
        if (r.width() > 6 && r.height() > 6)
        {
            const QRectF plot = plotRect();
            // 放大镜：框内放大到满格；缩小镜：框内缩小，反向放大视野
            const double fx = m_tool == ZoomOutTool ? plot.width() / r.width() : 1.0;
            const double fy = m_tool == ZoomOutTool ? plot.height() / r.height() : 1.0;
            const double tcx = m_t0 + (m_t1 - m_t0) * (r.center().x() - plot.left()) / plot.width();
            const double tSpan = (m_t1 - m_t0)
                                 * (m_tool == ZoomOutTool ? fx : r.width() / plot.width());
            const double span = m_yMax - m_yMin;
            const double ycy = m_yMax - span * (r.center().y() - plot.top()) / plot.height();
            const double ySpan = span * (m_tool == ZoomOutTool ? fy : r.height() / plot.height());
            m_t0 = tcx - tSpan / 2.0;
            m_t1 = tcx + tSpan / 2.0;
            m_yMin = ycy - ySpan / 2.0;
            m_yMax = ycy + ySpan / 2.0;
            m_autoY = false;
            emit timeRangeChanged(m_t0, m_t1);
        }
        m_rubberRect = QRectF();
        update();
        return;
    }
    m_dragging = false;
    m_dragAxis = NoAxis;
    unsetCursor();
    update();
    QWidget::mouseReleaseEvent(event);
}

void PlottingCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        const AxisZone zone = axisZoneAt(event->position());
        if (zone == XAxis)
        {
            resetXAxis();
            return;
        }
        if (zone == YAxisLeft || zone == YAxisRight)
        {
            resetYAxis();
            return;
        }
        if (plotRect().contains(event->position()))
        {
            m_autoY = true;
            resetView();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void PlottingCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
    {
        if (m_hasSelectedBookmark)
        {
            emit markerRemoveRequested(m_selectedBookmarkId);
            clearBookmarkSelection();
            event->accept();
            return;
        }
    }
    if (event->key() == Qt::Key_Escape)
    {
        if (m_rightBoxing || m_rightPressed)
        {
            m_rightPressed = false;
            m_rightBoxing = false;
            m_rightRect = QRectF();
            update();
        }
        else if (m_abEnabled || m_abHasA)
            clearMeasurement();
        else if (m_hasSelectedBookmark)
            clearBookmarkSelection();
        else if (m_tool != NoTool)
            setTool(NoTool);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Space)
    {
        m_spacePan = true;
        if (plotRect().contains(m_cursor))
            setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_0 && (event->modifiers() & Qt::ControlModifier))
    {
        m_autoY = true;
        resetView();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal)
    {
        zoomBy(0.8);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Minus)
    {
        zoomBy(1.25);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
    {
        const double span = m_t1 - m_t0;
        const double step = span * (event->modifiers() & Qt::ShiftModifier ? 0.02 : 0.1);
        const double dir = event->key() == Qt::Key_Left ? -1.0 : 1.0;
        m_t0 += dir * step;
        m_t1 += dir * step;
        m_followLive = false;
        emit timeRangeChanged(m_t0, m_t1);
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_BracketLeft || event->key() == Qt::Key_BracketRight)
    {
        const double tc = (m_t0 + m_t1) * 0.5;
        int prev = -1;
        int next = -1;
        for (int i = 0; i < m_timeMarkers.size(); ++i)
        {
            const double t = m_timeMarkers.at(i).t;
            if (t < tc - 1e-9)
                prev = i;
            else if (t > tc + 1e-9)
            {
                next = i;
                break;
            }
        }
        const bool forward = event->key() == Qt::Key_BracketRight;
        if (forward)
            jumpToBookmark(next >= 0 ? next : (m_timeMarkers.isEmpty() ? -1 : 0));
        else
            jumpToBookmark(prev >= 0 ? prev : (m_timeMarkers.isEmpty() ? -1 : m_timeMarkers.size() - 1));
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void PlottingCanvas::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space)
    {
        m_spacePan = false;
        if (plotRect().contains(m_cursor))
            setCursor(toolCursor());
        else
            unsetCursor();
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void PlottingCanvas::leaveEvent(QEvent *event)
{
    m_hover = false;
    if (m_hoverBar && !m_reordering)
    {
        const QPoint local = mapFromGlobal(QCursor::pos());
        if (!m_hoverBar->geometry().contains(local))
        {
            m_hoverBar->hide();
            update();
        }
    }
    update();
    QWidget::leaveEvent(event);
}
