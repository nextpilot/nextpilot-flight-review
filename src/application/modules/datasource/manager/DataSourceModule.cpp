#include "DataSourceModule.h"
#include "DataSourceSession.h"
#include "ModuleBridge.h"
#include "MainWindow.h"
#include "DataSourceRegistry.h"
#include "SourceModel.h"

#include <QHash>

using ps::data_source::SourceData;
#include <QPair>
#include <QVector>
#include <QWidget>

namespace ps::components
{
namespace
{
struct DataSourceRegistrar
{
    DataSourceRegistrar()
    {
        ExtensionRegistry::instance().addExtension(
            ExtensionDescriptor{QStringLiteral("data.source"), QStringLiteral("数据源"),
                                QStringLiteral("1.0"), ExtensionType::Builtin},
            [] { return std::make_unique<DataSourceComponent>(); });
    }
};
static DataSourceRegistrar g_dataSourceRegistrar;
} // namespace

ComponentManifest DataSourceComponent::manifest() const
{
    return {QStringLiteral("data.source"), QStringLiteral("数据源"), QStringLiteral("1.0"),
            {QStringLiteral("datasource"), QStringLiteral("dock")}};
}

DataSourceSession *dataSourceSession(MainWindow *mw)
{
    auto *existing = mw->findChild<QObject *>(QStringLiteral("ps.dataSourceSession"));
    if (existing)
        return static_cast<DataSourceSession *>(existing);
    auto *c = new DataSourceSession(mw);
    c->setObjectName(QStringLiteral("ps.dataSourceSession"));
    return c;
}

ps::data_source::SourceStore *dataSourceStore(MainWindow *mw) { return dataSourceSession(mw)->store(); }
void addDataSourceTimeMarker(MainWindow *mw, double t, const QString &label, bool prompt) { dataSourceSession(mw)->addTimeMarker(t, label, prompt); }
void removeDataSourceTimeMarker(MainWindow *mw, const QString &id) { dataSourceSession(mw)->removeTimeMarker(id); }
void moveDataSourceTimeMarker(MainWindow *mw, const QString &id, double to) { dataSourceSession(mw)->moveTimeMarker(id, to); }
const QVector<const PsChannelInfo *> &dataSourceChannels(MainWindow *mw) { return dataSourceSession(mw)->channels(); }
QVector<ps::TimeBookmark> &dataSourceTimeMarkers(MainWindow *mw) { return dataSourceSession(mw)->timeMarkers(); }

void DataSourceComponent::contribute(Registry &registry, HostContext &)
{
    for (const DataSourceSpec &source : registry.dataSources())
        DataSourceRegistry::instance().registerDescriptor(source.descriptor, {}, source.editor);
    registerDataSourceContributions(registry);
    registry.addStatusChip(StatusChipContribution{QStringLiteral("source"), QStringLiteral("无数据"), 0});
    registry.addWindowHook([](QWidget *w) {
        auto *mw = qobject_cast<MainWindow *>(w);
        if (!mw)
            return;
        dataSourceSession(mw);
        ps::bridge::setSourceViewsProvider(mw, [](MainWindow *m) {
            QVector<ps::bridge::SourceView> out;
            for (const SourceData &src : dataSourceSession(m)->store()->items())
                out.append(ps::bridge::SourceView{src.id, src.path, src.name, src.alias, src.channels});
            return out;
        });
    });
}


} // namespace ps::components

namespace ps::bridge
{
namespace
{
QHash<MainWindow *, SourceViewsProvider> &providers()
{
    static QHash<MainWindow *, SourceViewsProvider> p;
    return p;
}
} // namespace

void setSourceViewsProvider(MainWindow *mw, SourceViewsProvider provider)
{
    if (!mw)
        return;
    if (provider)
        providers().insert(mw, std::move(provider));
    else
        providers().remove(mw);
}

QVector<SourceView> sourceViews(MainWindow *mw)
{
    const auto it = providers().constFind(mw);
    return it == providers().constEnd() ? QVector<SourceView>{} : it.value()(mw);
}
QVector<const PsChannelInfo *> sourceChannels(MainWindow *mw)
{
    return ps::components::dataSourceChannels(mw);
}
QVector<ps::TimeBookmark> timeMarkers(MainWindow *mw)
{
    return ps::components::dataSourceTimeMarkers(mw);
}
void addTimeMarker(MainWindow *mw, double t, const QString &label, bool prompt)
{
    ps::components::addDataSourceTimeMarker(mw, t, label, prompt);
}
void removeTimeMarker(MainWindow *mw, const QString &id)
{
    ps::components::removeDataSourceTimeMarker(mw, id);
}
void moveTimeMarker(MainWindow *mw, const QString &id, double to)
{
    ps::components::moveDataSourceTimeMarker(mw, id, to);
}
} // namespace ps::bridge
