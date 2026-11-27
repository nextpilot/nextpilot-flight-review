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
        // 对照表）；A2d 第③步：region 字段退场——落区由 viewsContainers[].area
        // 决定，openView 以声明为准，占位只带 viewId。
        {QStringLiteral("plot.properties"), QString(), false}, // A2c：归位 plot.properties（owner plot）
        {QStringLiteral("workbench.panel.output"), QString(), false},
        {QStringLiteral("workbench.panel.markers"), QString(), false},
        {QStringLiteral("workbench.panel.repl"), QString(), false},
        {QStringLiteral("diagnostics"), QString(), false},
        {QStringLiteral("script.console"), QString(), true}, // A2c：归位 script.console（owner script）
    };
    spec.moduleIds = {
        QStringLiteral("mod.data"), QStringLiteral("mod.plot"), QStringLiteral("mod.script"),
        QStringLiteral("mod.protocol"), QStringLiteral("mod.analysis"), QStringLiteral("mod.settings"),
        QStringLiteral("mod.report"), QStringLiteral("mod.workspace"),
    };
    return spec;
}

} // namespace nextpilot::plotscope::registry
