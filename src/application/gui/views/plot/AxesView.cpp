#include "AxesView.h"
#include "axes/CursorReadout.h"
#include "axes/SeriesStore.h"
#include "axes/ViewState.h"
#include "ChannelMime.h"
#include "MenuIcons.h"

#include <fastplot/Panner.h>
#include <fastplot/Picker.h>
#include <fastplot/Axes.h>
#include <fastplot/Zoomer.h>

#include <QContextMenuEvent>
#include <QFileDialog>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QDrag>
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

// 右键菜单图标：文案 → MenuIcons 名字。
// 集中成一张映射表，避免每个 addAction 后面再拖一行 setIcon；改菜单文案时
// **必须同步这里**，否则图标会静默消失（不会崩，只是缺图标）。
QString menuIconName(const QString &text)
{
    if (text.contains(QStringLiteral("时间书签")))
        return QStringLiteral("bookmark");
    if (text.contains(QStringLiteral("A/B")))
        return QStringLiteral("measure-ab");
    // "固定当前读数" / "取消固定读数" 都归到图钉。
    if (text.contains(QStringLiteral("读数")))
        return QStringLiteral("pin");
    if (text.contains(QStringLiteral("放大")))
        return QStringLiteral("zoom-in");
    if (text.contains(QStringLiteral("缩小")))
        return QStringLiteral("zoom-out");
    if (text.contains(QStringLiteral("自适应")))
        return QStringLiteral("fit");
    if (text.contains(QStringLiteral("撤销视图")))
        return QStringLiteral("undo");
    if (text.contains(QStringLiteral("添加曲线")))
        return QStringLiteral("add-curve");
    if (text.contains(QStringLiteral("显示网格")))
        return QStringLiteral("grid");
    if (text.contains(QStringLiteral("显示图例")))
        return QStringLiteral("legend");
    if (text.contains(QStringLiteral("向右拆分")))
        return QStringLiteral("split-h");
    if (text.contains(QStringLiteral("向下拆分")))
        return QStringLiteral("split-v");
    if (text.contains(QStringLiteral("导出 PNG")))
        return QStringLiteral("image");
    if (text.contains(QStringLiteral("CSV")))
        return QStringLiteral("csv");
    if (text.contains(QStringLiteral("删除此画布")))
        return QStringLiteral("trash");
    if (text == QStringLiteral("属性") || text.contains(QStringLiteral("属性")))
        return QStringLiteral("info");
    if (text == QStringLiteral("曲线"))
        return QStringLiteral("curve");
    return QString();
}

void decorateMenu(QMenu *menu)
{
    for (QAction *a : menu->actions())
    {
        if (a->isSeparator())
            continue;
        if (QMenu *sub = a->menu())
        {
            decorateMenu(sub);
            const QString n = menuIconName(sub->title());
            if (!n.isEmpty())
                a->setIcon(MenuIcons::icon(n));
            // 曲线子菜单里每条曲线本身也是子菜单：统一给折线图标，读作"这是曲线组"。
            if (sub->title() == QStringLiteral("曲线"))
                for (QAction *inner : sub->actions())
                    if (!inner->isSeparator() && inner->menu())
                        inner->setIcon(MenuIcons::icon(QStringLiteral("curve")));
            continue;
        }
        const QString n = menuIconName(a->text());
        if (!n.isEmpty())
            a->setIcon(MenuIcons::icon(n));
    }
}
} // namespace

AxesView::AxesView(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(320, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAcceptDrops(true); // 数据集树的"列"节点可拖到画布（ChannelMime）

    // 绘制全部委托给 fastplot：本类不再自己算坐标、画网格、画包络。
    m_plot = new nextpilot::plotscope::fastplot::Axes(this);

    // 曲线集合出列：数据与样式归 store，这里只接变化信号重推。
    m_seriesStore = new SeriesStore(this);
    m_readout = new CursorReadout(m_seriesStore, this);
    m_readout->setPlot(m_plot);
    m_seriesStore->setColorProvider([this](int i) { return paletteSeriesColor(palette(), i); });
    connect(m_seriesStore, &SeriesStore::changed, this, &AxesView::pushToPlot);
    connect(m_seriesStore, &SeriesStore::titleChanged, this,
            [this](const QString &title, bool first) {
                m_title = title;
                if (!m_plot)
                    return;
                m_plot->setTitle(title);
                if (first)
                    m_plot->setLegendVisible(true); // 首条曲线：默认显示图例
            });
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
    m_viewState = new ViewState(this);
    m_viewState->resetBaseline({vp0.xLower(), vp0.xUpper(), vp0.yLower(), vp0.yUpper()});
    connect(m_viewState, &ViewState::timeFormatChanged, this, &AxesView::applyTimeFormat);

    // 图例行点击切显隐：Axes 只切 Line，这里同步 PlotSeries.visible，
    // 否则 pushToPlot 的 clearCurves 重建会把显隐状态冲掉。
    connect(m_plot, &nextpilot::plotscope::fastplot::Axes::legendItemToggled, this,
            [this](int curveIndex, bool visible) {
                const PlotSeries *s = m_seriesStore->at(curveIndex);
                if (!s)
                    return;
                m_seriesStore->setVisibleSilently(curveIndex, visible);
                Q_EMIT seriesVisibilityChanged(s->key(), visible);
            });

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
                Q_EMIT pointerHovered(); // 鼠标在绘图区内动：通知上层浮出本格工具栏
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

AxesView::~AxesView() = default;

const PlotSeries *AxesView::seriesAt(int index) const
{
    return m_seriesStore->at(index);
}
void AxesView::setLodData(const QVector<nextpilot::plotscope::dataset::LodTriple> &lod,
                          double t0, double t1)
{
    m_seriesStore->setFirstLod(lod, t0, t1);
}
QColor AxesView::nextSeriesColor() const
{
    return paletteSeriesColor(palette(), m_seriesStore->count());
}
int AxesView::setSeries(const PlotSeries &series)
{
    return m_seriesStore->upsert(series);
}
void AxesView::setChannel(DatasetId datasetId, const QString &table, const QString &column,
                          const QString &title)
{
    // 兼容入口：单条曲线替换全部（旧调用方随后会喂 LOD，故不重推）。
    PlotSeries s;
    s.datasetId = datasetId;
    s.table = table;
    s.column = column;
    s.title = title;
    m_seriesStore->replaceWith(s);
    m_title = title;
}
bool AxesView::removeSeries(const QString &key)
{
    return m_seriesStore->remove(key);
}
bool AxesView::setSeriesAxis(const QString &key, bool rightAxis)
{
    return m_seriesStore->setAxis(key, rightAxis);
}
bool AxesView::setSeriesVisible(const QString &key, bool visible)
{
    return m_seriesStore->setVisible(key, visible);
}
bool AxesView::setSeriesInk(const QString &key, const QColor &color)
{
    return m_seriesStore->setInk(key, color);
}
bool AxesView::setSeriesWidth(const QString &key, double width)
{
    return m_seriesStore->setWidth(key, width);
}
const PlotSeries *AxesView::seriesByKey(const QString &key) const
{
    return m_seriesStore->byKey(key);
}
bool AxesView::setSeriesXOffset(const QString &key, double offsetSec)
{
    return m_seriesStore->setXOffset(key, offsetSec);
}
void AxesView::setSourceOffsetProvider(std::function<double(DatasetId)> provider)
{
    m_seriesStore->setSourceOffsetProvider(std::move(provider));
}
void AxesView::setTimeFormatMode(const QString &mode)
{
    m_viewState->setTimeFormat(mode);
    applyTimeFormat();
}
void AxesView::applyTimeFormat()
{
    if (!m_plot)
        return;
#if 0
#endif
    nextpilot::plotscope::fastplot::Axis &x =
        m_plot->axis(nextpilot::plotscope::fastplot::AxisSide::Bottom);
    if (m_viewState->timeFormat() == QStringLiteral("relative"))
    {
        x.setTickLabelFormatter(nullptr);
        return;
    }
    const bool utcLike = m_viewState->timeFormat() == QStringLiteral("utc");
    x.setTickLabelFormatter([utcLike](double v) {
        // 负值（对齐后左移的数据）保留负号，别让 -0:03 变成 0:03。
        const bool neg = v < 0.0;
        double a = std::fabs(v);
        const int h = static_cast<int>(a / 3600.0);
        a -= h * 3600.0;
        const int m = static_cast<int>(a / 60.0);
        const int s = static_cast<int>(a - m * 60.0);
        QString t = h > 0 ? QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0'))
                                            .arg(s, 2, 10, QLatin1Char('0'))
                          : QStringLiteral("%1:%2").arg(m, 2, 10, QLatin1Char('0'))
                                   .arg(s, 2, 10, QLatin1Char('0'));
        // utc 模式：数据集时间轴是相对秒，没有绝对 UTC 基准（源未携带 epoch），
        // 这里如实退化为时分秒（文档已标注；接上 epoch 后只改这一处）。
        return (neg ? QStringLiteral("-") : QString()) + t;
    });
}

void AxesView::refreshSourceOffsets()
{
    if (m_seriesStore->isEmpty())
        return;
    // LOD 不变，只重推 x 映射（源 offset 在 pushToPlot 的副本上施加）。
    pushToPlot();
}
bool AxesView::toggleSeriesVisible(const QString &key)
{
    const int i = m_seriesStore->indexOf(key);
    if (i < 0)
        return false;
    // Axes::toggleLegendItem 切 Line 并发 legendItemToggled；
    // store 的 visible 由那条信号回写（单一事实源在 Axes）。
    m_plot->toggleLegendItem(i);
    return true;
}
// 源级时间 offset：datasetId 反查源（provider），叠加在曲线 xTimeOffset 之上。
double AxesView::sourceOffsetFor(const PlotSeries &s) const
{
    return m_seriesStore->sourceOffsetFor(s);
}
bool AxesView::setSeriesYTransform(const QString &key, const QString &descriptorId,
                                   const QString &paramsJson)
{
    return m_seriesStore->setYTransform(key, descriptorId, paramsJson);
}
void AxesView::setAxisTransform(const QString &axis, const QString &descriptorId)
{
    // 轴级变换存视图配方；曲线级按 key 单独设置时优先。
    m_viewState->setAxisTransform(axis, descriptorId);
}

QString AxesView::axisTransform(const QString &axis) const
{
    return m_viewState->axisTransforms().value(axis);
}

void AxesView::zoomRange(double factor)
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

void AxesView::autoScale()
{
    if (!m_plot)
        return;
    m_plot->autoScaleX();
    m_plot->autoScaleY();
    pushViewHistory();
    m_plot->update();
}

void AxesView::pushViewHistory()
{
    if (!m_plot)
        return;
    const fastplot::Viewport &vp = m_plot->viewport();
    m_viewState->record({vp.xLower(), vp.xUpper(), vp.yLower(), vp.yUpper()});
}
bool AxesView::canUndoView() const
{
    return m_viewState->canUndo();
}
void AxesView::undoView()
{
    if (!m_viewState->canUndo() || !m_plot)
        return;
    ViewState::Range prev;
    m_viewState->takeUndo(&prev);
    m_viewState->setRestoring(true);
    m_plot->setXRange(prev.xLo, prev.xHi);
    m_plot->setYRange(prev.yLo, prev.yHi);
    m_viewState->setRestoring(false);
    m_viewState->setBaseline(prev);
    double t0 = 0.0, t1 = 0.0;
    if (visibleTimeRange(&t0, &t1))
        Q_EMIT rangeChanged(t0, t1);
}
int AxesView::viewHistorySize() const
{
    return m_viewState->historySize();
}
void AxesView::setPlayhead(double x, bool active)
{
    if (!m_plot)
        return;
    m_plot->setPlayhead(x, active);
}

bool AxesView::playheadActive() const
{
    return m_plot && m_plot->playheadActive();
}

// ---- 游标读数 / A-B / 书签 交互 ----

// 从一条曲线取 t 时刻的最近样本（有界窗口，不插值）。xTimeOffset 在窗口时间上抵消。

// 把当前游标/A-B 状态换算成读数行，重写叠加层。
void AxesView::refreshReadout()
{
    // 书签数据留在交互侧，读数层只要"位置 + 文案 + 选中"。
    QVector<CursorReadout::Mark> marks;
    marks.reserve(m_bookmarks.size());
    for (const Bookmark &bm : m_bookmarks)
        marks.append({bm.id, bm.t, bm.label, bm.id == m_selectedBookmarkId});
    m_readout->refresh(marks);
}
void AxesView::setMarkers(const QJsonArray &markers)
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

int AxesView::markerCount() const
{
    return m_plot ? m_plot->markerCount() : 0;
}

bool AxesView::setStateRows(const QJsonArray &rows, bool append)
{
    if (!m_plot)
        return false;
    QVector<fastplot::Axes::StateRowMark> parsed;
    parsed.reserve(rows.size());
    for (const QJsonValue &jv : rows)
    {
        if (!jv.isObject())
            continue;
        const QJsonObject o = jv.toObject();
        fastplot::Axes::StateRowMark row;
        row.channel = o.value(QStringLiteral("channel")).toString();
        for (const QJsonValue &sv : o.value(QStringLiteral("segments")).toArray())
        {
            if (!sv.isObject())
                continue;
            const QJsonObject so = sv.toObject();
            fastplot::Axes::StateBandMark seg;
            seg.tStart = so.value(QStringLiteral("tStart")).toDouble();
            seg.tEnd = so.value(QStringLiteral("tEnd")).toDouble();
            seg.label = so.value(QStringLiteral("label")).toString();
            const QString col = so.value(QStringLiteral("color")).toString();
            if (!col.isEmpty())
                seg.color = QColor(col); // 缺省走 StateColors 的 FNV-1a 稳定着色
            row.segments.append(seg);
        }
        parsed.append(row);
    }
    if (append)
    {
        // 按 channel 名合并：同名单替换，新名追加（多通道多行叠加）。
        for (const auto &row : parsed)
        {
            bool replaced = false;
            for (auto &existing : m_stateRows)
                if (existing.channel == row.channel)
                {
                    existing = row;
                    replaced = true;
                    break;
                }
            if (!replaced)
                m_stateRows.append(row);
        }
    }
    else
    {
        m_stateRows = parsed;
    }
    m_plot->setStateBands(m_stateRows);
    return true;
}

int AxesView::stateRowCount() const
{
    return m_plot ? m_plot->stateRowCount() : 0;
}

double AxesView::timeAtPixel(double px) const
{
    return m_plot ? m_plot->pixelToX(px) : 0.0;
}

void AxesView::setCursorTime(double t, bool pinned)
{
    m_readout->setCursorTime(t);
    m_readout->setActive(true);
    m_readout->setPinned(pinned);
    refreshReadout();
    Q_EMIT linkedTimeChanged(t, true, this);
}
void AxesView::clearCursor()
{
    if (!m_readout->active())
        return;
    m_readout->setActive(false);
    m_readout->setPinned(false);
    refreshReadout();
    Q_EMIT linkedTimeChanged(0.0, false, this);
}
void AxesView::syncLinkedTime(double t, bool visible)
{
    // 静默：只画同页其它画布的联动竖线，不触发读数、不回广播。
    if (m_plot)
        m_plot->setLinkedCursor(t, visible);
}

void AxesView::toggleAB()
{
    // Ctrl+左键：以当前游标时间为中心生成一组 A/B，间距约视窗 10%。
    if (!m_plot)
        return;
    if (m_readout->abEnabled())
    {
        clearAB();
        return;
    }
    const double center = m_readout->active() ? m_readout->cursorTime()
                                              : m_plot->viewport().xLower();
    double t0 = 0.0, t1 = 0.0;
    visibleTimeRange(&t0, &t1);
    const double span = qMax(1e-9, t1 - t0) * 0.10;
    m_readout->setABEnabled(true);
    m_readout->setABRange(center - span * 0.5, center + span * 0.5, true);
    refreshReadout();
}
void AxesView::clearAB()
{
    if (!m_readout->abEnabled())
        return;
    m_readout->setABEnabled(false);
    m_readout->setABRange(m_readout->abA(), m_readout->abB(), false);
    refreshReadout();
}
void AxesView::addBookmark(double t, const QString &label)
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

void AxesView::removeBookmark(const QString &id)
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

void AxesView::renameBookmark(const QString &id, const QString &label)
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

AxesView::HitResult AxesView::hitTest(const QPointF &pixel) const
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
    if (m_readout->abEnabled())
    {
        if (std::fabs(m_plot->xToPixel(m_readout->abA()) - pixel.x()) <= kTolPx)
        {
            res.kind = Hit::AB_A;
            return res;
        }
        if (m_readout->abHasB() && std::fabs(m_plot->xToPixel(m_readout->abB()) - pixel.x()) <= kTolPx)
        {
            res.kind = Hit::AB_B;
            return res;
        }
    }
    if (m_readout->active() && std::fabs(m_plot->xToPixel(m_readout->cursorTime()) - pixel.x()) <= kTolPx)
    {
        res.kind = Hit::CursorLine;
        return res;
    }
    return res;
}

void AxesView::setCanvasTitle(const QString &title)
{
    m_viewState->setTitleOverride(title);
    m_plot->setTitle(m_viewState->titleOverride().isEmpty() ? m_title
                                                           : m_viewState->titleOverride());
}
bool AxesView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_plot || !m_plot)
        return QWidget::eventFilter(watched, event);

    switch (event->type())
    {
    case QEvent::MouseMove:
        onCanvasMouseMove(static_cast<QMouseEvent *>(event));
        break;
    case QEvent::MouseButtonDblClick:
    {
        // 双击标题栏 = 行内改名：
        // 只认标题带那一行，画图区双击仍然给 Zoomer/Picker。
        const QMouseEvent *me = static_cast<QMouseEvent *>(event);
        if (me->position().y() <= fastplot::Axes::kTitleBarHeight + 2)
        {
            bool ok = false;
            const QString name = QInputDialog::getText(
                this, QStringLiteral("重命名画布"), QStringLiteral("标题"),
                QLineEdit::Normal, canvasTitle(), &ok);
            if (ok)
            {
                setCanvasTitle(name);
                return true;   // 改名这一下不再落到双击缩放
            }
        }
        break;
    }
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

void AxesView::onCanvasMouseMove(QMouseEvent *event)
{
    if (!m_plot)
        return;

    // 图例上按住拖动 = 把这条曲线移到另一画布。
    // 放在 plotRect 判断之前：图例可能画在绘图区边缘，拖出去不该被早退吃掉。
    if (m_legendDragRow >= 0 && (event->buttons() & Qt::LeftButton)
        && (event->pos() - m_pressPos).manhattanLength() > QApplication::startDragDistance()
        && m_legendDragRow < m_seriesStore->all().size())
    {
        const PlotSeries sp = m_seriesStore->all().at(m_legendDragRow);
        QMimeData *mime = new QMimeData;
        mime->setData(QString::fromLatin1(series_mime::type()),
                      series_mime::encode(m_canvasId, sp.key(), sp.datasetId, sp.table,
                                          sp.column, sp.title).toUtf8());
        QDrag *drag = new QDrag(this);
        drag->setMimeData(mime);
        m_legendDragRow = -1;   // 交给 QDrag：release 不再当点击处理
        drag->exec(Qt::MoveAction);
        return;
    }

    if (!m_plot->plotRect().contains(event->pos()))
        return;

    // 拖动进行中：更新被拖对象。
    if (m_dragKind != 0)
    {
        const double t = timeAtPixel(event->pos().x());
        if (m_dragKind == 1) m_readout->setABStart(t);
        else if (m_dragKind == 2) m_readout->setABEnd(t);
        else if (m_dragKind == 3)
        {
            for (Bookmark &bm : m_bookmarks)
                if (bm.id == m_dragBookmarkId) bm.t = t;
            std::sort(m_bookmarks.begin(), m_bookmarks.end(),
                      [](const Bookmark &a, const Bookmark &c) { return a.t < c.t; });
        }
        else if (m_dragKind == 4) m_readout->setCursorTime(t);
        if (std::fabs(event->pos().x() - m_pressPos.x()) > QApplication::startDragDistance())
            m_pressMoved = true;
        refreshReadout();
        if (m_dragKind == 3) Q_EMIT bookmarksChanged();
        if (m_dragKind == 4) Q_EMIT linkedTimeChanged(m_readout->cursorTime(), true, this);
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

void AxesView::onCanvasMousePress(QMouseEvent *event)
{
    if (!m_plot || !m_plot->plotRect().contains(event->pos()))
        return;
    m_pressPos = event->pos();
    m_pressMoved = false;

    if (event->button() != Qt::LeftButton)
        return;
    const Qt::KeyboardModifiers mods = event->modifiers();

    // 图例命中：点击标题栏折叠/展开、点击行切显隐。
    // 图例区优先于修饰键复合（Ctrl+点图例行 = 切显隐，不生成 A/B）。
    {
        const int legendHit = m_plot->legendHitTest(event->pos());
        if (legendHit == -1)
        {
            m_plot->setLegendCollapsed(!m_plot->legendCollapsed());
            return;
        }
        if (legendHit >= 0)
        {
            // 点击切显隐、拖动移动曲线——两者共用图例行，靠"是否移动过"区分。
            m_legendDragRow = legendHit;
            return;
        }
    }

    // Ctrl+左键：直接生成一组 A/B。
    if (mods & Qt::ControlModifier)
    {
        double sx = 0.0, sy = 0.0;
        if (m_plot->nearestSample(event->pos(), &sx, &sy))
            m_readout->setCursorTime(sx);
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

void AxesView::onCanvasMouseRelease(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_dragKind = 0;
    m_dragBookmarkId.clear();
    // 图例上"按下-松开"没有位移 = 点击切显隐（拖动已由 mouseMove 启动 QDrag 清掉候选）。
    if (m_legendDragRow >= 0)
    {
        if (m_plot && !m_pressMoved)
            m_plot->toggleLegendItem(m_legendDragRow);
        m_legendDragRow = -1;
    }
}

void AxesView::onCanvasWheel(QWheelEvent *event)
{
    // Alt+滚轮且悬停在图例区：图例字号步进。
    // Ctrl+滚轮保留给 Y 轴缩放（canvas-interaction.md 既有约定）。
    if ((event->modifiers() & Qt::AltModifier) && m_plot
        && m_plot->legendHitTest(event->position().toPoint()) != -2)
    {
        m_plot->zoomLegendFont(event->angleDelta().y() > 0 ? 1 : -1);
        event->accept();
    }
    // 缩放/平移后，固定读数要跟着时间映射重画；悬停读数由下一次 mouse move 刷新。
    if (m_readout->pinned())
        refreshReadout();
}

void AxesView::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    const Qt::KeyboardModifiers mods = event->modifiers();

    // Ctrl+Z：撤销上一次缩放/平移。
    if (key == Qt::Key_Z && (mods & Qt::ControlModifier))
    {
        undoView();
        return;
    }
    if (key == Qt::Key_P && !(mods & Qt::ControlModifier))
    {
        // P：固定/解除当前读数。
        if (m_readout->active())
            setCursorTime(m_readout->cursorTime(), !m_readout->pinned());
        return;
    }
    if (key == Qt::Key_Escape)
    {
        // Esc 层级退出：拖动 > A/B > 书签选中 > 固定读数。
        if (m_dragKind) { m_dragKind = 0; return; }
        if (m_readout->abEnabled()) { clearAB(); return; }
        if (!m_selectedBookmarkId.isEmpty()) { m_selectedBookmarkId.clear(); refreshReadout(); return; }
        if (m_readout->pinned()) { m_readout->setPinned(false); refreshReadout(); return; }
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

bool AxesView::setPlotKind(const QString &kind)
{
    // 本画布真正支持的图型：折线 / 散点 / XY 曲线（都是"曲线"模型，走 LOD 折线绘制）。
    // 柱状/直方/频谱是另一种模型（无时间轴、无 LOD），由 fastplot 的专用 Instrument
    // widget 承载，走 plot-instrument 视图；仪表/地图/3D 同理。返回 false 让上层明确路由，
    // 不在这里假装支持。
    static const QStringList kHere = {QStringLiteral("timeseries"), QStringLiteral("scatter"),
                                      QStringLiteral("xy")};
    if (!kHere.contains(kind))
        return false;
    m_viewState->setPlotKind(kind);
    pushToPlot(); // 按新图型重建曲线（scatter 决定画不画线）
    return true;
}

void AxesView::pushToPlot()
{
    // 用户改过名的画布，重建曲线后标题仍是改名后的（不被首条曲线标题顶掉）。
    if (!m_viewState->titleOverride().isEmpty())
        m_plot->setTitle(m_viewState->titleOverride());
    m_plot->clearCurves();
    if (m_seriesStore->all().isEmpty())
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

    for (const PlotSeries &s : m_seriesStore->all())
    {
        if (s.lod.isEmpty())
            continue;
        // 只转三元组（<= 像素宽），不复制原始数据——零拷贝的边界就在这里。
        QVector<nextpilot::plotscope::fastplot::LodPoint> pts;
        pts.reserve(s.lod.size());
        for (const LodTriple &t : s.lod)
            // X 轴时间偏移在副本上施加（曲线级 xTimeOffset + 源级 offset），
            // LOD / 原始数据都不改。
            pts.push_back(nextpilot::plotscope::fastplot::LodPoint{
                t.x + s.xTimeOffset + sourceOffsetFor(s), t.min, t.max});

        auto &curve = m_plot->addCurve(nextpilot::plotscope::fastplot::Curve::fromLod(pts, s.color, s.lineWidth));
        curve.setName(s.legendName());
        curve.setYAxis(s.rightAxis ? nextpilot::plotscope::fastplot::AxisSide::Right : nextpilot::plotscope::fastplot::AxisSide::Left);
        // 散点图：只画符号不连线（fastplot 库能力，老 Scatter 模式的体验）。
        if (m_viewState->plotKind() == QStringLiteral("scatter"))
        {
            curve.setScatter(true);
            curve.setSymbol(nextpilot::plotscope::fastplot::Symbol::Circle);
        }
        // #205：隐藏曲线也进 Axes（图例灰显占位、可点击切回），
        // 但不参与轴范围与时间范围累计（隐藏不影响 autoRange）。
        curve.setVisible(s.visible);
        if (!s.visible)
            continue;
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

bool AxesView::exportPng(const QString &path)
{
    return m_plot && m_plot->exportPng(path);
}

bool AxesView::exportCsv(const QString &path)
{
    return m_plot && m_plot->exportCsv(path);
}

QJsonObject AxesView::saveViewState() const
{
    QJsonObject state = m_plot ? m_plot->saveState() : QJsonObject();
    // 曲线的左右轴分配是"视图状态"（不是数据），随状态一起存。
    QJsonArray axes;
    for (const PlotSeries &s : m_seriesStore->all())
    {
        QJsonObject o;
        o.insert(QStringLiteral("key"), s.key());
        o.insert(QStringLiteral("right"), s.rightAxis);
        axes.append(o);
    }
    state.insert(QStringLiteral("seriesAxes"), axes);
    // 曲线变换配方与 X 偏移：随视图状态持久化（重开后变换仍在）。
    QJsonArray tfArr;
    for (const PlotSeries &s : m_seriesStore->all())
        tfArr.append(QJsonObject{
            {QStringLiteral("key"), s.key()},
            {QStringLiteral("xOffset"), s.xTimeOffset},
            {QStringLiteral("yId"), s.yTransformId},
            {QStringLiteral("yParams"), s.yTransformParamsJson},
        });
    state.insert(QStringLiteral("seriesTransforms"), tfArr);
    QJsonObject axisTf;
    for (auto it = m_viewState->axisTransforms().constBegin(); it != m_viewState->axisTransforms().constEnd(); ++it)
        axisTf.insert(it.key(), it.value());
    state.insert(QStringLiteral("axisTransform"), axisTf);
    // 图例可见性是视图状态（用户在"显示"里开关的），必须随状态 round-trip。
    state.insert(QStringLiteral("legendVisible"), legendVisible());
    return state;
}

bool AxesView::restoreViewState(const QJsonObject &state)
{
    if (m_plot && !m_plot->restoreState(state))
        return false;
    const QJsonArray axes = state.value(QStringLiteral("seriesAxes")).toArray();
    for (const QJsonValue &v : axes)
    {
        const QJsonObject o = v.toObject();
        const QString key = o.value(QStringLiteral("key")).toString();
        const bool right = o.value(QStringLiteral("right")).toBool();
        m_seriesStore->setAxis(key, right);
    }
    // 变换配方恢复：曲线 X 偏移 / Y 变换。
    const QJsonArray tfArr = state.value(QStringLiteral("seriesTransforms")).toArray();
    for (const QJsonValue &v : tfArr)
    {
        const QJsonObject o = v.toObject();
        const QString key = o.value(QStringLiteral("key")).toString();
        m_seriesStore->setXOffset(key, o.value(QStringLiteral("xOffset")).toDouble());
        m_seriesStore->setYTransform(key, o.value(QStringLiteral("yId")).toString(),
                                     o.value(QStringLiteral("yParams")).toString());
    }
    const QJsonObject axisTf = state.value(QStringLiteral("axisTransform")).toObject();
    QHash<QString, QString> tfMap;
    for (auto it = axisTf.begin(); it != axisTf.end(); ++it)
        tfMap.insert(it.key(), it.value().toString());
    m_viewState->replaceAxisTransforms(tfMap);

    // 重设了挂轴/偏移就得重算 range；pushToPlot 内部会保留图例现状（不再强制打开），
    // 这里再把保存时的图例可见性精确恢复。
    if (!axes.isEmpty() || !tfArr.isEmpty())
        pushToPlot();
    if (state.contains(QStringLiteral("legendVisible")))
        setLegendVisible(state.value(QStringLiteral("legendVisible")).toBool());
    return true;
}

void AxesView::setLegendVisible(bool visible)
{
    if (m_plot)
        m_plot->setLegendVisible(visible);
}

bool AxesView::legendVisible() const
{
    return m_plot && m_plot->legendVisible();
}

bool AxesView::visibleTimeRange(double *t0, double *t1) const
{
    if (!m_plot || m_seriesStore->all().isEmpty())
        return false;
    const auto &vp = m_plot->viewport();
    // 只认时间轴（x）；数据为空时 viewport 仍是默认 0..1，不能拿来当时间窗。
    if (t0)
        *t0 = vp.xLower();
    if (t1)
        *t1 = vp.xUpper();
    return true;
}

int AxesView::lodPointCount() const
{
    int n = 0;
    for (const PlotSeries &s : m_seriesStore->all())
        n += s.lod.size();
    return n;
}
int AxesView::plotPixelWidth() const
{
    if (!m_plot)
        return 1;
    return qMax(1, int(m_plot->plotRect().width()));
}

QString AxesView::tickLabelForTest(double seconds) const
{
    const fastplot::Axis &ax = m_plot->axis(fastplot::AxisSide::Bottom);
    const auto f = ax.tickLabelFormatter();
    return f ? f(seconds) : QString();
}

void AxesView::applyTimeFormatToAxes()
{
    // setTickLabelFormatter 只改画法，不改 number；pushToPlot 重建轴范围时重设一次即可。
    applyTimeFormat();
}

void AxesView::renderFrom(const Dataset &dataset)
{
    // 用每条序列自己的 table.column 从 Dataset 重新取 LOD（便捷入口）。
    for (int i = 0; i < m_seriesStore->count(); ++i)
    {
        PlotSeries *s = m_seriesStore->mutableAt(i);
        if (!s || s->table.isEmpty() || s->column.isEmpty())
            continue;
        const Table *table = dataset.table(s->table);
        if (!table)
            continue;
        const int px = plotPixelWidth();
        const auto lod = dataset.lodMinMax(s->table, s->column,
                                           table->firstTimeUs, table->lastTimeUs, px);
        s->lod = lod;
        s->t0 = table->firstTimeUs / 1000000.0;
        s->t1 = table->lastTimeUs / 1000000.0;
    }
    pushToPlot();
}
void AxesView::clear()
{
    m_seriesStore->clear();
    m_title.clear();
    m_t0 = m_t1 = 0.0;
    m_plot->clearCurves();
    m_plot->setTitle(QString());
    m_plot->setLegendVisible(false);
    m_plot->update();
}
void AxesView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // 视口变了 → LOD 的像素分辨率该跟着变。
    // 否则窄屏取的 LOD 会被拉伸到大屏上（梳齿状假象），
    // 或者大屏取的 LOD 塞进小屏丢失细节。
    emit viewportChanged();
}

void AxesView::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    // 主题切换时 ThemeManager 重设全局 palette，PaletteChange 传播到每个 widget。
    // fastplot 库内色值在构造时定死，这里按新 palette 重设一遍，画布随之换肤。
    if (event->type() == QEvent::PaletteChange)
        applyPalette();
}

void AxesView::applyPalette()
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
void AxesView::dragEnterEvent(QDragEnterEvent *event)
{
    // 只接受本应用的通道 MIME（数据集树发的），不认文件路径/其它来源——
    // 打开文件有 data.load 意图那条正路，拖文件进来不该走画布。
    if (event->mimeData()
        && (channel_mime::decode(event->mimeData()).valid
            || (series_mime::decode(event->mimeData()).valid
                && series_mime::decode(event->mimeData()).srcCanvasId != m_canvasId)))
    {
        event->acceptProposedAction();
        m_plot->setOverlayText(series_mime::decode(event->mimeData()).valid
                                   ? QStringLiteral("松开把这条曲线移到本画布")
                                   : QStringLiteral("松开以绘制此通道"));
        return;
    }
    QWidget::dragEnterEvent(event);
}

void AxesView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()
        && (channel_mime::decode(event->mimeData()).valid
            || series_mime::decode(event->mimeData()).valid))
    {
        event->acceptProposedAction();
        return;
    }
    QWidget::dragMoveEvent(event);
}

bool AxesView::dropChannel(const QMimeData *mime)
{
    if (m_plot)
        m_plot->setOverlayText(QString()); // 清掉拖拽提示
    // 画布间移动曲线：拖回自己不算移动（否则会在原地丢一条）。
    const series_mime::Decoded sm = series_mime::decode(mime);
    if (sm.valid)
    {
        if (sm.srcCanvasId == m_canvasId)
            return false;
        Q_EMIT seriesTransferRequested(sm.srcCanvasId, sm.key, sm.datasetId, sm.table,
                                      sm.column, sm.title);
        return true;
    }
    const channel_mime::Decoded ch = channel_mime::decode(mime);
    if (!ch.valid)
        return false;
    // 画布不知道"取数"这回事——只发信号。ChannelView 订阅后走
    // plot.channel 意图（与数据集树双击完全同一条路径）。
    Q_EMIT channelDropped(ch.datasetId, ch.table, ch.column);
    return true;
}

void AxesView::dropEvent(QDropEvent *event)
{
    if (dropChannel(event->mimeData()))
    {
        event->acceptProposedAction();
        return;
    }
    QWidget::dropEvent(event);
}

void AxesView::dragLeaveEvent(QDragLeaveEvent *event)
{
    m_plot->setOverlayText(QString());
    QWidget::dragLeaveEvent(event);
}

// 右键菜单：列出叠加的曲线，逐条勾选"挂右 Y 轴"——双 Y 轴的用户选择入口。
// 按曲线选轴：菜单、view action、脚本三条路同一语义（plot.series.axis）。
void AxesView::contextMenuEvent(QContextMenuEvent *event)
{
    // 空画布与有曲线共用同一套菜单。
    // 早先这里 `m_seriesStore->all().isEmpty()` 提前 return，只给 4 项（属性三项 + 清游标）——
    // 于是空画布上想"显示网格 / 导出 / 加书签"全都没有入口（实测踩到）。main 同一位置 13 项。
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
        // 文案带点击时刻（main 的"在 16.02 添加时间书签…"语义）：
        // 用户不必先把游标挪过去，右键点在哪一刻就打到哪一刻。
        menu.addAction(QStringLiteral("在 %1 s 添加时间书签").arg(QString::number(clickT, 'f', 2)),
                       this, [this, clickT] { addBookmark(clickT); });
    }
    menu.addAction(m_readout->abEnabled() ? QStringLiteral("清除 A/B 测量") : QStringLiteral("A/B 游标测量（Δt/Δy）"),
                   this, [this] { toggleAB(); });
    if (m_readout->active() && m_readout->pinned())
        menu.addAction(QStringLiteral("取消固定读数"), this, [this] { m_readout->setPinned(false); refreshReadout(); });
    else
        menu.addAction(QStringLiteral("固定当前读数"), this, [this, clickT] {
            if (!m_readout->active()) setCursorTime(clickT, true);
            else setCursorTime(m_readout->cursorTime(), true);
        });
    menu.addSeparator();

    // 视图操作（右键菜单常用项）。
    menu.addAction(QStringLiteral("放大"), this, [this] { zoomRange(0.5); });
    menu.addAction(QStringLiteral("缩小"), this, [this] { zoomRange(2.0); });
    menu.addAction(QStringLiteral("自适应"), this, [this] { autoScale(); });
    auto *undoAct = menu.addAction(QStringLiteral("撤销视图 (Ctrl+Z)"), this, [this] { undoView(); });
    undoAct->setEnabled(canUndoView());
    menu.addSeparator();

    // 「添加曲线」：ng 还没有通道选择对话框，这里给禁用项 + 指路，
    // 而不是放一个点了没反应的假入口（main 那版是弹 ChannelPicker，ng 暂缺）。
    auto *addSeries = menu.addAction(QStringLiteral("添加曲线…"));
    addSeries->setEnabled(false);
    addSeries->setToolTip(QStringLiteral("从左侧数据集面板拖入字段，或在字段右键选「画到当前画布」"));
    menu.addSeparator();
    // 显示网格 / 显示图例：主题里 QMenu::indicator 不绘制，单靠 setCheckable
    // 用户看不出当前开关态，所以文案前置 ✓（与 main 同一位置、同一写法）。
    auto *gridAction = menu.addAction(QString());
    auto syncGridLabel = [gridAction](bool on) {
        gridAction->setText(on ? QStringLiteral("✓ 显示网格") : QStringLiteral("显示网格"));
    };
    gridAction->setCheckable(true);
    gridAction->setChecked(m_plot && m_plot->gridVisible());
    syncGridLabel(gridAction->isChecked());
    connect(gridAction, &QAction::triggered, this, [this, syncGridLabel](bool on) {
        if (m_plot) { m_plot->grid(on); }
        syncGridLabel(on);
    });
    auto *legendAction = menu.addAction(QString());
    auto syncLegendLabel = [legendAction](bool on) {
        legendAction->setText(on ? QStringLiteral("✓ 显示图例") : QStringLiteral("显示图例"));
    };
    legendAction->setCheckable(true);
    legendAction->setChecked(legendVisible());
    syncLegendLabel(legendAction->isChecked());
    connect(legendAction, &QAction::triggered, this, [this, syncLegendLabel](bool on) {
        setLegendVisible(on);
        syncLegendLabel(on);
    });
    menu.addSeparator();

    // 曲线子菜单：每条曲线 = 显隐 + 挂左/右轴 + 删除（老 showSeriesMenu 的体验）。
    QMenu *seriesMenu = menu.addMenu(QStringLiteral("曲线"));
    for (const PlotSeries &s : m_seriesStore->all())
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
    menu.addSeparator();

    // 画布级操作（页内）：本画布只发信号，由 PlotWorkspace 反查索引后执行——
    // 保证"删的是你右键的那个画布"，而不是碰巧处于激活态的另一个。
    menu.addAction(QStringLiteral("向右拆分"), this,
                   [this] { Q_EMIT canvasActionRequested(QStringLiteral("splitHorizontal")); });
    menu.addAction(QStringLiteral("向下拆分"), this,
                   [this] { Q_EMIT canvasActionRequested(QStringLiteral("splitVertical")); });

    // 导出本画布：自己开文件对话框（main 的"导出当前画布"在同一位置），
    // 不绕回 MainWindow 的 action 路径，避免"导出的是当前激活画布而不是右键画布"。
    menu.addAction(QStringLiteral("导出 PNG…"), this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出 PNG"), QString(),
                                                          QStringLiteral("PNG 图片 (*.png)"));
        if (!path.isEmpty())
            exportPng(path);
    });
    menu.addAction(QStringLiteral("导出当前画布 CSV…"), this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出 CSV"), QString(),
                                                          QStringLiteral("CSV 表格 (*.csv)"));
        if (!path.isEmpty())
            exportCsv(path);
    });

    menu.addSeparator();
    // 单画布页删不到 0：置灰（由 workspace 重排时同步），而不是点了才弹提示。
    auto *removeAct = menu.addAction(QStringLiteral("删除此画布"), this,
                                     [this] { Q_EMIT canvasActionRequested(QStringLiteral("removeCanvas")); });
    removeAct->setEnabled(m_canvasRemovable);
    if (!m_canvasRemovable)
        removeAct->setToolTip(QStringLiteral("页内至少保留一个画布"));

    // 图标集中装饰（见文件头 decorateMenu）：文案是本地构造的，改文案要同步映射表。
    decorateMenu(&menu);
    menu.exec(event->globalPos());
    event->accept();
}

} // namespace nextpilot::plotscope::gui
