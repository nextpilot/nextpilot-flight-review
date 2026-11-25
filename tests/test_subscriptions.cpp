// A8b-M2 契约面单测：ISubscriptions/SubscriptionBag（补建 M1 e0 欠账接口）。
// 只链 ps_workbench 依赖链 + Qt6::Core，验证订阅登记/计数/整批退订。

#include "ISubscriptions.h"

#include <QCoreApplication>
#include <QObject>

#include <cstdio>

using namespace nextpilot::plotscope::workbench;

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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    SubscriptionBag bag;
    CHECK(bag.count() == 0);

    QObject sender;
    int fired = 0;
    // 三次订阅各自持有独立句柄；空句柄忽略。
    bag.add(QObject::connect(&sender, &QObject::destroyed, [&]() { ++fired; }));
    bag.add(QObject::connect(&sender, &QObject::destroyed, [&]() { ++fired; }));
    CHECK(bag.count() == 2);

    // disposeAll 后句柄失效：销毁 sender 不再触发回调。
    bag.disposeAll();
    CHECK(bag.count() == 0);
    QObject *victim = new QObject;
    QObject *watched = victim;
    bag.add(QObject::connect(victim, &QObject::destroyed, watched, [&]() { ++fired; }));
    CHECK(bag.count() == 1);
    bag.disposeAll();
    delete victim;
    CHECK(fired == 0); // 已退订

    if (g_failed == 0)
        std::printf("subscriptions: ALL PASS\n");
    else
        std::printf("subscriptions: %d FAILED\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
