#include "BuiltinViewProviders.h"

#include "DiagnosticsPanel.h"
#include "DebugConsolePanel.h"
#include "OutputPanel.h"
#include "ProblemsPanel.h"

#include "ContributionParser.h"
#include "InstantiationService.h"
#include "ViewRegistry.h"

// 注册半（复审必修 2）：工厂实现留 gui/views、经头文件导出，本 TU 在 gui 层
// 把它们登记进 ViewRegistry；property-panel 归 plot 模块（笔 4），不在此列。

namespace nextpilot::plotscope::gui {

bool installBuiltinWorkbenchViewProviders(instantiation::InstantiationService *services)
{
    if (!services)
        return false;
    auto *views = services->getService<nextpilot::plotscope::registry::ViewRegistry>(QStringLiteral("views.registry"));
    if (!views)
        return false;

    const QString owner = QStringLiteral("core.workbench");
    bool ok = true;
    ok &= views->registerViewProvider(QStringLiteral("workbench.panel.output"), &createOutputPanel, owner);
    ok &= views->registerViewProvider(QStringLiteral("workbench.panel.markers"), &createProblemsPanel, owner);
    ok &= views->registerViewProvider(QStringLiteral("workbench.panel.repl"), &createDebugConsolePanel, owner);
    ok &= views->registerViewProvider(QStringLiteral("diagnostics"), &createDiagnosticsPanel, owner);
    return ok;
}

} // namespace nextpilot::plotscope::gui
