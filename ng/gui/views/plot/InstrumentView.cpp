#include "InstrumentView.h"

#include "AppContext.h"
#include "PanelFactoryRegistry.h"
#include "IIntentExecutor.h"
#include "Intent.h"
#include "Result.h"
#include "DataRepository.h"

#include <QStackedLayout>
#include <QVariantList>
#include <QVariantMap>

#include <fastplot/AttitudeIndicator.h>
#include <fastplot/Bar.h>
#include <fastplot/CompassRose.h>
#include <fastplot/DialGauge.h>
#include <fastplot/Histogram.h>
#include <fastplot/Spectrum.h>
#include <fastplot/TrackPlot.h>
#include <fastplot/Trajectory3D.h>
#include <fastplot/WaterfallPlot.h>

namespace nextpilot::plotscope::gui
{
using nextpilot::plotscope::context::AppContext;
using nextpilot::plotscope::intent::IIntentExecutor;
using nextpilot::plotscope::intent::Intent;
using nextpilot::plotscope::intent::IntentResult;
using nextpilot::plotscope::foundation::Result;

namespace
{
// 仪表配色：与曲线画布同一套冷色，避免每个仪表各挑一套颜色。
const QVector<QColor> kPalette{
    QColor(0x5b, 0xc0, 0xf0), QColor(0xf0, 0x9d, 0x5b), QColor(0x8a, 0xd8, 0x6e),
    QColor(0xc0, 0x7a, 0xe8), QColor(0xf0, 0x6b, 0x8a), QColor(0x6e, 0xd0, 0xd0),
};

QVector<double> toDoubleList(const QVariantList &list)
{
    QVector<double> out;
    out.reserve(list.size());
    for (const QVariant &v : list)
        out.append(v.toDouble());
    return out;
}

double scalar(const QVariantMap &scalars, const QString &role, double fallback = 0.0)
{
    return scalars.contains(role) ? scalars.value(role).toDouble() : fallback;
}

// 从数据集的列名推断仪表角色（table.column 限定名）。
// 这是"用户双击了某通道再点仪表"的默认绑定：只按列名语义匹配，不猜数值、不造数据。
// 匹配不到的角色宁可留空让 plot.instrument 明确报错，也不拿无关通道顶替。
QString findColumn(const nextpilot::plotscope::dataset::Dataset *ds, const QString &table,
                   const QStringList &keywords)
{
    if (!ds)
        return QString();
    const nextpilot::plotscope::dataset::Table *t = ds->table(table);
    if (!t)
        return QString();
    for (const nextpilot::plotscope::dataset::Column &c : t->columns)
    {
        if (!nextpilot::plotscope::dataset::valueTypeIsNumeric(c.type))
            continue;
        const QString lower = c.name.toLower();
        for (const QString &kw : keywords)
            if (lower.contains(kw))
                return table + QLatin1Char('.') + c.name;
    }
    return QString();
}

// 第一个数值通道（活动列缺失时的兜底）：只取 schema 元数据，不读数据。
QString firstNumericColumn(const nextpilot::plotscope::dataset::Dataset *ds, const QString &table)
{
    if (!ds)
        return QString();
    const nextpilot::plotscope::dataset::Table *t = ds->table(table);
    if (!t)
        return QString();
    for (const nextpilot::plotscope::dataset::Column &c : t->columns)
        if (nextpilot::plotscope::dataset::valueTypeIsNumeric(c.type))
            return table + QLatin1Char('.') + c.name;
    return QString();
}
} // namespace

InstrumentView::InstrumentView(intent::IIntentExecutor &executor, QWidget *parent)
    : QWidget(parent), m_executor(executor)
{
    m_stack = new QStackedLayout(this);

    m_attitude = new nextpilot::plotscope::fastplot::AttitudeIndicator();
    m_bar = new nextpilot::plotscope::fastplot::Bar();
    m_compass = new nextpilot::plotscope::fastplot::CompassRose();
    m_gauge = new nextpilot::plotscope::fastplot::DialGauge();
    m_histogram = new nextpilot::plotscope::fastplot::Histogram();
    m_spectrum = new nextpilot::plotscope::fastplot::Spectrum();
    m_track = new nextpilot::plotscope::fastplot::TrackPlot();
    m_traj = new nextpilot::plotscope::fastplot::Trajectory3D();
    m_waterfall = new nextpilot::plotscope::fastplot::WaterfallPlot();

    m_stack->addWidget(m_attitude);
    m_stack->addWidget(m_bar);
    m_stack->addWidget(m_compass);
    m_stack->addWidget(m_gauge);
    m_stack->addWidget(m_histogram);
    m_stack->addWidget(m_spectrum);
    m_stack->addWidget(m_track);
    m_stack->addWidget(m_traj);
    m_stack->addWidget(m_waterfall);

    m_histogram->setBins(40);
    m_histogram->setColor(kPalette.at(0));
    m_spectrum->setColor(kPalette.at(0));

    // 瀑布缓冲一次性预留上限：之后只 resize 不改容量，指针才稳定。
    m_wfBuffers.reserve(64);
    m_wfData.reserve(64);
}

void InstrumentView::switchKind(const QString &kind)
{
    m_kind = kind;
    QWidget *target = m_attitude;
    if (kind == QStringLiteral("bar"))
        target = m_bar;
    else if (kind == QStringLiteral("compass"))
        target = m_compass;
    else if (kind == QStringLiteral("gauge"))
        target = m_gauge;
    else if (kind == QStringLiteral("histogram"))
        target = m_histogram;
    else if (kind == QStringLiteral("fft"))
        target = m_spectrum;
    else if (kind == QStringLiteral("track"))
        target = m_track;
    else if (kind == QStringLiteral("traj3d"))
        target = m_traj;
    else if (kind == QStringLiteral("waterfall"))
        target = m_waterfall;
    m_stack->setCurrentWidget(target);
}

bool InstrumentView::renderFromActive(const QString &kind)
{
    if (!m_appCtx)
        return false;
    const dataset::DatasetId dsId = m_appCtx->activeDatasetId();
    const dataset::Dataset *ds = m_appCtx->data().get(dsId);
    if (!ds)
        return false;

    const QString table = m_appCtx->activeTable();
    // 活动列缺失时退到该表第一个数值通道——都是 schema 推断，不碰数据本体。
    const QString fallback = firstNumericColumn(ds, table);
    if (fallback.isEmpty())
        return false;

    QVariantMap roles;
    QVariantList channels;

    if (kind == QStringLiteral("gauge") || kind == QStringLiteral("histogram") ||
        kind == QStringLiteral("fft"))
    {
        roles[QStringLiteral("value")] =
            findColumn(ds, table, {QStringLiteral("value")});
        if (roles.value(QStringLiteral("value")).toString().isEmpty())
            roles[QStringLiteral("value")] = fallback;
    }
    else if (kind == QStringLiteral("compass"))
    {
        roles[QStringLiteral("yaw")] = findColumn(
            ds, table, {QStringLiteral("yaw"), QStringLiteral("heading"),
                        QStringLiteral("course")});
        if (roles.value(QStringLiteral("yaw")).toString().isEmpty())
            roles[QStringLiteral("yaw")] = fallback;
    }
    else if (kind == QStringLiteral("attitude"))
    {
        roles[QStringLiteral("roll")] =
            findColumn(ds, table, {QStringLiteral("roll")});
        roles[QStringLiteral("pitch")] =
            findColumn(ds, table, {QStringLiteral("pitch")});
        // 只有一个匹配时，另一角色也用活动通道兜底，避免姿态仪半空——
        // 但这会显示同值，属可接受的默认；用户可在属性面板改绑。
        if (roles.value(QStringLiteral("roll")).toString().isEmpty())
            roles[QStringLiteral("roll")] = fallback;
        if (roles.value(QStringLiteral("pitch")).toString().isEmpty())
            roles[QStringLiteral("pitch")] = fallback;
    }
    else if (kind == QStringLiteral("track"))
    {
        roles[QStringLiteral("lon")] = findColumn(
            ds, table, {QStringLiteral("lon"), QStringLiteral("lng"), QStringLiteral("long")});
        roles[QStringLiteral("lat")] =
            findColumn(ds, table, {QStringLiteral("lat")});
    }
    else if (kind == QStringLiteral("traj3d"))
    {
        roles[QStringLiteral("x")] = findColumn(ds, table, {QStringLiteral("x")});
        roles[QStringLiteral("y")] = findColumn(ds, table, {QStringLiteral("y")});
        roles[QStringLiteral("z")] = findColumn(ds, table, {QStringLiteral("z")});
    }
    else if (kind == QStringLiteral("waterfall") || kind == QStringLiteral("bar"))
    {
        // 瀑布/柱状：该表全部数值通道都作为序列喂入。
        if (const dataset::Table *t = ds->table(table))
            for (const dataset::Column &c : t->columns)
                if (dataset::valueTypeIsNumeric(c.type))
                    channels.append(table + QLatin1Char('.') + c.name);
        if (channels.isEmpty())
            return false;
    }
    else
    {
        return false;
    }

    // 乐队：表盘量程按数据自动算（PlotModule 里 rangeFromData 分支）。
    renderInstrument(dsId, kind, roles, channels, {});
    return true;
}

const nextpilot::plotscope::fastplot::ArrayData *InstrumentView::bindRole(const QString &role, const QVariantMap &series)
{
    const QVariantMap m = series.value(role).toMap();
    if (m.isEmpty())
        return nullptr;

    RoleBuffer &buffer = m_buffers[role];
    buffer.time = toDoubleList(m.value(QStringLiteral("time")).toList());
    buffer.value = toDoubleList(m.value(QStringLiteral("value")).toList());
    if (buffer.value.isEmpty())
        return nullptr;

    ++m_revision;
    buffer.data.emplace(buffer.time.constData(), buffer.value.constData(),
                        buffer.value.size(), m_revision);
    return &(*buffer.data);
}

void InstrumentView::renderInstrument(dataset::DatasetId datasetId, const QString &kind,
                                      const QVariantMap &roles, const QVariantList &channels,
                                      const QVariantList &range)
{
    Intent intent;
    intent.op = QStringLiteral("plot.instrument");
    intent.params[QStringLiteral("dataset")] = datasetId;
    intent.params[QStringLiteral("kind")] = kind;
    intent.params[QStringLiteral("roles")] = roles;
    intent.params[QStringLiteral("channels")] = channels;
    if (!range.isEmpty())
        intent.params[QStringLiteral("range")] = range;

    const Result<IntentResult> result = m_executor.execute(intent);
    if (!result)
        return;

    const QVariantMap payload = result.value().payload.toMap();
    const QVariantMap scalars = payload.value(QStringLiteral("scalars")).toMap();
    const QVariantMap series = payload.value(QStringLiteral("series")).toMap();

    switchKind(kind);

    if (kind == QStringLiteral("attitude"))
    {
        m_attitude->setRollPitch(scalar(scalars, QStringLiteral("roll")),
                                 scalar(scalars, QStringLiteral("pitch")));
    }
    else if (kind == QStringLiteral("compass"))
    {
        m_compass->setHeading(scalar(scalars, QStringLiteral("yaw")));
    }
    else if (kind == QStringLiteral("gauge"))
    {
        const QVariantMap valueSeries = series.value(QStringLiteral("value")).toMap();
        m_gauge->setTitle(valueSeries.value(QStringLiteral("name")).toString());
        if (range.size() == 2)
            m_gauge->setRange(range.at(0).toDouble(), range.at(1).toDouble());
        else if (payload.value(QStringLiteral("range")).toList().size() == 2)
        {
            const QVariantList r = payload.value(QStringLiteral("range")).toList();
            m_gauge->setRange(r.at(0).toDouble(), r.at(1).toDouble());
        }
        if (scalars.contains(QStringLiteral("value")))
            m_gauge->setValue(scalars.value(QStringLiteral("value")).toDouble());
        else
            m_gauge->setValueMissing();
    }
    else if (kind == QStringLiteral("histogram"))
    {
        const auto *data = bindRole(QStringLiteral("value"), series);
        m_histogram->setData(data);
    }
    else if (kind == QStringLiteral("fft"))
    {
        const auto *data = bindRole(QStringLiteral("value"), series);
        const QVariantMap valueM = series.value(QStringLiteral("value")).toMap();
        const QVariantList times = valueM.value(QStringLiteral("time")).toList();
        // 采样间隔：取首两个时间点的差（秒）。等间隔数据与 kernel 抽稀口径一致。
        double dt = 0.0;
        if (times.size() >= 2)
            dt = times.at(1).toDouble() - times.at(0).toDouble();
        m_spectrum->setSampleDt(dt);
        m_spectrum->setData(data);
    }
    else if (kind == QStringLiteral("track"))
    {
        const auto *lon = bindRole(QStringLiteral("lon"), series);
        const auto *lat = bindRole(QStringLiteral("lat"), series);
        const QVariantMap lonM = series.value(QStringLiteral("lon")).toMap();
        const QVariantMap latM = series.value(QStringLiteral("lat")).toMap();
        m_track->setTrack(lon, lat, lonM.value(QStringLiteral("name")).toString(),
                          latM.value(QStringLiteral("name")).toString());
    }
    else if (kind == QStringLiteral("traj3d"))
    {
        const auto *x = bindRole(QStringLiteral("x"), series);
        const auto *y = bindRole(QStringLiteral("y"), series);
        const auto *z = bindRole(QStringLiteral("z"), series);
        const QVariantMap xM = series.value(QStringLiteral("x")).toMap();
        const QVariantMap yM = series.value(QStringLiteral("y")).toMap();
        const QVariantMap zM = series.value(QStringLiteral("z")).toMap();
        m_traj->setTracks(x, y, z, xM.value(QStringLiteral("name")).toString(),
                          yM.value(QStringLiteral("name")).toString(),
                          zM.value(QStringLiteral("name")).toString());
    }
    else if (kind == QStringLiteral("bar"))
    {
        // 柱状图：每条通道一根柱，高度取该通道末值（与老画布"各通道当前值对比"一致）。
        const QVariantList channelsOut = payload.value(QStringLiteral("channels")).toList();
        QStringList labels;
        QVector<double> values;
        QVector<QColor> colors;
        for (int i = 0; i < channelsOut.size(); ++i)
        {
            const QVariantMap m = channelsOut.at(i).toMap();
            const QVariantList v = m.value(QStringLiteral("value")).toList();
            if (v.isEmpty())
                continue;
            labels.append(m.value(QStringLiteral("name")).toString());
            values.append(v.last().toDouble());
            colors.append(kPalette.at(i % kPalette.size()));
        }
        m_bar->setBars(labels, values, colors);
    }
    else if (kind == QStringLiteral("waterfall"))
    {
        const QVariantList channelsOut = payload.value(QStringLiteral("channels")).toList();
        m_wfBuffers.resize(channelsOut.size());
        m_wfData.clear();
        m_wfData.reserve(channelsOut.size());

        QVector<const nextpilot::plotscope::fastplot::IPlotData *> data;
        QVector<QColor> colors;
        for (int i = 0; i < channelsOut.size(); ++i)
        {
            const QVariantMap m = channelsOut.at(i).toMap();
            RoleBuffer &b = m_wfBuffers[i];
            b.time = toDoubleList(m.value(QStringLiteral("time")).toList());
            b.value = toDoubleList(m.value(QStringLiteral("value")).toList());
            if (b.value.isEmpty())
                continue;
            ++m_revision;
            m_wfData.push_back(nextpilot::plotscope::fastplot::ArrayData(
                b.time.constData(), b.value.constData(), b.value.size(), m_revision));
            data.push_back(&m_wfData.last());
            colors.push_back(kPalette.at(i % kPalette.size()));
        }
        m_waterfall->setSeries(data, colors);
    }
}

QImage InstrumentView::grabInstrument()
{
    return grab().toImage();
}

QWidget *createInstrumentPanel(AppContext &appCtx, IIntentExecutor &executor,
                               QWidget *parent)
{
    auto *view = new InstrumentView(executor, parent);
    view->setAppContext(&appCtx);
    // 图库切图型：仪表盘/罗盘/地平仪/3D 等由外壳切到本视图后，
    // 通过这个 view action 指定 kind 并**立即从当前活动通道喂数据**
    // （与脚本/回放同一入口，不再留空表盘）。
    appCtx.interactions().registerViewAction(QStringLiteral("plot.instrument.setKind"),
                              [view](const QVariantMap &params) -> Result<QVariant> {
                                  const QString kind = params.value(QStringLiteral("kind")).toString();
                                  if (kind.isEmpty())
                                      return nextpilot::plotscope::foundation::fail(QStringLiteral("ekind"), QStringLiteral("缺少仪表 kind"));
                                  view->switchKind(kind);
                                  // 绑定失败（无活动数据集/无匹配通道）不使切型失败：仪表明仍切过去
                                  // 显示空态，由调用方决定如何提示。
                                  view->renderFromActive(kind);
                                  return QVariant(kind);
                              });
    // 数据集树双击新通道时，若当前仪表可见则跟着刷新——与画布 addChannel 并列，
    // 走同一活动通道信号，不引入仪表对数据源的直接依赖。
    QObject::connect(&appCtx, &AppContext::activeChannelChanged, view,
                     [view](int, const QString &, const QString &) {
                         if (view->isVisible() && !view->currentKind().isEmpty())
                             view->renderFromActive(view->currentKind());
                     });
    return view;
}

// 视图层自注册：仪表面板类型 "plot-instrument"，器官按 id 请求，外壳不认识本类。
NG_REGISTER_PANEL("plot-instrument", plotInstrument, createInstrumentPanel)

} // namespace nextpilot::plotscope::gui

