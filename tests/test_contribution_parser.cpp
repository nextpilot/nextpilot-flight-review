// A8b-M1 ContributionParser 单测：正常解析 + 三坏输入（重复 id / 畸形 JSON / 未知键字段）。
// 只链 ps_registry + Qt6::Core，不碰壳、不碰命令层。

#include "ContributionParser.h"

#include <QCoreApplication>
#include <QJsonArray>
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

    // --- 正常解析：commands + menus.commandPalette ---
    {
        auto obj = fromJson(R"({
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
        })");
        ParseResult r = parseContributes(obj, QStringLiteral("plotscope.hello"));
        CHECK(r.ok);
        CHECK(r.issues.isEmpty());
        CHECK(r.doc.owner == QLatin1String("plotscope.hello"));
        CHECK(r.doc.commands.size() == 2);
        CHECK(r.doc.commands.at(0).command == QLatin1String("hello.say"));
        CHECK(r.doc.commands.at(0).title == QStringLiteral("说你好"));
        CHECK(r.doc.commands.at(0).order == 10);
        CHECK(r.doc.commandPalette.size() == 2);
        CHECK(r.doc.commandPalette.at(0).command == QLatin1String("hello.say"));
        CHECK(r.doc.commandPalette.at(0).group == QLatin1String("z@1"));
        CHECK(r.doc.commandPalette.at(0).when == QLatin1String("editor == canvas"));
    }

    // --- 坏输入 1：重复 command id → 后到者跳过 + issue，不覆盖 ---
    {
        auto obj = fromJson(R"({
            "commands": [
                {"command":"dup","title":"第一"},
                {"command":"dup","title":"第二"}
            ]
        })");
        ParseResult r = parseContributes(obj, QStringLiteral("dup.owner"));
        CHECK(!r.ok);
        CHECK(hasIssueContaining(r, QStringLiteral("重复 command id")));
        CHECK(r.doc.commands.size() == 1);
        CHECK(r.doc.commands.at(0).title == QStringLiteral("第一")); // 先到者保留
    }

    // --- 坏输入 2a：commands 非数组 → 整条拒收 + issue，不崩 ---
    {
        auto obj = fromJson(R"({"commands": {"command":"oops"}})");
        ParseResult r = parseContributes(obj, QStringLiteral("bad.owner"));
        CHECK(!r.ok);
        CHECK(hasIssueContaining(r, QStringLiteral("必须是数组")));
        CHECK(r.doc.commands.isEmpty());
    }

    // --- 坏输入 2b：contributes 为空 → 整条拒收 + issue，不崩 ---
    {
        ParseResult r = parseContributes(QJsonObject(), QStringLiteral("empty.owner"));
        CHECK(!r.ok);
        CHECK(hasIssueContaining(r, QStringLiteral("整条拒收")));
        CHECK(r.doc.isEmpty());
    }

    // --- 坏输入 3：未知键 / 未知字段 → 跳过 + 诊断，其余照常 ---
    {
        auto obj = fromJson(R"({
            "bogusTop": 1,
            "commands": [
                {"command":"ok.cmd","title":"正常","unknownField":"x"}
            ],
            "menus": {
                "editor/context": [{"command":"later"}],
                "commandPalette": [{"command":"ok.cmd","bogus":9}]
            }
        })");
        ParseResult r = parseContributes(obj, QStringLiteral("mixed.owner"));
        CHECK(!r.ok);
        CHECK(hasIssueContaining(r, QStringLiteral("未知 contributes 键")));
        CHECK(hasIssueContaining(r, QStringLiteral("命令条目未知字段")));
        CHECK(hasIssueContaining(r, QStringLiteral("未支持的菜单 place")));
        CHECK(hasIssueContaining(r, QStringLiteral("菜单条目未知字段")));
        // 其余照常：合法条目仍被解析
        CHECK(r.doc.commands.size() == 1);
        CHECK(r.doc.commands.at(0).command == QLatin1String("ok.cmd"));
        CHECK(r.doc.commandPalette.size() == 1);
        CHECK(r.doc.commandPalette.at(0).command == QLatin1String("ok.cmd"));
    }

    // --- 缺 command id → 跳过该条 + issue ---
    {
        auto obj = fromJson(R"({"commands":[{"title":"无 id"}]})");
        ParseResult r = parseContributes(obj, QStringLiteral("noid.owner"));
        CHECK(!r.ok);
        CHECK(hasIssueContaining(r, QStringLiteral("缺 command id")));
        CHECK(r.doc.commands.isEmpty());
    }

    if (g_failed == 0)
        std::printf("contribution_parser: ALL PASS\n");
    else
        std::printf("contribution_parser: %d FAILED\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
