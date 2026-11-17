#include "DataSourceModule.h"
#include "DataSourceSession.h"
#include "ModuleBridge.h"
#include "ModuleContext.h"
#include "DataSourceRegistry.h"
#include "DataSourceModel.h"

#include <QHash>

using ps::data_source::DataSource;
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

DataSourceSession *dataSourceSession(ps::runtime::ModuleContext &ctx)
{
    auto *existing = ctx.window()->findChild<QObject *>(QStringLiteral("ps.dataSourceSession"));
    if (existing)
        return static_cast<DataSourceSession *>(existing);
    auto *c = new DataSourceSession(ctx);
    c->setObjectName(QStringLiteral("ps.dataSourceSession"));
    return c;
}

int dataSourceActiveId(ps::runtime::ModuleContext *ctx)
{
    return dataSourceSession(*ctx)->activeSourceId();
}
QString dataSourceActivePath(ps::runtime::ModuleContext *ctx)
{
    auto *session = dataSourceSession(*ctx);
    auto *source = session->sourceById(session->activeSourceId());
    return source ? source->config.path : QString();
}
QString dataSourceActiveFormat(ps::runtime::ModuleContext *ctx)
{
    auto *session = dataSourceSession(*ctx);
    auto *source = session->sourceById(session->activeSourceId());
    return source ? source->config.fmt : QString();
}
void addDataSourceTimeMarker(ps::runtime::ModuleContext *ctx, double t, const QString &label, bool prompt) { dataSourceSession(*ctx)->addTimeMarker(t, label, prompt); }
void removeDataSourceTimeMarker(ps::runtime::ModuleContext *ctx, const QString &id) { dataSourceSession(*ctx)->removeTimeMarker(id); }
void moveDataSourceTimeMarker(ps::runtime::ModuleContext *ctx, const QString &id, double to) { dataSourceSession(*ctx)->moveTimeMarker(id, to); }
void renameDataSourceTimeMarker(ps::runtime::ModuleContext *ctx, const QString &id, const QString &label) { dataSourceSession(*ctx)->renameTimeMarker(id, label); }
const QVector<const PsChannelInfo *> &dataSourceChannels(ps::runtime::ModuleContext *ctx) { return dataSourceSession(*ctx)->channels(); }
QVector<ps::TimeBookmark> &dataSourceTimeMarkers(ps::runtime::ModuleContext *ctx) { return dataSourceSession(*ctx)->timeMarkers(); }

void DataSourceComponent::contribute(Registry &registry, HostContext &)
{
    for (const DataSourceSpec &source : registry.dataSources())
        DataSourceRegistry::instance().registerDescriptor(source.descriptor, {}, source.editor);
    registerDataSourceContributions(registry);
    registry.addStatusChip(StatusChipContribution{QStringLiteral("source"), QStringLiteral("无数据"), 0});
    registry.addModuleHook([](ps::runtime::ModuleContext &ctx) {
        dataSourceSession(ctx);
        ps::bridge::setSourceViewsProvider(&ctx, [](ps::runtime::ModuleContext *m) {
            QVector<ps::bridge::SourceView> out;
            for (const DataSource &src : dataSourceSession(*m)->sources())
                out.append(ps::bridge::SourceView{src.config.id, src.config.path, src.config.name, src.config.alias, src.channels});
            return out;
        });
    });
}


} // namespace ps::components

namespace ps::bridge
{
namespace
{
QHash<ps::runtime::ModuleContext *, SourceViewsProvider> &providers()
{
    static QHash<ps::runtime::ModuleContext *, SourceViewsProvider> p;
    return p;
}
} // namespace

void setSourceViewsProvider(ps::runtime::ModuleContext *ctx, SourceViewsProvider provider)
{
    if (!ctx)
        return;
    if (provider)
        providers().insert(ctx, std::move(provider));
    else
        providers().remove(ctx);
}

QVector<SourceView> sourceViews(ps::runtime::ModuleContext *ctx)
{
    const auto it = providers().constFind(ctx);
    return it == providers().constEnd() ? QVector<SourceView>{} : it.value()(ctx);
}
QVector<const PsChannelInfo *> sourceChannels(ps::runtime::ModuleContext *ctx)
{
    return ps::components::dataSourceChannels(ctx);
}
QVector<ps::TimeBookmark> timeMarkers(ps::runtime::ModuleContext *ctx)
{
    return ps::components::dataSourceTimeMarkers(ctx);
}
void addTimeMarker(ps::runtime::ModuleContext *ctx, double t, const QString &label, bool prompt)
{
    ps::components::addDataSourceTimeMarker(ctx, t, label, prompt);
}
void removeTimeMarker(ps::runtime::ModuleContext *ctx, const QString &id)
{
    ps::components::removeDataSourceTimeMarker(ctx, id);
}
void moveTimeMarker(ps::runtime::ModuleContext *ctx, const QString &id, double to)
{
    ps::components::moveDataSourceTimeMarker(ctx, id, to);
}
void renameTimeMarker(ps::runtime::ModuleContext *ctx, const QString &id, const QString &label)
{
    ps::components::renameDataSourceTimeMarker(ctx, id, label);
}
} // namespace ps::bridge
