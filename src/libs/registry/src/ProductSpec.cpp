#include "ProductSpec.h"

namespace nextpilot::plotscope::registry
{

ProductSpec plotScopeProductSpec()
{
    ProductSpec spec;
    spec.id = QStringLiteral("plotscope");
    spec.displayName = QStringLiteral("PlotScope");
    spec.version = QStringLiteral("0.1.0");
    spec.reportTitle = QStringLiteral("PlotScope 报告");
    spec.defaultPanels = {
        {Region::SecondarySidebar, QStringLiteral("property-panel"), QString(), false},
        {Region::Panel, QStringLiteral("output-console"), QString(), false},
        {Region::Panel, QStringLiteral("problems"), QString(), false},
        {Region::Panel, QStringLiteral("debug-console"), QString(), false},
        {Region::Panel, QStringLiteral("diagnostics"), QString(), false},
        {Region::Panel, QStringLiteral("script-console"), QString(), true},
    };
    spec.moduleIds = {
        QStringLiteral("mod.data"), QStringLiteral("mod.plot"), QStringLiteral("mod.script"),
        QStringLiteral("mod.protocol"), QStringLiteral("mod.analysis"), QStringLiteral("mod.settings"),
        QStringLiteral("mod.report"), QStringLiteral("mod.workspace"),
    };
    return spec;
}

} // namespace nextpilot::plotscope::registry
