#include "ShellComponent.h"

#include <QDockWidget>
#include <QPlainTextEdit>

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
            {QStringLiteral("ribbon"), QStringLiteral("dock")}};
}

void ShellComponent::contribute(Registry &registry, const HostContext &)
{
    registry.addDock(DockContribution{QStringLiteral("dock.log"), QStringLiteral("日志"),
                                      Qt::BottomDockWidgetArea, true, 5,
                                      QStringLiteral(":/icons/log.svg"),
                                      [](QWidget *parent) -> QWidget * {
                                          auto *dock = new QDockWidget(QStringLiteral("日志"), parent);
                                          dock->setObjectName(QStringLiteral("dock.log"));
                                          dock->setProperty("psIcon", QStringLiteral(":/icons/log.svg"));
                                          auto *view = new QPlainTextEdit(dock);
                                          view->setObjectName(QStringLiteral("dock.log.view"));
                                          dock->setWidget(view);
                                          return dock;
                                      }});

    RibbonContribution filePage;
    filePage.page = QStringLiteral("文件");
    filePage.order = 0;
    filePage.panels = {
        RibbonPanel{QStringLiteral("文件"),
            {RibbonItem{QStringLiteral("file.open"), QStringLiteral("打开文件"), {}, true, QStringLiteral("open")},
             RibbonItem{QStringLiteral("file.importLayout"), QStringLiteral("导入布局"), {}, true, QStringLiteral("import")},
             RibbonItem{QStringLiteral("file.settings"), QStringLiteral("设置"), {}, true, QStringLiteral("settings")}},
            {}},
        RibbonPanel{QStringLiteral("导出"),
            {RibbonItem{QStringLiteral("file.saveLayout"), QStringLiteral("保存布局"), {}, true, QStringLiteral("export")},
             RibbonItem{QStringLiteral("file.exportSettings"), QStringLiteral("导出设置"), {}, false, QStringLiteral("export_cfg")},
             RibbonItem{QStringLiteral("file.exportData"), QStringLiteral("导出数据"), {}, false, QStringLiteral("export_data")}},
            {}},
        RibbonPanel{QStringLiteral("截图"),
            {RibbonItem{QStringLiteral("file.screenshot"), QStringLiteral("截图当前页"), {}, true, QStringLiteral("screenshot")}},
            {}},
        RibbonPanel{QStringLiteral("帮助"),
            {RibbonItem{QStringLiteral("help.about"), QStringLiteral("关于"), {}, false, QStringLiteral("settings")}},
            {}},
    };
    registry.addRibbon(filePage);
}
} // namespace ps::components
