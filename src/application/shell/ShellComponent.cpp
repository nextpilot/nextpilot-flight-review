#include "ShellComponent.h"
#include "LogManager.h"

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
