#include "InstrumentView.h"

#include "AppContext.h"
#include "PanelFactoryRegistry.h"
#include "IIntentExecutor.h"
#include "Intent.h"
#include "Result.h"

#include <QStackedLayout>
#include <QVariantList>
#include <QVariantMap>

#include <fastplot/AttitudeIndicator.h>
#include <fastplot/CompassRose.h>
#include <fastplot/DialGauge.h>
#include <fastplot/TrackPlot.h>
#include <fastplot/Trajectory3D.h>
#include <fastplot/WaterfallPlot.h>

namespace ng::gui
{
using ng::core::AppContext;
using ng::core::IIntentExecutor;
using ng::core::Intent;
using ng::core::IntentResult;
using ng::core::Result;

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
} // namespace

InstrumentView::InstrumentView(core::IIntentExecutor &executor, QWidget *parent)
    : QWidget(parent), m_executor(executor)
{
    m_stack = new QStackedLayout(this);

    m_attitude = new fastplot::AttitudeIndicator();
    m_compass = new fastplot::CompassRose();
    m_gauge = new fastplot::DialGauge();
    m_track = new fastplot::TrackPlot();
    m_traj = new fastplot::Trajectory3D();
    m_waterfall = new fastplot::WaterfallPlot();

    m_stack->addWidget(m_attitude);
    m_stack->addWidget(m_compass);
    m_stack->addWidget(m_gauge);
    m_stack->addWidget(m_track);
    m_stack->addWidget(m_traj);
    m_stack->addWidget(m_waterfall);

    // 瀑布缓冲一次性预留上限：之后只 resize 不改容量，指针才稳定。
    m_wfBuffers.reserve(64);
    m_wfData.reserve(64);
}

void InstrumentView::switchKind(const QString &kind)
{
    m_kind = kind;
    QWidget *target = m_attitude;
    if (kind == QStringLiteral("compass"))
        target = m_compass;
    else if (kind == QStringLiteral("gauge"))
        target = m_gauge;
    else if (kind == QStringLiteral("track"))
        target = m_track;
    else if (kind == QStringLiteral("traj3d"))
        target = m_traj;
    else if (kind == QStringLiteral("waterfall"))
        target = m_waterfall;
    m_stack->setCurrentWidget(target);
}

const fastplot::ArrayData *InstrumentView::bindRole(const QString &role, const QVariantMap &series)
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

void InstrumentView::renderInstrument(core::DatasetId datasetId, const QString &kind,
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
    else if (kind == QStringLiteral("waterfall"))
    {
        const QVariantList channelsOut = payload.value(QStringLiteral("channels")).toList();
        m_wfBuffers.resize(channelsOut.size());
        m_wfData.clear();
        m_wfData.reserve(channelsOut.size());

        QVector<const fastplot::IPlotData *> data;
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
            m_wfData.push_back(fastplot::ArrayData(
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

QWidget *createInstrumentPanel(AppContext & /*appCtx*/, IIntentExecutor &executor,
                               QWidget *parent)
{
    return new InstrumentView(executor, parent);
}

// 视图层自注册：仪表面板类型 "plot-instrument"，器官按 id 请求，外壳不认识本类。
NG_REGISTER_PANEL("plot-instrument", plotInstrument, createInstrumentPanel)

} // namespace ng::gui

#include "InstrumentView.moc"
