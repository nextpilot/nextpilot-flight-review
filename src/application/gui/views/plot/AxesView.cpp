#include "AxesView.h"
#include "axes/CursorReadout.h"
#include "axes/Interaction.h"
#include "axes/SeriesStore.h"
#include "axes/ViewState.h"
#include "ChannelMime.h"
#include "axes/ContextMenuDecor.h"

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
    // 交互出列：事件过滤器与鼠标状态机归 Interaction。
    m_interaction = new Interaction(this);
    m_interaction->attach();

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
    m_readout->refresh(m_interaction ? m_interaction->marksForReadout()
                                     : QVector<CursorReadout::Mark>{});
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
    return m_interaction ? m_interaction->timeAtPixel(px) : 0.0;
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
    if (m_interaction)
        m_interaction->addBookmark(t, label);
}
void AxesView::removeBookmark(const QString &id)
{
    if (m_interaction)
        m_interaction->removeBookmark(id);
}
void AxesView::renameBookmark(const QString &id, const QString &label)
{
    if (m_interaction)
        m_interaction->renameBookmark(id, label);
}
AxesView::HitResult AxesView::hitTest(const QPointF &pixel) const
{
    return m_interaction ? m_interaction->hitTest(pixel) : HitResult{};
}
void AxesView::setCanvasTitle(const QString &title)
{
    m_viewState->setTitleOverride(title);
    m_plot->setTitle(m_viewState->titleOverride().isEmpty() ? m_title
                                                           : m_viewState->titleOverride());
}
bool AxesView::eventFilter(QObject *watched, QEvent *event)
{
    if (m_interaction)
        m_interaction->handlePlotEvent(watched, event);
    // 始终不吞事件：读数状态只是"顺便"维护，导航仍交给 Panner/Zoomer。
    return QWidget::eventFilter(watched, event);
}




void AxesView::keyPressEvent(QKeyEvent *event)
{
    if (m_interaction)
        m_interaction->keyPress(event);
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
    if (m_interaction)
        m_interaction->contextMenu(event);
}
const QVector<AxesView::Bookmark> &AxesView::bookmarks() const
{
    static const QVector<Bookmark> kEmpty;
    return m_interaction ? m_interaction->bookmarks() : kEmpty;
}

} // namespace nextpilot::plotscope::gui
