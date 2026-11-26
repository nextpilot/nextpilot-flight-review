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
    spec.aboutText = QStringLiteral("数据 / 绘图 / 分析软件");
    spec.aboutText += QStringLiteral("\n架构底座：Dataset / Intent / Contribution / Module / Pack");
    spec.aboutText += QStringLiteral("\n绘图库：fastplot（Figure / Axes / Line / Artist）");
    spec.defaultPanels = {
        // A2b 笔2b 第②步：gui 内置 5 点改 views 声明 id（值映射见 PR !169 正文
        // 对照表）；region 字段保留至第⑦步删（落区由 viewsContainers[].area 决定，
        // 本清单中 gui 内置项的 region 已不再被读——openView 落区以声明为准）。
        {Region::SecondarySidebar, QStringLiteral("property-panel"), QString(), false},
        {Region::Panel, QStringLiteral("workbench.panel.output"), QString(), false},
        {Region::Panel, QStringLiteral("workbench.panel.markers"), QString(), false},
        {Region::Panel, QStringLiteral("workbench.panel.repl"), QString(), false},
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
