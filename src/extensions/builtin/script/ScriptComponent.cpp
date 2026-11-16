#include "ScriptComponent.h"

#include <QString>

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

// 这里只保留 Lua 执行能力；脚本面板、脚本 Ribbon 归未来的独立脚本模块，
// 不再由绘图页贡献
void ScriptComponent::contribute(Registry &registry, HostContext &)
{
    registry.addScriptRunner(ScriptRunnerContribution{
        [](const QString &source, const QVector<ScriptSeries> &inputs,
           const QMap<QString, double> &params, const QString &language) {
            return ScriptEngine::run(source, inputs, params, language);
        }});
}

} // namespace ps::components
