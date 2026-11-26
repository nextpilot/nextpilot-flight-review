#include "AutomotiveViewFactories.h"

#include "ContributionParser.h"
#include "IIntentExecutor.h"
#include "InstantiationService.h"
#include "ViewRegistry.h"

#include "AppContext.h"
#include "CanBrowserPanel.h"

// automotive 视图装配·注册半（A2c）：声明在此落（owner "automotive"），provider
// 登记也在此。不写 using namespace dataset|parser（A2c 判据：gui 面 0）。

namespace nextpilot::plotscope::ext::automotive {

bool installAutomotiveViewFactories(instantiation::InstantiationService *services, context::AppContext *appCtx,
                                    intent::IIntentExecutor *executor)
{
    Q_UNUSED(executor); // 领域面板自己取数，不经意图层（createCanBrowserPanel 同款）
    if (!services || !appCtx)
        return false;
    auto *views = services->getService<nextpilot::plotscope::registry::ViewRegistry>(QStringLiteral("views.registry"));
    if (!views)
        return false;

    const QString owner = QStringLiteral("automotive");

    // ---- 声明半（owner "automotive"）：先于 provider 登记 ----
    {
        nextpilot::plotscope::registry::ContributesDoc doc;
        doc.owner = owner;

        nextpilot::plotscope::registry::ContributedViewContainer auxiliary;
        auxiliary.id = QStringLiteral("automotive.auxiliary");
        auxiliary.title = QStringLiteral("汽车工具箱");
        auxiliary.area = QStringLiteral("auxiliary"); // → SecondarySidebar
        auxiliary.order = 3;
        doc.viewContainers.append(auxiliary);

        nextpilot::plotscope::registry::ContributedView view;
        view.container = QStringLiteral("automotive.auxiliary");
        view.id = QStringLiteral("automotive.canBrowser");
        view.name = QStringLiteral("CAN 报文");
        view.icon = QStringLiteral("can_bus");
        view.order = 0;
        doc.views.append(view);

        views->declare(doc);
    }

    // ---- 注册半 ----
    return views->registerViewProvider(QStringLiteral("automotive.canBrowser"),
                                       &nextpilot::plotscope::ext::automotive::createCanBrowserPanel, owner);
}

} // namespace nextpilot::plotscope::ext::automotive
