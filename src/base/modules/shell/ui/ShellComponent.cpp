#include "ShellComponent.h"

#include "DataSourceModule.h"
#include "PlotModule.h"

#include <QString>

namespace ps::components
{
namespace
{
struct ShellRegistrar
{
    ShellRegistrar()
    {
        ExtensionRegistry::instance().addExtension(
            ExtensionDescriptor{QStringLiteral("shell.core"), QStringLiteral("内置工作区"),
                                QStringLiteral("1.0"), ExtensionType::Builtin},
            [] { return std::make_unique<ShellComponent>(); });
    }
};
static ShellRegistrar g_shellRegistrar;
} // namespace

ComponentManifest ShellComponent::manifest() const
{
    return {QStringLiteral("shell.core"), QStringLiteral("内置工作区"), QStringLiteral("1.0"),
            {QStringLiteral("ribbon")}};
}

void ShellComponent::contribute(Registry &registry, const HostContext &)
{

    registerDataSourceContributions(registry);
    registry.addDock(DockContribution{QStringLiteral("dock.prop"), QStringLiteral("属性面板"),
                                      Qt::RightDockWidgetArea, true, 1, nullptr});
    registry.addDock(DockContribution{QStringLiteral("dock.tree"), QStringLiteral("对象树"),
                                      Qt::RightDockWidgetArea, true, 2, nullptr});
    registry.addDock(DockContribution{QStringLiteral("dock.script"), QStringLiteral("脚本 · Lua 5.4"),
                                      Qt::RightDockWidgetArea, true, 4, nullptr});
    registry.addDock(DockContribution{QStringLiteral("dock.log"), QStringLiteral("日志"),
                                      Qt::BottomDockWidgetArea, true, 5, nullptr});

    registry.addRibbon(RibbonContribution{QStringLiteral("文件"), {}, {},
        {{QStringLiteral("文件"),
             {{QStringLiteral("file.open"), QStringLiteral("打开文件"), {}, true},
              {QStringLiteral("file.importLayout"), QStringLiteral("导入布局"), {}, true},
              {QStringLiteral("file.settings"), QStringLiteral("设置"), {}, true}}},
         {QStringLiteral("导出"),
             {{QStringLiteral("file.saveLayout"), QStringLiteral("保存布局"), {}, true},
              {QStringLiteral("file.exportSettings"), QStringLiteral("导出设置"), {}, false},
              {QStringLiteral("file.exportData"), QStringLiteral("导出数据"), {}, false}}},
         {QStringLiteral("截图"),
             {{QStringLiteral("file.screenshot"), QStringLiteral("截图当前页"), {}, true}}}},
        0});


    registerPlotContributions(registry);

    registry.addRibbon(RibbonContribution{QStringLiteral("视图"), {}, {},
        {{QStringLiteral("面板"),
             {{QStringLiteral("view.toggleDock"), QStringLiteral("数据源"), QStringLiteral("dock.src"), true},
              {QStringLiteral("view.toggleDock"), QStringLiteral("属性"), QStringLiteral("dock.prop"), false},
              {QStringLiteral("view.toggleDock"), QStringLiteral("脚本"), QStringLiteral("dock.script"), false},
              {QStringLiteral("view.toggleDock"), QStringLiteral("日志"), QStringLiteral("dock.log"), false}}}},
        5});
}

} // namespace ps::components
