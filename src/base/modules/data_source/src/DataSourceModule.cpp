#include "MainWindow.h"
#include "DataSourceModule.h"

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

void DataSourceComponent::contribute(Registry &registry, const HostContext &)
{
    registerDataSourceContributions(registry);
}

void registerDataSourceContributions(Registry &registry)
{
    registry.addDock(DockContribution{QStringLiteral("dock.src"), QStringLiteral("数据源"),
                                      Qt::LeftDockWidgetArea, true, 0,
                                      QStringLiteral(":/icons/datasource.svg"), [](QWidget *p){ auto *w=qobject_cast<MainWindow*>(p); return w?w->createDock(QStringLiteral("dock.src"), p):nullptr; }});

    registry.addRibbon(RibbonContribution{QStringLiteral("数据"), {}, {},
        {{QStringLiteral("常用"),
             {{QStringLiteral("data.quickOpen"), QStringLiteral("快速打开"), {}, true},
              {QStringLiteral("data.recent"), QStringLiteral("最近"), {}, false},
              {QStringLiteral("data.exportSnapshot"), QStringLiteral("导出快照"), {}, false},
              {QStringLiteral("data.reload"), QStringLiteral("重载"), {}, false}}},
         {QStringLiteral("来源"), {}, QStringLiteral("sourceGallery")}},
        1});
}

} // namespace ps::components
