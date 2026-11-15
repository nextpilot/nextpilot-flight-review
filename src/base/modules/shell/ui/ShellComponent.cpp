#include "ShellComponent.h"
#include "MainWindow.h"

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
            {QStringLiteral("ribbon")}};
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
                                          dock->setWidget(view);
                                          if (auto *w = qobject_cast<MainWindow *>(parent))
                                              w->attachLogView(view);
                                          return dock;
                                      }});

    registry.addRibbon(RibbonContribution{QStringLiteral("文件"), {}, {},
        {{QStringLiteral("文件"),
             {{QStringLiteral("file.open"), QStringLiteral("打开文件"), {}, true, QStringLiteral("open")},
              {QStringLiteral("file.importLayout"), QStringLiteral("导入布局"), {}, true, QStringLiteral("import")},
              {QStringLiteral("file.settings"), QStringLiteral("设置"), {}, true, QStringLiteral("settings")}}},
         {QStringLiteral("导出"),
             {{QStringLiteral("file.saveLayout"), QStringLiteral("保存布局"), {}, true, QStringLiteral("export")},
              {QStringLiteral("file.exportSettings"), QStringLiteral("导出设置"), {}, false, QStringLiteral("export_cfg")},
              {QStringLiteral("file.exportData"), QStringLiteral("导出数据"), {}, false, QStringLiteral("export_data")}}},
         {QStringLiteral("截图"),
             {{QStringLiteral("file.screenshot"), QStringLiteral("截图当前页"), {}, true, QStringLiteral("screenshot")}}}},
        0});



}

} // namespace ps::components
