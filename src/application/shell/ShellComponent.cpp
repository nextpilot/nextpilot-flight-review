#include "ShellComponent.h"
#include "LogManager.h"
#include "MainWindow.h"
#include "PropertyManager.h"

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
                                QStringLiteral("1.0"), ExtensionType::Builtin, {}, {}, 0, nullptr},
            [] { return std::make_unique<ShellComponent>(); });
    }
};
static ShellRegistrar g_shellRegistrar;
} // namespace

ComponentManifest ShellComponent::manifest() const
{
    return {QStringLiteral("shell.core"), QStringLiteral("内置工作区"), QStringLiteral("1.0"),
            {QStringLiteral("ribbon"), QStringLiteral("dock")}};
}

void ShellComponent::contribute(Registry &registry, HostContext &)
{
    registry.addDock(LogManager::dockContribution());
    // 属性面板/对象树由 shell 统一提供；没有模块注册节点时保持隐藏。
    registry.addDock(DockContribution{QStringLiteral("dock.prop"), QStringLiteral("属性面板"),
                                      Qt::RightDockWidgetArea, true, 1, QStringLiteral(":/icons/prop.svg"),
                                      [](QWidget *p) {
                                          auto *mw = qobject_cast<MainWindow *>(p);
                                          return mw ? mw->properties().createPanelView() : nullptr;
                                      }});
    registry.addDock(DockContribution{QStringLiteral("dock.tree"), QStringLiteral("对象树"),
                                      Qt::RightDockWidgetArea, true, 2, QStringLiteral(":/icons/prop.svg"),
                                      [](QWidget *p) {
                                          auto *mw = qobject_cast<MainWindow *>(p);
                                          return mw ? mw->properties().createTreeView() : nullptr;
                                      }});

    RibbonContribution filePage;
    filePage.page = QStringLiteral("文件");
    filePage.order = 0;
    filePage.panels = {
        RibbonPanel{QStringLiteral("文件"),
            {RibbonItem{QStringLiteral("file.open"), {}, {}, true, {}},
             RibbonItem{QStringLiteral("file.importLayout"), {}, {}, true, {}},
             RibbonItem{QStringLiteral("file.settings"), {}, {}, true, {}}},
            {}},
        RibbonPanel{QStringLiteral("导出"),
            {RibbonItem{QStringLiteral("file.saveLayout"), {}, {}, true, {}},
             RibbonItem{QStringLiteral("file.exportSettings"), {}, {}, false, {}},
             RibbonItem{QStringLiteral("file.exportData"), {}, {}, false, {}}},
            {}},
        RibbonPanel{QStringLiteral("截图"),
            {RibbonItem{QStringLiteral("file.screenshot"), {}, {}, true, {}}},
            {}},
        RibbonPanel{QStringLiteral("帮助"),
            {RibbonItem{QStringLiteral("help.about"), {}, {}, false, {}}},
            {}},
    };
    registry.addRibbon(filePage);
}
} // namespace ps::components
