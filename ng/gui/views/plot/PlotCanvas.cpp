#include "PlotCanvas.h"
#include "ChannelMime.h"

#include <fastplot/Panner.h>
#include <fastplot/Picker.h>
#include <fastplot/Axes.h>
#include <fastplot/Zoomer.h>

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QInputDialog>
#include <QUuid>
#include <QJsonArray>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMenu>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace nextpilot::plotscope::dataset;

namespace nextpilot::plotscope::gui
{

namespace
{
// 曲线色来自主题 token（数据语义色），不硬编码——深色主题下自动换成适配色。
// 轴/网格/标题/背景色走 palette（见 applyPalette），同样随主题。
const nextpilot::plotscope::fastplot::AxisSide kSides[4] = {nextpilot::plotscope::fastplot::AxisSide::Left, nextpilot::plotscope::fastplot::AxisSide::Right,
                                      nextpilot::plotscope::fastplot::AxisSide::Bottom, nextpilot::plotscope::fastplot::AxisSide::Top};

// 多曲线配色盘：从主题 highlight 出发做色相轮换，保证深浅主题都可读。
// 不写死十六进制——取 palette 的一个基色，再按索引做 HSL 偏移。
QColor paletteSeriesColor(const QPalette &pal, int index)
{
    const QColor base = pal.color(QPalette::Highlight);
    int h = base.hue();
    if (h < 0)
        h = 210; // 无彩色（灰阶主题）时给一个稳定的蓝相
    // 黄金角步进：相邻曲线色相差异大，多条也不易撞色。
    const int hue = (h + int(index * 137.508)) % 360;
    const int sat = qMax(90, base.saturation());
    const int lit = qBound(110, base.lightness(), 170);
    return QColor::fromHsl(hue, sat, lit);
}
} // namespace

PlotCanvas::PlotCanvas(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(320, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAcceptDrops(true); // 数据集树的"列"节点可拖到画布（ChannelMime）

    // 绘制全部委托给 fastplot：本类不再自己算坐标、画网格、画包络。
    m_plot = new nextpilot::plotscope::fastplot::Axes(this);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_plot);

    // 交互部件：滚轮/框选缩放、左键平移、悬停读数。
    // 三者的状态机都在库里，本类只做接线——画布自身仍然只负责画。
    m_zoomer = new nextpilot::plotscope::fastplot::Zoomer(m_plot, this);
    m_panner = new nextpilot::plotscope::fastplot::Panner(m_plot, this);
    m_picker = new nextpilot::plotscope::fastplot::Picker(m_plot, this);

    // 记录初始范围作为历史基线（撤销可回到初始视图）。
    const fastplot::Viewport &vp0 = m_plot->viewport();
    m_lastViewRange = {vp0.xLower(), vp0.xUpper(), vp0.yLower(), vp0.yUpper()};

    connect(m_zoomer, &nextpilot::plotscope::fastplot::Zoomer::zoomed, this, [this] {
        pushViewHistory();
        double t0 = 0.0;
        double t1 = 0.0;
        if (visibleTimeRange(&t0, &t1))
            Q_EMIT rangeChanged(t0, t1);
    });
    connect(m_panner, &nextpilot::plotscope::fastplot::Panner::panned, this, [this] {
        pushViewHistory();
        double t0 = 0.0;
        double t1 = 0.0;
        if (visibleTimeRange(&t0, &t1))
            Q_EMIT rangeChanged(t0, t1);
    });
    connect(m_picker, &nextpilot::plotscope::fastplot::Picker::hovered, this,
            [this](double x, double y, bool valid) {
                if (!valid)
                {
                    m_plot->setOverlayText(QString());
                    return;
                }
                // 单位属于视图层的知识，库只给数值。
                m_plot->setOverlayText(QStringLiteral("t = %1 s    v = %2")
                                          .arg(x, 0, 'f', 3)
                                          .arg(y, 0, 'f', 4));
            });

    // 游标读数/A-B/书签交互：本类作为 m_plot 的事件过滤器，命中测量线/书签时优先处理，
    // 其余交回 Panner/Zoomer/Picker（后装先收，故在三者之后 install）。
    m_plot->installEventFilter(this);
    m_plot->setFocusPolicy(Qt::StrongFocus); // 键盘快捷键（P/Esc/Del/[]）需要焦点

    applyPalette(); // 颜色全部来自当前主题 palette，不留硬编码浅色
}

PlotCanvas::~PlotCanvas() = default;

const PlotSeries *PlotCanvas::seriesAt(int index) const
{
    return index >= 0 && index < m_series.size() ? &m_series.at(index) : nullptr;
}

void PlotCanvas::setLodData(const QVector<LodTriple> &lod, double t0, double t1)
{
    // 兼容入口：更新首条曲线的 LOD。
    if (m_series.isEmpty())
        return;
    m_series[0].lod = lod;
    m_series[0].t0 = t0;
    m_series[0].t1 = t1;
    m_t0 = t0;
    m_t1 = t1;
    pushToPlot();
}

QColor PlotCanvas::nextSeriesColor() const
{
    return paletteSeriesColor(palette(), m_series.size());
}

int PlotCanvas::setSeries(const PlotSeries &series)
{
    // 同 key 已存在则原地更新（重复拖入同一通道不叠加、只刷新数据）。
    for (int i = 0; i < m_series.size(); ++i)
    {
        if (m_series.at(i).key() == series.key())
        {
            PlotSeries updated = series;
            if (updated.color.isValid() && !updated.color.name().isEmpty())
            {
                // 保留已有颜色（上层未显式指定时不要因刷新而换色）。
                if (updated.color == m_series.at(i).color)
                {
                    // 保持不变
                }
            }
            updated.color = m_series.at(i).color;
            // 变换配方是视图态：重新取数刷新时不丢（外部 series 不带这些字段）。
            updated.xTimeOffset = m_series.at(i).xTimeOffset;
            updated.yTransformId = m_series.at(i).yTransformId;
            updated.yTransformParamsJson = m_series.at(i).yTransformParamsJson;
            m_series[i] = updated;
            m_title = updated.title;
            m_plot->setTitle(updated.title);
            pushToPlot();
            return i;
        }
    }

    PlotSeries s = series;
    if (!s.color.isValid())
        s.color = nextSeriesColor();
    m_series.append(s);
    if (m_series.size() == 1)
    {
        m_title = s.title;
        m_plot->setTitle(s.title);
        m_plot->setLegendVisible(true); // 首条曲线：默认显示图例（之后归用户开关）
    }
    pushToPlot();
    return m_series.size() - 1;
}

void PlotCanvas::setChannel(DatasetId datasetId, const QString &table, const QString &column,
                            const QString &title)
{
    // 兼容入口：单条曲线替换全部（旧调用方/测试用）。
    m_series.clear();
    PlotSeries s;
    s.datasetId = datasetId;
    s.table = table;
    s.column = column;
    s.title = title;
    s.color = paletteSeriesColor(palette(), 0);
    m_series.append(s);
    m_title = title;
    m_plot->setTitle(title);
    // 不在这里 pushToPlot：旧调用方随后会喂 LOD 数据触发绘制。
}

bool PlotCanvas::removeSeries(const QString &key)
{
    for (int i = 0; i < m_series.size(); ++i)
    {
        if (m_series.at(i).key() == key)
        {
            m_series.removeAt(i);
            m_title = m_series.isEmpty() ? QString() : m_series.first().title;
            pushToPlot();
            return true;
        }
    }
    return false;
}

bool PlotCanvas::setSeriesAxis(const QString &key, bool rightAxis)
{
    for (PlotSeries &s : m_series)
    {
        if (s.key() == key)
        {
            if (s.rightAxis == rightAxis)
                return true;
            s.rightAxis = rightAxis;
            pushToPlot(); // 重算左右轴各自的 range（见 pushToPlot 分轴逻辑）
            return true;
        }
    }
    return false;
}

bool PlotCanvas::setSeriesVisible(const QString &key, bool visible)
{
    for (PlotSeries &s : m_series)
    {
        if (s.key() == key)
        {
            if (s.visible == visible)
                return true;
            s.visible = visible;
            pushToPlot();
            return true;
        }
    }
    return false;
}

bool PlotCanvas::setSeriesInk(const QString &key, const QColor &color)
{
    for (PlotSeries &s : m_series)
    {
        if (s.key() == key)
        {
            s.color = color;
            pushToPlot();
            return true;
        }
    }
    return false;
}

bool PlotCanvas::setSeriesWidth(const QString &key, double width)
{
    for (PlotSeries &s : m_series)
    {
        if (s.key() == key)
        {
            s.lineWidth = width;
            pushToPlot();
            return true;
        }
    }
    return false;
}

const PlotSeries *PlotCanvas::seriesByKey(const QString &key) const
{
    for (const PlotSeries &s : m_series)
        if (s.key() == key)
            return &s;
    return nullptr;
}

bool PlotCanvas::setSeriesXOffset(const QString &key, double offsetSec)
{
    for (PlotSeries &s : m_series)
    {
        if (s.key() == key)
        {
            // offset 是配方（绝对值），pushToPlot 时对副本施加，原始数据不动。
            s.xTimeOffset = offsetSec;
            pushToPlot();
            return true;
        }
    }
    return false;
}

bool PlotCanvas::setSeriesYTransform(const QString &key, const QString &descriptorId,
                                     const QString &paramsJson)
{
    for (PlotSeries &s : m_series)
    {
        if (s.key() == key)
        {
            s.yTransformId = descriptorId;
            s.yTransformParamsJson = paramsJson;
            // Y 变换的实际数值施加发生在取数层（ChannelView -> TransformEvaluator），
            // 这里只更新配方；下次 setSeries 会把新结果喂进来。
            return true;
        }
    }
    return false;
}

void PlotCanvas::setAxisTransform(const QString &axis, const QString &descriptorId)
{
    // 轴级变换存视图配方（成员）；曲线级按 key 单独设置时优先。
    if (descriptorId.isEmpty())
        m_axisTransforms.remove(axis);
    else
        m_axisTransforms.insert(axis, descriptorId);
}

QString PlotCanvas::axisTransform(const QString &axis) const
{
    return m_axisTransforms.value(axis);
}

void PlotCanvas::zoomRange(double factor)
{
    if (!m_plot)
        return;
    // 以当前可见窗中心为锚点缩放 X 轴；factor<1 放大、>1 缩小。
    double t0 = 0.0, t1 = 0.0;
    if (!visibleTimeRange(&t0, &t1) || t1 <= t0)
        return;
    const double mid = (t0 + t1) / 2.0;
    const double half = (t1 - t0) / 2.0 * factor;
    m_plot->setXRange(mid - half, mid + half);
    pushViewHistory(); // 变化后记录（内部压入变化前的 m_lastViewRange）
    m_plot->update();
}

void PlotCanvas::autoScale()
{
    if (!m_plot)
        return;
    m_plot->autoScaleX();
    m_plot->autoScaleY();
    pushViewHistory();
    m_plot->update();
}

void PlotCanvas::pushViewHistory()
{
    if (m_restoringView || !m_plot)
        return;
    const fastplot::Viewport &vp = m_plot->viewport();
    const ViewRange cur{vp.xLower(), vp.xUpper(), vp.yLower(), vp.yUpper()};
    // 变化后才发信号，故 m_lastViewRange 是"变化前"的范围——把它压栈作为撤销目标。
    const auto same = [](double a, double b) { return std::fabs(a - b) < 1e-9; };
    if (!(same(cur.xLo, m_lastViewRange.xLo) && same(cur.xHi, m_lastViewRange.xHi) &&
          same(cur.yLo, m_lastViewRange.yLo) && same(cur.yHi, m_lastViewRange.yHi)))
    {
        m_viewHistory.push_back(m_lastViewRange);
        if (m_viewHistory.size() > 64)
            m_viewHistory.removeFirst();
    }
    m_lastViewRange = cur;
}

bool PlotCanvas::canUndoView() const
{
    return !m_viewHistory.isEmpty();
}

void PlotCanvas::undoView()
{
    if (m_viewHistory.isEmpty() || !m_plot)
        return;
    const ViewRange prev = m_viewHistory.takeLast();
    m_restoringView = true;
    m_plot->setXRange(prev.xLo, prev.xHi);
    m_plot->setYRange(prev.yLo, prev.yHi);
    m_restoringView = false;
    m_lastViewRange = prev;
    double t0 = 0.0, t1 = 0.0;
    if (visibleTimeRange(&t0, &t1))
        Q_EMIT rangeChanged(t0, t1);
}

int PlotCanvas::viewHistorySize() const
{
    return m_viewHistory.size();
}

void PlotCanvas::setPlayhead(double x, bool active)
{
    if (!m_plot)
        return;
    m_plot->setPlayhead(x, active);
}

bool PlotCanvas::playheadActive() const
{
    return m_plot && m_plot->playheadActive();
}

// ---- 游标读数 / A-B / 书签 交互 ----

// 从一条曲线取 t 时刻的最近样本（有界窗口，不插值）。xTimeOffset 在窗口时间上抵消。
PlotCanvas::SampledValue PlotCanvas::sampleSeries(const PlotSeries &s, double t) const
{
    SampledValue out;
    if (s.lod.isEmpty())
        return out;
    // LOD 三元组按 x 升序，二分找最近列。x 已含 xTimeOffset，故目标要加偏移。
    const double target = t + s.xTimeOffset;
    const QVector<LodTriple> &lod = s.lod;
    int lo = 0, hi = lod.size() - 1;
    while (lo < hi)
    {
        const int mid = (lo + hi) / 2;
        if (lod.at(mid).x < target)
            lo = mid + 1;
        else
            hi = mid;
    }
    int idx = lo;
    if (idx > 0 && std::fabs(lod.at(idx - 1).x - target) <= std::fabs(lod.at(idx).x - target))
        --idx;
    const LodTriple &p = lod.at(idx);
    out.ok = true;
    out.value = (p.min + p.max) / 2.0; // LOD 桶的代表值（与 fastplot nearestSample 一致）
    return out;
}

// 把当前游标/A-B 状态换算成读数行，重写叠加层。
void PlotCanvas::refreshReadout()
{
    if (!m_plot)
        return;

    // 十字游标读数框。
    if (m_cursorActive)
    {
        QVector<fastplot::Axes::ReadoutRow> rows;
        for (const PlotSeries &s : m_series)
        {
            if (!s.visible || rows.size() >= 8)
                continue;
            const SampledValue v = sampleSeries(s, m_cursorT);
            rows.append({s.legendName(),
                         v.ok ? QString::number(v.value, 'g', 6) : QStringLiteral("--"),
                         s.color});
        }
        const QString header = m_cursorPinned
            ? QStringLiteral("固定 t = %1 s").arg(m_cursorT, 0, 'f', 3)
            : QStringLiteral("t = %1 s").arg(m_cursorT, 0, 'f', 3);
        m_plot->setCursorReadout(m_cursorT, true, m_cursorPinned, header, rows);
    }
    else
    {
        m_plot->setCursorReadout(0.0, false, false, QString(), {});
    }

    // A/B 读数：Δt + 每曲线 Δy 与斜率。
    if (m_abEnabled)
    {
        const double dt = m_abB - m_abA;
        QVector<fastplot::Axes::ReadoutRow> rows;
        bool triangle = false;
        double ayPix = 0.0, byPix = 0.0;
        for (const PlotSeries &s : m_series)
        {
            if (!s.visible || rows.size() >= 6)
                continue;
            const SampledValue va = sampleSeries(s, m_abA);
            const SampledValue vb = m_abHasB ? sampleSeries(s, m_abB) : SampledValue{};
            const QString dy = (va.ok && vb.ok) ? QString::number(vb.value - va.value, 'g', 6) : QStringLiteral("--");
            const QString k = (va.ok && vb.ok && std::fabs(dt) > 1e-12)
                ? QString::number((vb.value - va.value) / dt, 'g', 5) + QStringLiteral("/s")
                : QStringLiteral("--");
            rows.append({s.legendName(), QStringLiteral("Δy %1  k %2").arg(dy, k), s.color});
            // 首条可见曲线画直角三角形：把 A/B 两点 y 数据换算成像素。
            if (!triangle && va.ok && vb.ok && s.rightAxis == false)
            {
                const QRectF plot = m_plot->plotRect();
                const double lo = m_plot->viewport().yLower();
                const double hi = m_plot->viewport().yUpper();
                if (hi > lo)
                {
                    ayPix = plot.bottom() - plot.height() * (va.value - lo) / (hi - lo);
                    byPix = plot.bottom() - plot.height() * (vb.value - lo) / (hi - lo);
                    triangle = true;
                }
            }
        }
        const QString header = QStringLiteral("Δx = %1 s（时间轴）").arg(dt, 0, 'f', 4);
        m_plot->setAB(true, m_abA, m_abB, m_abHasB, header, rows, triangle, ayPix, byPix, 0.0);
    }
    else
    {
        m_plot->setAB(false, 0, 0, false, QString(), {}, false, 0, 0, 0.0);
    }

    // 书签。
    QVector<fastplot::Axes::BookmarkMark> marks;
    for (const Bookmark &bm : m_bookmarks)
    {
        const QString label = bm.label.trimmed().isEmpty()
            ? QStringLiteral("t=%1").arg(bm.t, 0, 'f', 3)
            : bm.label;
        marks.append({bm.id, bm.t, label, bm.id == m_selectedBookmarkId});
    }
    m_plot->setBookmarks(marks);
}

void PlotCanvas::setMarkers(const QJsonArray &markers)
{
    if (!m_plot)
        return;
    QVector<fastplot::Axes::MarkerMark> out;
    out.reserve(markers.size());
    for (const QJsonValue &jv : markers)
    {
        if (!jv.isObject())
            continue;
        const QJsonObject o = jv.toObject();
        fastplot::Axes::MarkerMark mk;
        const QString kind = o.value(QStringLiteral("kind")).toString();
        if (kind == QStringLiteral("region"))    mk.kind = fastplot::Axes::MarkerMark::Kind::Region;
        else if (kind == QStringLiteral("valueBand")) mk.kind = fastplot::Axes::MarkerMark::Kind::ValueBand;
        else                                      mk.kind = fastplot::Axes::MarkerMark::Kind::Event;
        const QString sev = o.value(QStringLiteral("severity")).toString();
        if (sev == QStringLiteral("warning"))   mk.severity = fastplot::Axes::MarkerMark::Severity::Warning;
        else if (sev == QStringLiteral("error")) mk.severity = fastplot::Axes::MarkerMark::Severity::Error;
        else if (sev == QStringLiteral("critical")) mk.severity = fastplot::Axes::MarkerMark::Severity::Critical;
        else                                    mk.severity = fastplot::Axes::MarkerMark::Severity::Info;
        mk.tStart = o.value(QStringLiteral("tStart")).toDouble();
        mk.tEnd = o.value(QStringLiteral("tEnd")).toDouble();
        mk.valueLow = o.value(QStringLiteral("valueLow")).toDouble();
        mk.valueHigh = o.value(QStringLiteral("valueHigh")).toDouble();
        mk.hasValue = o.value(QStringLiteral("hasValue")).toBool();
        mk.label = o.value(QStringLiteral("label")).toString();
        const QString col = o.value(QStringLiteral("color")).toString();
        if (!col.isEmpty()) mk.color = QColor(col);
        out.append(mk);
    }
    m_plot->setMarkers(out);
}

int PlotCanvas::markerCount() const
{
    return m_plot ? m_plot->markerCount() : 0;
}

double PlotCanvas::timeAtPixel(double px) const
{
    return m_plot ? m_plot->pixelToX(px) : 0.0;
}

void PlotCanvas::setCursorTime(double t, bool pinned)
{
    m_cursorT = t;
    m_cursorActive = true;
    m_cursorPinned = pinned;
    refreshReadout();
    Q_EMIT linkedTimeChanged(t, true, this);
}

void PlotCanvas::clearCursor()
{
    if (!m_cursorActive)
        return;
    m_cursorActive = false;
    m_cursorPinned = false;
    refreshReadout();
    Q_EMIT linkedTimeChanged(0.0, false, this);
}

void PlotCanvas::syncLinkedTime(double t, bool visible)
{
    // 静默：只画同页其它画布的联动竖线，不触发读数、不回广播。
    if (m_plot)
        m_plot->setLinkedCursor(t, visible);
}

void PlotCanvas::toggleAB()
{
    // Ctrl+左键：以当前游标时间为中心生成一组 A/B，间距约视窗 10%。
    if (!m_plot)
        return;
    if (m_abEnabled)
    {
        clearAB();
        return;
    }
    m_abEnabled = true;
    m_abHasB = true;
    const double center = m_cursorActive ? m_cursorT : m_plot->viewport().xLower();
    double t0 = 0.0, t1 = 0.0;
    visibleTimeRange(&t0, &t1);
    const double span = qMax(1e-9, t1 - t0) * 0.10;
    m_abA = center - span * 0.5;
    m_abB = center + span * 0.5;
    refreshReadout();
}

void PlotCanvas::clearAB()
{
    if (!m_abEnabled)
        return;
    m_abEnabled = false;
    m_abHasB = false;
    refreshReadout();
}

void PlotCanvas::addBookmark(double t, const QString &label)
{
    // 同一时间点已有书签则选中它，不重复创建。
    for (const Bookmark &bm : m_bookmarks)
    {
        if (std::fabs(bm.t - t) < 1e-9)
        {
            m_selectedBookmarkId = bm.id;
            refreshReadout();
            return;
        }
    }
    Bookmark bm;
    bm.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    bm.t = t;
    bm.label = label.trimmed().isEmpty() ? QStringLiteral("B%1").arg(++m_bookmarkSeq) : label.trimmed();
    m_bookmarks.append(bm);
    m_selectedBookmarkId = bm.id;
    std::sort(m_bookmarks.begin(), m_bookmarks.end(),
              [](const Bookmark &a, const Bookmark &c) { return a.t < c.t; });
    refreshReadout();
    Q_EMIT bookmarksChanged();
}

void PlotCanvas::removeBookmark(const QString &id)
{
    const int before = m_bookmarks.size();
    m_bookmarks.erase(std::remove_if(m_bookmarks.begin(), m_bookmarks.end(),
                                     [&](const Bookmark &bm) { return bm.id == id; }),
                      m_bookmarks.end());
    if (m_bookmarks.size() == before)
        return;
    if (m_selectedBookmarkId == id)
        m_selectedBookmarkId.clear();
    refreshReadout();
    Q_EMIT bookmarksChanged();
}

void PlotCanvas::renameBookmark(const QString &id, const QString &label)
{
    if (label.trimmed().isEmpty())
        return;
    for (Bookmark &bm : m_bookmarks)
    {
        if (bm.id == id)
        {
            bm.label = label.trimmed();
            refreshReadout();
            Q_EMIT bookmarksChanged();
            return;
        }
    }
}

PlotCanvas::HitResult PlotCanvas::hitTest(const QPointF &pixel) const
{
    HitResult res;
    if (!m_plot)
        return res;
    const QRectF plot = m_plot->plotRect();
    if (!plot.contains(pixel))
        return res;
    constexpr double kTolPx = 10.0;

    // 书签 chip 命中：竖线附近优先（选中/拖动）。
    for (const Bookmark &bm : m_bookmarks)
    {
        const double x = m_plot->xToPixel(bm.t);
        if (std::fabs(x - pixel.x()) <= kTolPx)
        {
            res.kind = Hit::Bookmark;
            res.bookmarkId = bm.id;
            return res;
        }
    }
    if (m_abEnabled)
    {
        if (std::fabs(m_plot->xToPixel(m_abA) - pixel.x()) <= kTolPx)
        {
            res.kind = Hit::AB_A;
            return res;
        }
        if (m_abHasB && std::fabs(m_plot->xToPixel(m_abB) - pixel.x()) <= kTolPx)
        {
            res.kind = Hit::AB_B;
            return res;
        }
    }
    if (m_cursorActive && std::fabs(m_plot->xToPixel(m_cursorT) - pixel.x()) <= kTolPx)
    {
        res.kind = Hit::CursorLine;
        return res;
    }
    return res;
}

bool PlotCanvas::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_plot || !m_plot)
        return QWidget::eventFilter(watched, event);

    switch (event->type())
    {
    case QEvent::MouseMove:
        onCanvasMouseMove(static_cast<QMouseEvent *>(event));
        break;
    case QEvent::MouseButtonPress:
        onCanvasMousePress(static_cast<QMouseEvent *>(event));
        break;
    case QEvent::MouseButtonRelease:
        onCanvasMouseRelease(static_cast<QMouseEvent *>(event));
        break;
    case QEvent::Wheel:
        onCanvasWheel(static_cast<QWheelEvent *>(event));
        break;
    default:
        break;
    }
    // 始终不吞事件：本过滤器只"顺便"维护读数状态，导航仍交给 Panner/Zoomer。
    return QWidget::eventFilter(watched, event);
}

void PlotCanvas::onCanvasMouseMove(QMouseEvent *event)
{
    if (!m_plot || !m_plot->plotRect().contains(event->pos()))
        return;

    // 拖动进行中：更新被拖对象。
    if (m_dragKind != 0)
    {
        const double t = timeAtPixel(event->pos().x());
        if (m_dragKind == 1) m_abA = t;
        else if (m_dragKind == 2) m_abB = t;
        else if (m_dragKind == 3)
        {
            for (Bookmark &bm : m_bookmarks)
                if (bm.id == m_dragBookmarkId) bm.t = t;
            std::sort(m_bookmarks.begin(), m_bookmarks.end(),
                      [](const Bookmark &a, const Bookmark &c) { return a.t < c.t; });
        }
        else if (m_dragKind == 4) m_cursorT = t;
        if (std::fabs(event->pos().x() - m_pressPos.x()) > QApplication::startDragDistance())
            m_pressMoved = true;
        refreshReadout();
        if (m_dragKind == 3) Q_EMIT bookmarksChanged();
        if (m_dragKind == 4) Q_EMIT linkedTimeChanged(m_cursorT, true, this);
        return;
    }

    // 修饰键：Shift 拖 = 移动书签、Ctrl = A/B 编辑态；普通悬停 = 吸附最近样本时间。
    const HitResult hit = hitTest(event->pos());
    if ((event->buttons() & Qt::LeftButton) && hit.kind == Hit::Bookmark)
    {
        m_dragKind = 3;
        m_dragBookmarkId = hit.bookmarkId;
        m_pressPos = event->pos();
        return;
    }
    if ((event->buttons() & Qt::LeftButton) && (hit.kind == Hit::AB_A || hit.kind == Hit::AB_B))
    {
        m_dragKind = hit.kind == Hit::AB_A ? 1 : 2;
        m_pressPos = event->pos();
        return;
    }

    // 悬停：取最近样本时间，更新读数（不 pinned）。联动只同步时间。
    double sx = 0.0, sy = 0.0;
    if (m_plot->nearestSample(event->pos(), &sx, &sy))
        setCursorTime(sx, false);
}

void PlotCanvas::onCanvasMousePress(QMouseEvent *event)
{
    if (!m_plot || !m_plot->plotRect().contains(event->pos()))
        return;
    m_pressPos = event->pos();
    m_pressMoved = false;

    if (event->button() != Qt::LeftButton)
        return;
    const Qt::KeyboardModifiers mods = event->modifiers();

    // Ctrl+左键：直接生成一组 A/B。
    if (mods & Qt::ControlModifier)
    {
        double sx = 0.0, sy = 0.0;
        if (m_plot->nearestSample(event->pos(), &sx, &sy))
            m_cursorT = sx;
        toggleAB();
        return;
    }
    // Shift+左键：添加时间书签。
    if (mods & Qt::ShiftModifier)
    {
        double sx = 0.0, sy = 0.0;
        m_plot->nearestSample(event->pos(), &sx, &sy);
        addBookmark(sx);
        return;
    }

    const HitResult hit = hitTest(event->pos());
    if (hit.kind == Hit::Bookmark)
    {
        m_selectedBookmarkId = hit.bookmarkId;
        m_dragKind = 3;
        m_dragBookmarkId = hit.bookmarkId;
        refreshReadout();
    }
    else if (hit.kind == Hit::AB_A || hit.kind == Hit::AB_B)
    {
        m_dragKind = hit.kind == Hit::AB_A ? 1 : 2;
    }
    else if (hit.kind == Hit::CursorLine)
    {
        m_dragKind = 4;
    }
}

void PlotCanvas::onCanvasMouseRelease(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_dragKind = 0;
    m_dragBookmarkId.clear();
}

void PlotCanvas::onCanvasWheel(QWheelEvent *event)
{
    // 缩放/平移后，固定读数要跟着时间映射重画；悬停读数由下一次 mouse move 刷新。
    if (m_cursorPinned)
        refreshReadout();
}

void PlotCanvas::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    const Qt::KeyboardModifiers mods = event->modifiers();

    // Ctrl+Z：撤销上一次缩放/平移（对齐 PJ4 undo）。
    if (key == Qt::Key_Z && (mods & Qt::ControlModifier))
    {
        undoView();
        return;
    }
    if (key == Qt::Key_P && !(mods & Qt::ControlModifier))
    {
        // P：固定/解除当前读数。
        if (m_cursorActive)
            setCursorTime(m_cursorT, !m_cursorPinned);
        return;
    }
    if (key == Qt::Key_Escape)
    {
        // Esc 层级退出：拖动 > A/B > 书签选中 > 固定读数。
        if (m_dragKind) { m_dragKind = 0; return; }
        if (m_abEnabled) { clearAB(); return; }
        if (!m_selectedBookmarkId.isEmpty()) { m_selectedBookmarkId.clear(); refreshReadout(); return; }
        if (m_cursorPinned) { m_cursorPinned = false; refreshReadout(); return; }
        clearCursor();
        return;
    }
    if (key == Qt::Key_Delete || key == Qt::Key_Backspace)
    {
        if (!m_selectedBookmarkId.isEmpty())
            removeBookmark(m_selectedBookmarkId);
        return;
    }
    if (key == Qt::Key_BracketLeft || key == Qt::Key_BracketRight)
    {
        // [ / ]：上/下一个时间书签跳转。
        if (m_bookmarks.isEmpty())
            return;
        const bool forward = key == Qt::Key_BracketRight;
        if (m_selectedBookmarkId.isEmpty())
        {
            m_selectedBookmarkId = forward ? m_bookmarks.first().id : m_bookmarks.last().id;
        }
        else
        {
            int idx = -1;
            for (int i = 0; i < m_bookmarks.size(); ++i)
                if (m_bookmarks.at(i).id == m_selectedBookmarkId) { idx = i; break; }
            idx = forward ? qMin(idx + 1, m_bookmarks.size() - 1) : qMax(idx - 1, 0);
            m_selectedBookmarkId = m_bookmarks.at(idx).id;
        }
        for (const Bookmark &bm : m_bookmarks)
            if (bm.id == m_selectedBookmarkId) { setCursorTime(bm.t, true); break; }
        refreshReadout();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool PlotCanvas::setPlotKind(const QString &kind)
{
    // 本画布真正支持的图型：折线 / 散点 / XY 曲线（都是"曲线"模型，走 LOD 折线绘制）。
    // 柱状/直方/频谱是另一种模型（无时间轴、无 LOD），由 fastplot 的专用 Instrument
    // widget 承载，走 plot-instrument 视图；仪表/地图/3D 同理。返回 false 让上层明确路由，
    // 不在这里假装支持。
    static const QStringList kHere = {QStringLiteral("timeseries"), QStringLiteral("scatter"),
                                      QStringLiteral("xy")};
    if (!kHere.contains(kind))
        return false;
    m_plotKind = kind;
    pushToPlot(); // 按新图型重建曲线（scatter 决定画不画线）
    return true;
}

void PlotCanvas::pushToPlot()
{
    m_plot->clearCurves();
    if (m_series.isEmpty())
    {
        // 图例可见性归用户/状态管理，pushToPlot 只管曲线，不在这里改图例开关。
        m_plot->update();
        return;
    }

    // 左右轴各自的范围——双 Y 轴是用户选择（曲线声明挂哪条轴），
    // 两条轴必须独立定界；混算会让挂右轴的曲线被左轴量级压扁。
    double leftLo = std::numeric_limits<double>::infinity();
    double leftHi = -std::numeric_limits<double>::infinity();
    double rightLo = std::numeric_limits<double>::infinity();
    double rightHi = -std::numeric_limits<double>::infinity();
    bool haveTime = false;

    for (const PlotSeries &s : m_series)
    {
        if (s.lod.isEmpty() || !s.visible)
            continue;
        // 只转三元组（<= 像素宽），不复制原始数据——零拷贝的边界就在这里。
        QVector<nextpilot::plotscope::fastplot::LodPoint> pts;
        pts.reserve(s.lod.size());
        for (const LodTriple &t : s.lod)
            // X 轴时间偏移在副本上施加，LOD / 原始数据都不改。
            pts.push_back(nextpilot::plotscope::fastplot::LodPoint{
                t.x + s.xTimeOffset, t.min, t.max});

        auto &curve = m_plot->addCurve(nextpilot::plotscope::fastplot::Curve::fromLod(pts, s.color, s.lineWidth));
        curve.setName(s.legendName());
        curve.setYAxis(s.rightAxis ? nextpilot::plotscope::fastplot::AxisSide::Right : nextpilot::plotscope::fastplot::AxisSide::Left);
        // 散点图：只画符号不连线（fastplot 库能力，老 Scatter 模式的体验）。
        if (m_plotKind == QStringLiteral("scatter"))
        {
            curve.setScatter(true);
            curve.setSymbol(nextpilot::plotscope::fastplot::Symbol::Circle);
        }
        // 按曲线挂的轴分别累计 min/max。
        double &lo = s.rightAxis ? rightLo : leftLo;
        double &hi = s.rightAxis ? rightHi : leftHi;
        for (const LodTriple &t : s.lod)
        {
            lo = qMin(lo, qMin(t.min, t.max));
            hi = qMax(hi, qMax(t.min, t.max));
        }
        if (!haveTime)
        {
            m_t0 = s.t0;
            m_t1 = s.t1;
            haveTime = true;
        }
    }

    // 横轴按真实时间定界（不是按序号）：数据可能不等间隔。
    if (haveTime && m_t1 > m_t0)
        m_plot->setXRange(m_t0, m_t1);
    // 每条轴按自己那条曲线的 min/max 留 5% 余量。
    auto padRange = [](double lo, double hi) -> QPair<double, double> {
        if (!std::isfinite(lo) || !std::isfinite(hi) || hi <= lo)
            return {0.0, 0.0};
        const double pad = (hi - lo) * 0.05 + 1e-9;
        return {lo - pad, hi + pad};
    };
    const auto left = padRange(leftLo, leftHi);
    if (left.first != left.second)
        m_plot->setYRange(left.first, left.second);
    const auto right = padRange(rightLo, rightHi);
    if (right.first != right.second)
        m_plot->setYRange2(right.first, right.second);
    m_plot->update();
}

bool PlotCanvas::exportPng(const QString &path)
{
    return m_plot && m_plot->exportPng(path);
}

bool PlotCanvas::exportCsv(const QString &path)
{
    return m_plot && m_plot->exportCsv(path);
}

QJsonObject PlotCanvas::saveViewState() const
{
    QJsonObject state = m_plot ? m_plot->saveState() : QJsonObject();
    // 曲线的左右轴分配是"视图状态"（不是数据），随状态一起存。
    QJsonArray axes;
    for (const PlotSeries &s : m_series)
    {
        QJsonObject o;
        o.insert(QStringLiteral("key"), s.key());
        o.insert(QStringLiteral("right"), s.rightAxis);
        axes.append(o);
    }
    state.insert(QStringLiteral("seriesAxes"), axes);
    // 曲线变换配方与 X 偏移：随视图状态持久化（重开后变换仍在）。
    QJsonArray tfArr;
    for (const PlotSeries &s : m_series)
        tfArr.append(QJsonObject{
            {QStringLiteral("key"), s.key()},
            {QStringLiteral("xOffset"), s.xTimeOffset},
            {QStringLiteral("yId"), s.yTransformId},
            {QStringLiteral("yParams"), s.yTransformParamsJson},
        });
    state.insert(QStringLiteral("seriesTransforms"), tfArr);
    QJsonObject axisTf;
    for (auto it = m_axisTransforms.constBegin(); it != m_axisTransforms.constEnd(); ++it)
        axisTf.insert(it.key(), it.value());
    state.insert(QStringLiteral("axisTransform"), axisTf);
    // 图例可见性是视图状态（用户在"显示"里开关的），必须随状态 round-trip。
    state.insert(QStringLiteral("legendVisible"), legendVisible());
    return state;
}

bool PlotCanvas::restoreViewState(const QJsonObject &state)
{
    if (m_plot && !m_plot->restoreState(state))
        return false;
    const QJsonArray axes = state.value(QStringLiteral("seriesAxes")).toArray();
    for (const QJsonValue &v : axes)
    {
        const QJsonObject o = v.toObject();
        const QString key = o.value(QStringLiteral("key")).toString();
        const bool right = o.value(QStringLiteral("right")).toBool();
        for (int i = 0; i < m_series.size(); ++i)
            if (m_series.at(i).key() == key)
                m_series[i].rightAxis = right;
    }
    // 变换配方恢复：曲线 X 偏移 / Y 变换。
    const QJsonArray tfArr = state.value(QStringLiteral("seriesTransforms")).toArray();
    for (const QJsonValue &v : tfArr)
    {
        const QJsonObject o = v.toObject();
        const QString key = o.value(QStringLiteral("key")).toString();
        for (int i = 0; i < m_series.size(); ++i)
        {
            if (m_series.at(i).key() != key)
                continue;
            m_series[i].xTimeOffset = o.value(QStringLiteral("xOffset")).toDouble();
            m_series[i].yTransformId = o.value(QStringLiteral("yId")).toString();
            m_series[i].yTransformParamsJson = o.value(QStringLiteral("yParams")).toString();
        }
    }
    const QJsonObject axisTf = state.value(QStringLiteral("axisTransform")).toObject();
    m_axisTransforms.clear();
    for (auto it = axisTf.begin(); it != axisTf.end(); ++it)
        m_axisTransforms.insert(it.key(), it.value().toString());

    // 重设了挂轴/偏移就得重算 range；pushToPlot 内部会保留图例现状（不再强制打开），
    // 这里再把保存时的图例可见性精确恢复。
    if (!axes.isEmpty() || !tfArr.isEmpty())
        pushToPlot();
    if (state.contains(QStringLiteral("legendVisible")))
        setLegendVisible(state.value(QStringLiteral("legendVisible")).toBool());
    return true;
}

void PlotCanvas::setLegendVisible(bool visible)
{
    if (m_plot)
        m_plot->setLegendVisible(visible);
}

bool PlotCanvas::legendVisible() const
{
    return m_plot && m_plot->legendVisible();
}

bool PlotCanvas::visibleTimeRange(double *t0, double *t1) const
{
    if (!m_plot || m_series.isEmpty())
        return false;
    const auto &vp = m_plot->viewport();
    // 只认时间轴（x）；数据为空时 viewport 仍是默认 0..1，不能拿来当时间窗。
    if (t0)
        *t0 = vp.xLower();
    if (t1)
        *t1 = vp.xUpper();
    return true;
}

int PlotCanvas::lodPointCount() const
{
    int n = 0;
    for (const PlotSeries &s : m_series)
        n += s.lod.size();
    return n;
}

int PlotCanvas::plotPixelWidth() const
{
    if (!m_plot)
        return 1;
    return qMax(1, int(m_plot->plotRect().width()));
}

void PlotCanvas::renderFrom(const Dataset &dataset)
{
    // 用每条序列自己的 table.column 从 Dataset 重新取 LOD（便捷入口）。
    for (PlotSeries &s : m_series)
    {
        if (s.table.isEmpty() || s.column.isEmpty())
            continue;
        const Table *table = dataset.table(s.table);
        if (!table)
            continue;
        const int px = plotPixelWidth();
        const auto lod = dataset.lodMinMax(s.table, s.column,
                                           table->firstTimeUs, table->lastTimeUs, px);
        s.lod = lod;
        s.t0 = table->firstTimeUs / 1000000.0;
        s.t1 = table->lastTimeUs / 1000000.0;
    }
    pushToPlot();
}

void PlotCanvas::clear()
{
    m_series.clear();
    m_title.clear();
    m_t0 = m_t1 = 0.0;
    m_plot->clearCurves();
    m_plot->setTitle(QString());
    m_plot->setLegendVisible(false);
    m_plot->update();
}

void PlotCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // 视口变了 → LOD 的像素分辨率该跟着变。
    // 否则窄屏取的 LOD 会被拉伸到大屏上（梳齿状假象），
    // 或者大屏取的 LOD 塞进小屏丢失细节。
    emit viewportChanged();
}

void PlotCanvas::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    // 主题切换时 ThemeManager 重设全局 palette，PaletteChange 传播到每个 widget。
    // fastplot 库内色值在构造时定死，这里按新 palette 重设一遍，画布随之换肤。
    if (event->type() == QEvent::PaletteChange)
        applyPalette();
}

void PlotCanvas::applyPalette()
{
    if (!m_plot)
        return;
    const QPalette &pal = palette();
    m_plot->setBackground(pal.color(QPalette::Base));
    m_plot->setTitleColor(pal.color(QPalette::WindowText));
    for (nextpilot::plotscope::fastplot::AxisSide side : kSides)
    {
        auto &axis = m_plot->axis(side);
        axis.setInk(pal.color(QPalette::Mid));
        axis.setLabelColor(pal.color(QPalette::WindowText));
        axis.setGridInk(pal.color(QPalette::Light));
    }
    // 曲线色在 pushToPlot 时按当时主题 palette 取（见 pushToPlot），
    // 主题切换后的下一次重取数会用新色；这里不重建曲线，避免丢失 LOD。
    m_plot->replot();
}

// --- 拖拽画图：只认通道 MIME，解码后发信号，取数交给 ChannelView ---
void PlotCanvas::dragEnterEvent(QDragEnterEvent *event)
{
    // 只接受本应用的通道 MIME（数据集树发的），不认文件路径/其它来源——
    // 打开文件有 data.load 意图那条正路，拖文件进来不该走画布。
    if (event->mimeData() && channel_mime::decode(event->mimeData()).valid)
    {
        event->acceptProposedAction();
        m_plot->setOverlayText(QStringLiteral("松开以绘制此通道"));
        return;
    }
    QWidget::dragEnterEvent(event);
}

void PlotCanvas::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData() && channel_mime::decode(event->mimeData()).valid)
    {
        event->acceptProposedAction();
        return;
    }
    QWidget::dragMoveEvent(event);
}

bool PlotCanvas::dropChannel(const QMimeData *mime)
{
    const channel_mime::Decoded ch = channel_mime::decode(mime);
    if (m_plot)
        m_plot->setOverlayText(QString()); // 清掉拖拽提示
    if (!ch.valid)
        return false;
    // 画布不知道"取数"这回事——只发信号。ChannelView 订阅后走
    // plot.channel 意图（与数据集树双击完全同一条路径）。
    Q_EMIT channelDropped(ch.datasetId, ch.table, ch.column);
    return true;
}

void PlotCanvas::dropEvent(QDropEvent *event)
{
    if (dropChannel(event->mimeData()))
    {
        event->acceptProposedAction();
        return;
    }
    QWidget::dropEvent(event);
}

void PlotCanvas::dragLeaveEvent(QDragLeaveEvent *event)
{
    m_plot->setOverlayText(QString());
    QWidget::dragLeaveEvent(event);
}

// 右键菜单：列出叠加的曲线，逐条勾选"挂右 Y 轴"——双 Y 轴的用户选择入口。
// 老代码在 AxisZone 上做，这里简化为"按曲线选轴"：更直接、脚本/属性也能走
// plot.series.axis 同一动作（菜单、view action、脚本三条路同一语义）。
void PlotCanvas::contextMenuEvent(QContextMenuEvent *event)
{
    if (m_series.isEmpty())
    {
        // 空画布也允许选中画布/轴，让属性面板有内容可显示。
        QMenu menu(this);
        menu.setTitle(QStringLiteral("画布操作"));
        menu.addAction(QStringLiteral("画布属性"), this, [this] { Q_EMIT canvasSelectionRequested(); });
        menu.addAction(QStringLiteral("X 轴属性"), this, [this] { Q_EMIT axisSelectionRequested(QStringLiteral("x")); });
        menu.addAction(QStringLiteral("左 Y 轴属性"), this, [this] { Q_EMIT axisSelectionRequested(QStringLiteral("y-left")); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("清除游标读数"), this, [this] { clearCursor(); clearAB(); });
        menu.exec(event->globalPos());
        event->accept();
        return;
    }
    QMenu menu(this);
    menu.setTitle(QStringLiteral("画布操作"));

    // 统一选中入口：把画布/轴选中交给上层 -> 属性面板。
    QMenu *inspect = menu.addMenu(QStringLiteral("属性"));
    inspect->addAction(QStringLiteral("画布属性"), this, [this] { Q_EMIT canvasSelectionRequested(); });
    inspect->addAction(QStringLiteral("X 轴属性"), this, [this] { Q_EMIT axisSelectionRequested(QStringLiteral("x")); });
    inspect->addAction(QStringLiteral("左 Y 轴属性"), this, [this] { Q_EMIT axisSelectionRequested(QStringLiteral("y-left")); });
    inspect->addAction(QStringLiteral("右 Y 轴属性"), this, [this] { Q_EMIT axisSelectionRequested(QStringLiteral("y-right")); });
    menu.addSeparator();

    // 读数测量（对齐老 PlottingCanvas 右键菜单）。
    const HitResult hit = hitTest(event->pos());
    const double clickT = timeAtPixel(event->pos().x());
    if (hit.kind == Hit::Bookmark)
    {
        const QString id = hit.bookmarkId;
        QMenu *bm = menu.addMenu(QStringLiteral("时间书签"));
        bm->addAction(QStringLiteral("重命名…"), this, [this, id] {
            bool ok = false;
            QString cur;
            for (const Bookmark &b : m_bookmarks) if (b.id == id) cur = b.label;
            const QString text = QInputDialog::getText(this, QStringLiteral("重命名书签"),
                QStringLiteral("标签："), QLineEdit::Normal, cur, &ok);
            if (ok) renameBookmark(id, text);
        });
        bm->addAction(QStringLiteral("删除"), this, [this, id] { removeBookmark(id); });
    }
    else
    {
        menu.addAction(QStringLiteral("添加时间书签"), this, [this, clickT] { addBookmark(clickT); });
    }
    menu.addAction(m_abEnabled ? QStringLiteral("清除 A/B 测量") : QStringLiteral("A/B 游标测量（Δt/Δy）"),
                   this, [this] { toggleAB(); });
    if (m_cursorActive && m_cursorPinned)
        menu.addAction(QStringLiteral("取消固定读数"), this, [this] { m_cursorPinned = false; refreshReadout(); });
    else
        menu.addAction(QStringLiteral("固定当前读数"), this, [this, clickT] {
            if (!m_cursorActive) setCursorTime(clickT, true);
            else setCursorTime(m_cursorT, true);
        });
    menu.addSeparator();

    // 视图操作（抄老 PlottingCanvas 右键菜单的常用项）。
    menu.addAction(QStringLiteral("放大"), this, [this] { zoomRange(0.5); });
    menu.addAction(QStringLiteral("缩小"), this, [this] { zoomRange(2.0); });
    menu.addAction(QStringLiteral("自适应"), this, [this] { autoScale(); });
    auto *undoAct = menu.addAction(QStringLiteral("撤销视图 (Ctrl+Z)"), this, [this] { undoView(); });
    undoAct->setEnabled(canUndoView());
    menu.addSeparator();
    auto *gridAction = menu.addAction(QStringLiteral("显示网格"));
    gridAction->setCheckable(true);
    gridAction->setChecked(m_plot && m_plot->gridVisible());
    connect(gridAction, &QAction::triggered, this, [this](bool on) {
        if (m_plot) { m_plot->grid(on); }
    });
    auto *legendAction = menu.addAction(QStringLiteral("显示图例"));
    legendAction->setCheckable(true);
    legendAction->setChecked(legendVisible());
    connect(legendAction, &QAction::triggered, this, [this](bool on) { setLegendVisible(on); });
    menu.addSeparator();

    // 曲线子菜单：每条曲线 = 显隐 + 挂左/右轴 + 删除（老 showSeriesMenu 的体验）。
    QMenu *seriesMenu = menu.addMenu(QStringLiteral("曲线"));
    for (const PlotSeries &s : m_series)
    {
        const QString key = s.key();
        QMenu *one = seriesMenu->addMenu(s.legendName());
        auto *vis = one->addAction(QStringLiteral("显示"));
        vis->setCheckable(true);
        vis->setChecked(s.visible);
        connect(vis, &QAction::triggered, this, [this, key](bool on) { setSeriesVisible(key, on); });
        auto *right = one->addAction(QStringLiteral("挂右侧 Y 轴"));
        right->setCheckable(true);
        right->setChecked(s.rightAxis);
        connect(right, &QAction::triggered, this, [this, key](bool on) { setSeriesAxis(key, on); });
        one->addSeparator();
        one->addAction(QStringLiteral("属性"), this, [this, key] { Q_EMIT seriesSelectionRequested(key); });
        one->addAction(QStringLiteral("Y 轴变换…"), this, [this, key] { Q_EMIT seriesTransformRequested(key); });
        one->addAction(QStringLiteral("删除"), this, [this, key] { removeSeries(key); });
    }
    menu.exec(event->globalPos());
    event->accept();
}

} // namespace nextpilot::plotscope::gui
