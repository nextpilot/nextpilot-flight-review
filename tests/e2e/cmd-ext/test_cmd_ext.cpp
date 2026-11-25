// A8b-M1 e2e cmd-ext：内置测试扩展一条命令 → 接到命令面板 → 执行成功 → 卸载撤回。
//
// 走与 gui_main 相同的装配路径：扩展 contributes（来自扫描/manifest）→
// assembly::ExtensionActivationService.bind(CommandRegistry*, RuntimeHost&) → host.execute(intent)。
// 用真实 RuntimeHost 与 CommandRegistry，不 mock；不构造完整 MainWindow（保持测试稳定）。

#include "App.h"
#include "AppContext.h"
#include "RuntimeHost.h"
#include "ExtensionActivationService.h"
#include "command/CommandRegistry.h"
#include "ContributionParser.h"
#include "ViewRegistry.h"
#include "IViewsService.h"
#include "IActionProvider.h"
#include "Intent.h"

#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cstdio>

using namespace nextpilot::plotscope;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::command;
using namespace nextpilot::plotscope::intent;

namespace
{
int g_failed = 0;
#define CHECK(cond)                                                            \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);                \
            ++g_failed;                                                        \
        }                                                                      \
    } while (0)
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 组合根（headless）：App 自带 AppContext + RuntimeHost。
    app::App application; // 默认空 settingsPath：内存态
    application.startup(/*headless=*/true);

    // 样例扩展的 contributes（与 tests/e2e/cmd-ext/extensions 的 manifest 等价）。
    const char *manifest = R"({
        "commands":[{"command":"hello.say","title":"说你好","category":"测试","order":10}],
        "menus":{
            "commandPalette":[{"command":"hello.say","group":"z@1"}],
            "view/title":[{"command":"hello.say","group":"navigation","when":"view == hello.view"}],
            "view/item/context":[{"command":"hello.say","group":"inline","when":"view == hello.view && item.kind == demo"}]
        },
        "viewsContainers":[{"id":"hello.container","title":"示例视图","area":"sidebar","order":20}],
        "views":[{"id":"hello.view","name":"示例","container":"hello.container","order":0}],
        "colors":{"hello.accent":{"dark":"#4fc3f7","light":"#0277bd"}},
        "statusbar":[{"id":"hello.status","side":"right","priority":30,"text":"示例: --","commandId":"hello.say"}]
    })";
    auto obj = QJsonDocument::fromJson(QByteArray(manifest)).object();
    ContributesDoc doc = parseContributes(obj, QStringLiteral("plotscope.helloworld")).doc;
    CHECK(application.context().toolbox().mountContribution(doc));

    // 装配层同款接线：激活服务把贡献命令灌进一个命令表 + 注册执行 handler。
    CommandRegistry commands;
    assembly::ExtensionActivationService activation(application.context());
    activation.bind(&commands, application.host());

    // 面板出现：贡献命令已进命令表（buildCommandItems 从 commands() 读）。
    CHECK(activation.boundCommandCount() == 1);
    CHECK(commands.contains(QStringLiteral("hello.say")));
    const CommandDescriptor *d = commands.find(QStringLiteral("hello.say"));
    CHECK(d != nullptr);
    if (d)
    {
        CHECK(d->title == QStringLiteral("说你好"));
        CHECK(d->category == QStringLiteral("测试"));
        CHECK(d->intentOp == QLatin1String("hello.say"));
        CHECK(d->provider == QLatin1String("plotscope.helloworld"));
    }

    // A8b-M2 e2e 视图用例：contributes 声明经唯一扩展注册表可读，侧栏容器/视图/视图标题动作
    // /右键项/colors/statusbar 都在；ViewRegistry 能按容器查视图并挂 provider。
    auto &ext = application.context().toolbox();
    CHECK(ext.contributedViewContainers().size() == 1);
    CHECK(ext.contributedViewContainers().at(0).item.id == QLatin1String("hello.container"));
    CHECK(ext.contributedViewContainers().at(0).item.area == QLatin1String("sidebar"));
    CHECK(ext.contributedViews().size() == 1);
    CHECK(ext.contributedViews().at(0).item.container == QLatin1String("hello.container"));
    // view/title 与 view/item/context 两个 place 的菜单项可见（侧栏视图的动作）。
    const auto titleItems = ext.menuItemsIn(QStringLiteral("view/title"));
    CHECK(titleItems.size() == 1);
    CHECK(titleItems.at(0).item.command == QLatin1String("hello.say"));
    CHECK(ext.menuItemsIn(QStringLiteral("view/item/context")).size() == 1);
    CHECK(ext.contributedColors().size() == 1);
    CHECK(ext.contributedColors().at(0).item.light == QLatin1String("#0277bd"));
    CHECK(ext.contributedStatuses().size() == 1);
    CHECK(ext.contributedStatuses().at(0).item.id == QLatin1String("hello.status"));

    // A8b-M2 裁定 52442006 Step 5 e2e：bind 已把扩展包 views 声明灌进组合根的
    // ViewRegistry（views.registry 服务），壳读回走 IViewsService（views.readback）。
    auto *services = application.host().services();
    CHECK(services != nullptr);
    auto *viewSvc =
        services ? services->getService<nextpilot::plotscope::workbench::IViewsService>(
                       QStringLiteral("views.readback"))
                 : nullptr;
    CHECK(viewSvc != nullptr);
    if (viewSvc)
    {
        const auto containers = viewSvc->containersIn(QStringLiteral("sidebar"));
        CHECK(containers.size() == 1);
        if (!containers.isEmpty())
        {
            CHECK(containers.at(0).id == QLatin1String("hello.container"));
            CHECK(containers.at(0).title == QStringLiteral("示例视图"));
        }
        const auto inContainer = viewSvc->viewsInContainer(QStringLiteral("hello.container"));
        CHECK(inContainer.size() == 1);
        if (!inContainer.isEmpty())
            CHECK(inContainer.at(0).id == QLatin1String("hello.view"));
        const auto one = viewSvc->viewForId(QStringLiteral("hello.view"));
        CHECK(one.owner == QLatin1String("plotscope.helloworld"));
        CHECK(viewSvc->viewForId(QStringLiteral("nope")).owner.isEmpty());
    }

    // viewId 注册内容工厂后 openView 走 ViewRegistry::create 同一条链（这里工厂
    // headless 返回 nullptr，只验证链路命中 provider）。
    ViewRegistry *viewsReg =
        services ? services->getService<ViewRegistry>(QStringLiteral("views.registry")) : nullptr;
    CHECK(viewsReg != nullptr);
    if (viewsReg)
    {
        CHECK(viewsReg->viewsIn(QStringLiteral("hello.container")).size() == 1);
        CHECK(viewsReg->registerViewProvider(
            QStringLiteral("hello.view"),
            [](context::AppContext &, intent::IIntentExecutor &, QWidget *) -> QWidget * {
                return nullptr;
            },
            QStringLiteral("plotscope.helloworld")));
        CHECK(viewsReg->provider(QStringLiteral("hello.view")) != nullptr);
        CHECK(viewSvc->openView(QStringLiteral("hello.view"), application.context(),
                                application.host(), nullptr) == nullptr);
    }

    // 执行成功：命令面板激活 → executeCommand(id) → intentOp → host.execute → handler。
    Intent intent;
    intent.op = QStringLiteral("hello.say");
    auto result = application.host().execute(intent);
    CHECK(static_cast<bool>(result));
    CHECK(activation.triggeredCount(QStringLiteral("hello.say")) == 1);

    // handler 不认领未贡献的命令（不抢别人的 op）。
    Intent unknown;
    unknown.op = QStringLiteral("no.such.command");
    auto other = application.host().execute(unknown);
    CHECK(!static_cast<bool>(other));

    // 卸载 owner：命令从命令表整批撤，不留悬空。
    application.context().toolbox().unmountContribution(QStringLiteral("plotscope.helloworld"));
    activation.unbind(QStringLiteral("plotscope.helloworld"), &commands);
    CHECK(!commands.contains(QStringLiteral("hello.say")));
    CHECK(activation.boundCommandCount() == 0);
    // 视图声明也整批撤回（侧栏容器/视图/标题动作/colors/status 全消失）。
    CHECK(ext.contributedViewContainers().isEmpty());
    CHECK(ext.contributedViews().isEmpty());
    CHECK(ext.menuItemsIn(QStringLiteral("view/title")).isEmpty());
    CHECK(ext.menuItemsIn(QStringLiteral("view/item/context")).isEmpty());
    CHECK(ext.contributedColors().isEmpty());
    CHECK(ext.contributedStatuses().isEmpty());
    if (viewsReg)
    {
        CHECK(viewsReg->provider(QStringLiteral("hello.view")) == nullptr);
    }
    // Step 5：unbind 经同一 ViewRegistry 按 owner 整批撤，服务读回同步为空。
    if (viewsReg)
        CHECK(viewsReg->viewsIn(QStringLiteral("hello.container")).isEmpty());
    if (viewSvc)
    {
        CHECK(viewSvc->viewsInContainer(QStringLiteral("hello.container")).isEmpty());
        CHECK(viewSvc->containersIn(QStringLiteral("sidebar")).isEmpty());
        CHECK(viewSvc->viewForId(QStringLiteral("hello.view")).owner.isEmpty());
    }
    // host 再 execute 该 op 无 handler 认领。
    auto afterUnmount = application.host().execute(intent);
    CHECK(!static_cast<bool>(afterUnmount));

    if (g_failed == 0)
        std::printf("cmd-ext e2e: ALL PASS\n");
    else
        std::printf("cmd-ext e2e: %d FAILED\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
