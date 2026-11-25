// A8b ContributionParser 单测：
//   M1 —— commands + menus.commandPalette + 三坏输入；
//   M2 —— viewsContainers / views / menus 其余 place / colors / statusbar + §3.0.14 畸形用例。
// 只链 ps_registry + Qt6::Core，不碰壳、不碰命令层。

#include "ContributionParser.h"

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

QJsonObject fromJson(const char *s)
{
    return QJsonDocument::fromJson(QByteArray(s)).object();
}

bool hasIssueContaining(const ParseResult &r, const QString &needle)
{
    for (const auto &i : r.issues)
        if (i.message.contains(needle))
            return true;
    return false;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ===== M1 回归：commands + menus.commandPalette =====
    {
        ParseResult r = parseContributes(fromJson(R"({
            "commands": [
                {"command":"hello.say","title":"说你好","category":"测试","icon":"chat","order":10},
                {"command":"hello.bye","title":"再见"}
            ],
            "menus": {
                "commandPalette": [
                    {"command":"hello.say","group":"z@1","when":"editor == canvas"},
                    {"command":"hello.bye"}
                ]
            }
        })"), QStringLiteral("plotscope.hello"));
        CHECK(r.ok);
        CHECK(r.issues.isEmpty());
        CHECK(r.doc.commands.size() == 2);
        CHECK(r.doc.commandPalette.size() == 2);
        CHECK(r.doc.commandPalette.at(0).group == QLatin1String("z@1"));
    }

    // ===== M2-A：viewsContainers / views 正常解析 =====
    {
        ParseResult r = parseContributes(fromJson(R"({
            "viewsContainers": [
                {"id":"plot.side","title":"绘图","area":"sidebar","icon":"plot","order":30},
                {"id":"plot.bottom","title":"日志","area":"bottom"}
            ],
            "views": [
                {"id":"plot.props","name":"属性","container":"plot.side","when":"editor == canvas","order":5}
            ]
        })"), QStringLiteral("plot"));
        CHECK(r.ok);
        CHECK(r.doc.viewContainers.size() == 2);
        CHECK(r.doc.viewContainers.at(0).id == QLatin1String("plot.side"));
        CHECK(r.doc.viewContainers.at(0).area == QLatin1String("sidebar"));
        CHECK(r.doc.viewContainers.at(0).order == 30);
        CHECK(r.doc.views.size() == 1);
        CHECK(r.doc.views.at(0).name == QStringLiteral("属性"));
        CHECK(r.doc.views.at(0).container == QLatin1String("plot.side"));
    }

    // ===== M2-B：menus 其余 place（view/title / view/item/context / menubar / editor/context） =====
    {
        ParseResult r = parseContributes(fromJson(R"({
            "menus": {
                "view/title": [{"command":"plot.reset","when":"view == plot.props","group":"navigation@1"}],
                "view/item/context": [{"command":"plot.del","when":"item.kind == curve"}],
                "menubar/plot": [{"command":"plot.new","label":"新建","icon":"plus","order":10}],
                "editor/context": [{"command":"plot.copy"}]
            }
        })"), QStringLiteral("plot"));
        CHECK(r.ok);
        CHECK(r.doc.menuItems.size() == 4);
        // QJsonObject 按 key 字母序遍历，不依赖插入序——按 place 查找断言。
        auto findPlace = [&r](const QString &p) -> const ContributedMenuItem * {
            for (const auto &it : r.doc.menuItems)
                if (it.place == p)
                    return &it;
            return nullptr;
        };
        const auto *vt = findPlace(QStringLiteral("view/title"));
        CHECK(vt != nullptr && vt->when == QLatin1String("view == plot.props"));
        CHECK(findPlace(QStringLiteral("view/item/context")) != nullptr);
        const auto *mb = findPlace(QStringLiteral("menubar/plot"));
        CHECK(mb != nullptr && mb->label == QStringLiteral("新建"));
        CHECK(findPlace(QStringLiteral("editor/context")) != nullptr);
    }

    // ===== M2-C：colors / statusbar 正常解析 =====
    {
        ParseResult r = parseContributes(fromJson(R"({
            "colors": {"plot.cursor":{"dark":"#f2b134","light":"#1A73E8"}},
            "statusbar": [{"id":"plot.cursor.time","side":"right","priority":20,"text":"游标: --"}]
        })"), QStringLiteral("plot"));
        CHECK(r.ok);
        CHECK(r.doc.colors.size() == 1);
        CHECK(r.doc.colors.at(0).dark == QLatin1String("#f2b134"));
        CHECK(r.doc.statuses.size() == 1);
        CHECK(r.doc.statuses.at(0).side == QLatin1String("right"));
        CHECK(r.doc.statuses.at(0).priority == 20);
    }

    // ===== 坏输入 1：重复 command id → 后到者跳过 + issue，不覆盖 =====
    {
        ParseResult r = parseContributes(fromJson(R"({
            "commands": [{"command":"dup","title":"第一"},{"command":"dup","title":"第二"}]
        })"), QStringLiteral("dup.owner"));
        CHECK(!r.ok);
        CHECK(hasIssueContaining(r, QStringLiteral("重复")));
        CHECK(r.doc.commands.size() == 1);
        CHECK(r.doc.commands.at(0).title == QStringLiteral("第一"));
    }

    // ===== 坏输入 2a：commands 非数组 → 整条拒收 =====
    {
        ParseResult r = parseContributes(fromJson(R"({"commands": {"command":"oops"}})"),
                                          QStringLiteral("bad.owner"));
        CHECK(!r.ok);
        CHECK(hasIssueContaining(r, QStringLiteral("必须是数组")));
        CHECK(r.doc.commands.isEmpty());
    }

    // ===== 坏输入 2b：contributes 为空 → 整条拒收 =====
    {
        ParseResult r = parseContributes(QJsonObject(), QStringLiteral("empty.owner"));
        CHECK(!r.ok);
        CHECK(hasIssueContaining(r, QStringLiteral("整条拒收")));
        CHECK(r.doc.isEmpty());
    }

    // ===== 坏输入 3：未知键 / 未知字段 → 跳过 + 诊断，其余照常 =====
    // M2 起 editor/context 已是合法 place，改用 bogusPlace 触发"未支持"诊断。
    {
        ParseResult r = parseContributes(fromJson(R"({
            "bogusTop": 1,
            "commands": [{"command":"ok.cmd","title":"正常","unknownField":"x"}],
            "menus": {
                "bogusPlace": [{"command":"later"}],
                "commandPalette": [{"command":"ok.cmd","bogus":9}]
            }
        })"), QStringLiteral("mixed.owner"));
        CHECK(!r.ok);
        CHECK(hasIssueContaining(r, QStringLiteral("未知 contributes 键")));
        CHECK(hasIssueContaining(r, QStringLiteral("命令条目未知字段")));
        CHECK(hasIssueContaining(r, QStringLiteral("未支持的菜单 place")));
        CHECK(hasIssueContaining(r, QStringLiteral("菜单条目未知字段")));
        CHECK(r.doc.commands.size() == 1);
        CHECK(r.doc.commandPalette.size() == 1);
    }

    // ===== 缺 id → 跳过该条 + issue =====
    {
        ParseResult r = parseContributes(fromJson(R"({"commands":[{"title":"无 id"}]})"),
                                          QStringLiteral("noid.owner"));
        CHECK(!r.ok);
        CHECK(hasIssueContaining(r, QStringLiteral("缺 command")));
        CHECK(r.doc.commands.isEmpty());
    }

    // ===== §3.0.14 畸形用例 1：place 非法 =====
    {
        ParseResult r = parseContributes(fromJson(R"({"menus":{"mystery":[{"command":"x"}]}})"),
                                          QStringLiteral("bad.place"));
        CHECK(!r.ok);
        CHECK(hasIssueContaining(r, QStringLiteral("未支持的菜单 place")));
        CHECK(r.doc.menuItems.isEmpty());
    }

    // ===== §3.0.14 畸形用例 2：views 非数组 / 空 id =====
    {
        ParseResult r = parseContributes(fromJson(R"({"views":[{"name":"无 id"}]})"),
                                          QStringLiteral("bad.view"));
        CHECK(!r.ok);
        CHECK(hasIssueContaining(r, QStringLiteral("缺 id")));
        CHECK(r.doc.views.isEmpty());
    }

    // ===== §3.0.14 畸形用例 3：跨类型重复 id（容器 id == view id）→ 后到者跳过 =====
    {
        ParseResult r = parseContributes(fromJson(R"({
            "viewsContainers":[{"id":"dup.id","title":"容器"}],
            "views":[{"id":"dup.id","name":"视图"}]
        })"), QStringLiteral("cross.dup"));
        CHECK(!r.ok);
        CHECK(hasIssueContaining(r, QStringLiteral("重复 id")));
        // 全局 id 唯一：二者合计恰好保留 1 条（哪个先到取决于 QJsonObject key 序）。
        CHECK(r.doc.views.size() + r.doc.viewContainers.size() == 1);
    }

    if (g_failed == 0)
        std::printf("contribution_parser: ALL PASS\n");
    else
        std::printf("contribution_parser: %d FAILED\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
