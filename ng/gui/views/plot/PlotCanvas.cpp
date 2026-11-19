#include "PlotCanvas.h"
#include "ChannelMime.h"

#include <fastplot/Panner.h>
#include <fastplot/Picker.h>
#include <fastplot/Axes.h>
#include <fastplot/Zoomer.h>

#include <QContextMenuEvent>
#include <QJsonArray>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMenu>
#include <QResizeEvent>
#include <QVBoxLayout>

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

    connect(m_zoomer, &nextpilot::plotscope::fastplot::Zoomer::zoomed, this, [this] {
        double t0 = 0.0;
        double t1 = 0.0;
        if (visibleTimeRange(&t0, &t1))
            Q_EMIT rangeChanged(t0, t1);
    });
    connect(m_panner, &nextpilot::plotscope::fastplot::Panner::panned, this, [this] {
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
    m_plot->update();
}

void PlotCanvas::autoScale()
{
    if (!m_plot)
        return;
    m_plot->autoScaleX();
    m_plot->autoScaleY();
    m_plot->update();
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

    // 视图操作（抄老 PlottingCanvas 右键菜单的常用项）。
    menu.addAction(QStringLiteral("放大"), this, [this] { zoomRange(0.5); });
    menu.addAction(QStringLiteral("缩小"), this, [this] { zoomRange(2.0); });
    menu.addAction(QStringLiteral("自适应"), this, [this] { autoScale(); });
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
