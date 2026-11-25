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
        "menus":{"commandPalette":[{"command":"hello.say","group":"z@1"}]}})";
    auto obj = QJsonDocument::fromJson(QByteArray(manifest)).object();
    ContributesDoc doc = parseContributes(obj, QStringLiteral("plotscope.helloworld")).doc;
    CHECK(application.context().toolbox().mountContribution(doc));

    // 装配层同款接线：激活服务把贡献命令灌进一个命令表 + 注册执行 handler。
    CommandRegistry commands;
    assembly::ExtensionActivationService activation(application.context().toolbox());
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
    // host 再 execute 该 op 无 handler 认领。
    auto afterUnmount = application.host().execute(intent);
    CHECK(!static_cast<bool>(afterUnmount));

    if (g_failed == 0)
        std::printf("cmd-ext e2e: ALL PASS\n");
    else
        std::printf("cmd-ext e2e: %d FAILED\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
