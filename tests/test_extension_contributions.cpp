// A8b-M1 contributes 契约单测：mount/unmount/owner 索引/find + 整批撤回。
// 贡献面是唯一扩展注册表 ToolboxHub 的 contributes 属性（不是第二个宿主）——
// 只链 ps_registry + Qt6::Core，验证 contributes 是纯数据表，不碰壳/命令层。

#include "ToolboxHub.h"
#include "ContributionParser.h"
#include "RegistryTree.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cstdio>

using namespace nextpilot::plotscope::registry;

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

ContributesDoc doc(const char *json, const QString &owner)
{
    auto obj = QJsonDocument::fromJson(QByteArray(json)).object();
    return parseContributes(obj, owner).doc; // 测试输入均合法，忽略 parse issue
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    RegistryTree tree;
    ToolboxHub extensions(tree);

    // 唯一扩展节点上树（kind=Extension）；contributes 是该节点的属性面，不新增 kind。
    bool onTree = false;
    for (RegistryNode *n : tree.nodes())
        if (n == &extensions)
            onTree = true;
    CHECK(onTree);

    // mount 两个 owner
    CHECK(extensions.mountContribution(doc(R"({"commands":[{"command":"plot.new","title":"新建"}]})",
                                           QStringLiteral("plot"))));
    CHECK(extensions.mountContribution(doc(R"({"commands":[{"command":"data.open","title":"打开"}],"menus":{"commandPalette":[{"command":"data.open"}]}})",
                                           QStringLiteral("data"))));

    CHECK(extensions.isContributionMounted(QStringLiteral("plot")));
    CHECK(extensions.isContributionMounted(QStringLiteral("data")));
    CHECK(extensions.contributionOwners().size() == 2);
    CHECK(extensions.contributedCommands().size() == 2);

    // RegistryNode 可观测口径：只有 contributes、未 mount 任何 Pack 时，
    // count/keysAsStrings 也必须看见这些 owner（诊断 extension 行不能是 0）。
    // 本测试实例未装内置包，故 count 恰等于 contributes owner 数。
    CHECK(extensions.count() == 2);
    CHECK(extensions.keysAsStrings().contains(QStringLiteral("plot")));
    CHECK(extensions.keysAsStrings().contains(QStringLiteral("data")));

    // M2：一份带视图/菜单/颜色/状态的 contributes 文档，mount 后各表可读、按 owner 归属。
    CHECK(extensions.mountContribution(doc(R"({
        "commands":[{"command":"view.open","title":"打开视图"}],
        "viewsContainers":[{"id":"plot.side","title":"绘图","area":"sidebar","order":30}],
        "views":[{"id":"plot.props","name":"属性","container":"plot.side"}],
        "menus":{"view/title":[{"command":"view.open","when":"view == plot.props"}]},
        "colors":{"plot.cursor":{"dark":"#fff","light":"#000"}},
        "statusbar":[{"id":"plot.cursor.time","side":"right","priority":20}]
    })", QStringLiteral("views.owner"))));
    CHECK(extensions.contributedViewContainers().size() == 1);
    CHECK(extensions.contributedViewContainers().at(0).owner == QLatin1String("views.owner"));
    CHECK(extensions.contributedViews().size() == 1);
    CHECK(extensions.contributedViews().at(0).item.container == QLatin1String("plot.side"));
    CHECK(extensions.menuItemsIn(QStringLiteral("view/title")).size() == 1);
    CHECK(extensions.menuItemsIn(QStringLiteral("view/title")).at(0).item.command == QLatin1String("view.open"));
    CHECK(extensions.contributedColors().size() == 1);
    CHECK(extensions.contributedStatuses().size() == 1);
    // 卸载整批撤：M2 五张表全空，不留悬空。
    CHECK(extensions.unmountContribution(QStringLiteral("views.owner")));
    CHECK(extensions.contributedViewContainers().isEmpty());
    CHECK(extensions.contributedViews().isEmpty());
    CHECK(extensions.contributedMenuItems().isEmpty());
    CHECK(extensions.contributedColors().isEmpty());
    CHECK(extensions.contributedStatuses().isEmpty());

    // find / listAll
    const auto *c = extensions.findCommand(QStringLiteral("plot.new"));
    CHECK(c != nullptr);
    CHECK(c->title == QStringLiteral("新建"));
    CHECK(extensions.findCommand(QStringLiteral("missing")) == nullptr);
    CHECK(extensions.commandIdsOf(QStringLiteral("plot")).size() == 1);
    CHECK(extensions.paletteEntries().size() == 1);
    CHECK(extensions.paletteEntries().at(0).owner == QLatin1String("data"));

    // 重复 mount(owner) → 拒 + 诊断
    CHECK(!extensions.mountContribution(doc(R"({"commands":[{"command":"plot.again"}]})", QStringLiteral("plot"))));
    CHECK(!extensions.contributionDiagnostics().isEmpty());
    CHECK(extensions.contributedCommands().size() == 2); // 未增

    // 跨 owner 重复 id → 先到者保留 + 诊断
    CHECK(extensions.mountContribution(doc(R"({"commands":[{"command":"data.open","title":"冒名"}]})",
                                           QStringLiteral("evil"))));
    const auto *kept = extensions.findCommand(QStringLiteral("data.open"));
    CHECK(kept != nullptr);
    CHECK(kept->title == QStringLiteral("打开")); // data 的先到者保留，evil 被跳过

    // unmount(owner) 整批撤：命令 + 归属 + palette 一并消失，无悬空
    CHECK(extensions.unmountContribution(QStringLiteral("data")));
    CHECK(!extensions.isContributionMounted(QStringLiteral("data")));
    CHECK(extensions.findCommand(QStringLiteral("data.open")) == nullptr);
    CHECK(extensions.commandIdsOf(QStringLiteral("data")).isEmpty());
    CHECK(extensions.paletteEntries().isEmpty()); // data 的 palette 项被撤
    // plot 与 evil 不受影响
    CHECK(extensions.findCommand(QStringLiteral("plot.new")) != nullptr);

    CHECK(extensions.unmountContribution(QStringLiteral("missing")) == false); // 未挂载 → false

    // 全撤后空表
    extensions.unmountContribution(QStringLiteral("plot"));
    extensions.unmountContribution(QStringLiteral("evil"));
    CHECK(extensions.contributedCommands().isEmpty());
    CHECK(extensions.contributionOwners().isEmpty());
    CHECK(extensions.count() == 0); // 全撤后诊断恢复 0，不留幽灵扩展

    if (g_failed == 0)
        std::printf("extension_contributions: ALL PASS\n");
    else
        std::printf("extension_contributions: %d FAILED\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
