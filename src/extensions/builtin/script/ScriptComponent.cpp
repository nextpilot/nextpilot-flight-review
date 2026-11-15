#include "MainWindow.h"
#include "ScriptComponent.h"

#include "ScriptEngine.h"

namespace ps::components
{
namespace
{
struct ScriptRegistrar
{
    ScriptRegistrar()
    {
        ExtensionRegistry::instance().addExtension(
            ExtensionDescriptor{QStringLiteral("script.runner"), QStringLiteral("脚本运行"),
                                QStringLiteral("1.0"), ExtensionType::Builtin},
            [] { return std::make_unique<ScriptComponent>(); });
    }
};
static ScriptRegistrar g_scriptRegistrar;
} // namespace

ComponentManifest ScriptComponent::manifest() const
{
    return {QStringLiteral("script.runner"), QStringLiteral("脚本运行"), QStringLiteral("1.0"),
            {QStringLiteral("script")}};
}

void ScriptComponent::contribute(Registry &registry, const HostContext &)
{
    registry.addDock(DockContribution{QStringLiteral("dock.script"), QStringLiteral("脚本 · Lua 5.4"),
                                      Qt::RightDockWidgetArea, true, 4,
                                      QStringLiteral(":/icons/script.svg"), [](QWidget *p){ auto *w=qobject_cast<MainWindow*>(p); return w?w->createDock(QStringLiteral("dock.script"), p):nullptr; }});
    registry.addRibbon(RibbonContribution{
        QStringLiteral("脚本"), {}, {},
        {{QStringLiteral("Lua"),
          {{QStringLiteral("script.run"), QStringLiteral("运行"), {}, true},
           {QStringLiteral("script.format"), QStringLiteral("格式化"), {}, false}}}},
        3});
    registry.addScriptRunner(ScriptRunnerContribution{
        [](const QString &source, const QVector<ScriptSeries> &inputs,
           const QMap<QString, double> &params, const QString &language) {
            return ScriptEngine::run(source, inputs, params, language);
        }});
}

} // namespace ps::components
