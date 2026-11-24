// 状态栏登记处（libs/context::StatusHub）契约测试：headless、无 QWidget。
//
// 这一段只验"登记 + 文案 + 排序 + 瞬时消息"四件事。**节流不在此验**——
// 按 D2，某个键要不要限频是生产方（模块）的领域知识，hub 不认识业务键。

#include "StatusHub.h"

#include <QCoreApplication>
#include <QSignalSpy>

#include <cstdio>
#include <cstdlib>

using nextpilot::plotscope::context::StatusHub;
using namespace nextpilot::plotscope::registry;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond);                 \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

#define OK(msg) std::fprintf(stdout, "[ok] %s\n", msg)

static StatusSpec makeSpec(const QString &id, StatusSide side, int priority)
{
    StatusSpec s;
    s.id = id;
    s.side = side;
    s.priority = priority;
    return s;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    StatusHub hub;

    // 1) 登记：先声明者优先；缺省 provider 补 "module"；未声明 id 不被 updateStatus 接受。
    {
        QSignalSpy declared(&hub, &StatusHub::statusSpecDeclared);
        StatusSpec a = makeSpec(QStringLiteral("data.source.count"), StatusSide::Left, 10);
        a.text = QStringLiteral("源: 0");
        hub.registerSpec(a);
        CHECK(declared.count() == 1);

        StatusSpec dup = makeSpec(QStringLiteral("data.source.count"), StatusSide::Right, 99);
        hub.registerSpec(dup); // 同 id 第二次声明：忽略
        CHECK(declared.count() == 1);
        CHECK(hub.spec(QStringLiteral("data.source.count"))->side == StatusSide::Left);
        CHECK(hub.spec(QStringLiteral("data.source.count"))->provider == QStringLiteral("module"));

        hub.updateStatus(QStringLiteral("no.such.id"), QStringLiteral("x"));
        CHECK(hub.text(QStringLiteral("no.such.id")).isEmpty());
    }
    OK("登记：同 id 先声明者优先、缺省 provider、未声明 id 的推送被忽略");

    // 2) 排布：同侧按 priority 升序、同权重按 id 字典序；specsOf 只取该侧。
    {
        hub.registerSpec(makeSpec(QStringLiteral("playback.state"), StatusSide::Right, 30));
        hub.registerSpec(makeSpec(QStringLiteral("plot.axes.cursor"), StatusSide::Right, 20));
        hub.registerSpec(makeSpec(QStringLiteral("b.right.alpha"), StatusSide::Right, 30));

        const auto all = hub.specs();
        CHECK(all.size() == 4);
        const auto right = hub.specsOf(StatusSide::Right);
        CHECK(right.size() == 3);
        CHECK(right.at(0).id == QStringLiteral("plot.axes.cursor")); // priority 20
        CHECK(right.at(1).id == QStringLiteral("b.right.alpha"));    // priority 30，id 字典序在前
        CHECK(right.at(2).id == QStringLiteral("playback.state"));
        const auto left = hub.specsOf(StatusSide::Left);
        CHECK(left.size() == 1);
        CHECK(left.first().id == QStringLiteral("data.source.count"));
    }
    OK("排布：同侧 priority 升序 + id 稳定排序，specsOf 分侧");

    // 3) 常驻文案：变化才发信号；同值重复推送不刷新；初值来自 spec.text。
    {
        const QString id = QStringLiteral("playback.state");
        CHECK(hub.text(id).isEmpty()); // 本用例未给初值
        QSignalSpy changed(&hub, &StatusHub::statusChanged);
        hub.updateStatus(id, QStringLiteral("回放: 1.0x"));
        CHECK(changed.count() == 1);
        CHECK(hub.text(id) == QStringLiteral("回放: 1.0x"));
        hub.updateStatus(id, QStringLiteral("回放: 1.0x"));
        CHECK(changed.count() == 1); // 未变不发
        hub.updateStatus(id, QStringLiteral("回放: 2.0x"));
        CHECK(changed.count() == 2);
        CHECK(hub.text(id) == QStringLiteral("回放: 2.0x"));
        CHECK(hub.text(QStringLiteral("data.source.count")) == QStringLiteral("源: 0")); // 初值保留
    }
    OK("常驻文案：推模式只在变化时发信号，spec.text 作为初值");

    // 4) 瞬时消息：非空才发；空串与非法时长直接丢弃（避免壳侧空闪）。
    {
        QSignalSpy flashed(&hub, &StatusHub::statusFlashed);
        hub.flash(QStringLiteral("已载入数据集 #3"));
        CHECK(flashed.count() == 1);
        hub.flash(QString());
        CHECK(flashed.count() == 1);
        hub.flash(QStringLiteral("x"), 0);
        CHECK(flashed.count() == 1);
        hub.clear();
        CHECK(hub.specs().isEmpty());
    }
    OK("瞬时消息：非空才发、空串/非法时长丢弃、clear 复位");

    std::fprintf(stdout, "ALL PASS\n");
    return 0;
}
