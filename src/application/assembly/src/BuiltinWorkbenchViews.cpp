#include "BuiltinWorkbenchViews.h"

#include "ContributionParser.h"
#include "InstantiationService.h"
#include "ViewRegistry.h"

// 声明半：纯数据，无任何 gui/views 依赖（复审必修 2：装配层次不得倒挂）。
// 不写 using namespace（笔 5 判据：0），类型全限定。

namespace nextpilot::plotscope::assembly {

bool declareBuiltinWorkbenchViews(instantiation::InstantiationService *services)
{
    if (!services)
        return false;
    auto *views = services->getService<nextpilot::plotscope::registry::ViewRegistry>(QStringLiteral("views.registry"));
    if (!views)
        return false;

    nextpilot::plotscope::registry::ContributesDoc doc;
    doc.owner = QStringLiteral("core.workbench");

    nextpilot::plotscope::registry::ContributedViewContainer sidebar;
    sidebar.id = QStringLiteral("workbench.sidebar");
    sidebar.title = QStringLiteral("侧栏");
    sidebar.area = QStringLiteral("sidebar"); // 壳落区映射：sidebar → PrimarySidebar
    sidebar.order = 0;
    doc.viewContainers.append(sidebar);

    nextpilot::plotscope::registry::ContributedViewContainer panel;
    panel.id = QStringLiteral("workbench.panel");
    panel.title = QStringLiteral("面板");
    panel.area = QStringLiteral("bottom"); // 壳落区映射：bottom → Panel（底部面板组）
    panel.order = 0;
    doc.viewContainers.append(panel);

    nextpilot::plotscope::registry::ContributedView view;
    view.container = QStringLiteral("workbench.panel");
    // owner 由 doc.owner 统一注入，此处无需预设（declare 覆盖所有子项）。

    view.id = QStringLiteral("workbench.panel.output");
    view.name = QStringLiteral("输出");
    view.icon = QStringLiteral("output");
    view.order = 0;
    doc.views.append(view);

    view.id = QStringLiteral("workbench.panel.markers");
    view.name = QStringLiteral("问题");
    view.icon = QStringLiteral("problems");
    view.order = 1;
    doc.views.append(view);

    view.id = QStringLiteral("workbench.panel.repl");
    view.name = QStringLiteral("调试控制台");
    view.icon = QStringLiteral("debug_console");
    view.order = 2;
    doc.views.append(view);

    view.id = QStringLiteral("diagnostics"); // ⚠️ 无 VSCode 对位
    view.name = QStringLiteral("诊断");
    view.icon = QStringLiteral("diagnostics");
    view.order = 3;
    doc.views.append(view);

    views->declare(doc);
    return true;
}

} // namespace nextpilot::plotscope::assembly
