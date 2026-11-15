#include "MainWindow.h"
#include "PlotModule.h"

namespace ps::components
{
namespace
{
struct PlotRegistrar
{
    PlotRegistrar()
    {
        ExtensionRegistry::instance().addExtension(
            ExtensionDescriptor{QStringLiteral("plot.core"), QStringLiteral("绘图"),
                                QStringLiteral("1.0"), ExtensionType::Builtin},
            [] { return std::make_unique<PlotComponent>(); });
    }
};
static PlotRegistrar g_plotRegistrar;
} // namespace

ComponentManifest PlotComponent::manifest() const
{
    return {QStringLiteral("plot.core"), QStringLiteral("绘图"), QStringLiteral("1.0"),
            {QStringLiteral("plot"), QStringLiteral("dock")}};
}

void PlotComponent::contribute(Registry &registry, const HostContext &)
{
    registerPlotContributions(registry);
}

void registerPlotContributions(Registry &registry)
{
    registry.addDock(DockContribution{QStringLiteral("dock.prop"), QStringLiteral("属性面板"),
                                      Qt::RightDockWidgetArea, true, 1,
                                      QStringLiteral(":/icons/prop.svg"), [](QWidget *p){ auto *w=qobject_cast<MainWindow*>(p); return w?w->createDock(QStringLiteral("dock.prop"), p):nullptr; }});
    registry.addDock(DockContribution{QStringLiteral("dock.tree"), QStringLiteral("对象树"),
                                      Qt::RightDockWidgetArea, true, 2,
                                      QStringLiteral(":/icons/panel.svg"), [](QWidget *p){ auto *w=qobject_cast<MainWindow*>(p); return w?w->createDock(QStringLiteral("dock.tree"), p):nullptr; }});
    registry.addRibbon(RibbonContribution{QStringLiteral("绘图"), {}, {},
        {{QStringLiteral("页面"),
             {{QStringLiteral("page.new"), QStringLiteral("新建页"), {}, true},
              {QStringLiteral("page.addCanvas"), QStringLiteral("添加画布"), {}, true},
              {QStringLiteral("page.splitHorizontal"), QStringLiteral("向右拆分"), {}, true},
              {QStringLiteral("page.splitVertical"), QStringLiteral("向下拆分"), {}, true}}},
         {QStringLiteral("画布布局"), {}, QStringLiteral("layoutPicker")},
         {QStringLiteral("显示"), {}, QStringLiteral("viewToggles")},
         {QStringLiteral("图表类型"), {}, QStringLiteral("plotGallery")}},
        2});
}

} // namespace ps::components
