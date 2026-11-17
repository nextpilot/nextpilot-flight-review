#include "analysis/AnalysisModule.h"
#include "analysis/AnalysisRibbon.h"

namespace ps::components
{
namespace
{
struct AnalysisRegistrar
{
    AnalysisRegistrar()
    {
        ExtensionRegistry::instance().addExtension(
            ExtensionDescriptor{QStringLiteral("analysis.core"), QStringLiteral("分析"),
                                QStringLiteral("1.0"), ExtensionType::Builtin},
            [] { return std::make_unique<AnalysisComponent>(); });
    }
};
static AnalysisRegistrar g_analysisRegistrar;
} // namespace

ComponentManifest AnalysisComponent::manifest() const
{
    return {QStringLiteral("analysis.core"), QStringLiteral("分析"), QStringLiteral("1.0"),
            {QStringLiteral("analysis"), QStringLiteral("dock")}};
}

void AnalysisComponent::contribute(Registry &registry, HostContext &)
{
    registerAnalysisContributions(registry);
}

void registerAnalysisContributions(Registry &registry)
{
    // 分析页是唯一入口：领域工具箱只注册条目，不新增 Ribbon 页。
    registry.addRibbon(RibbonContribution{QStringLiteral("分析"), {}, {},
        {{QStringLiteral("应用库"), {}, [](QWidget *p) { ps::analysis::buildAnalysisGallery(p); }}},
        200});
}

} // namespace ps::components
