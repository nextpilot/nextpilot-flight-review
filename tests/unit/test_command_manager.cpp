#include "CommandManager.h"

#include <QCoreApplication>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    ps::commands::CommandManager registry;

    QString seen;
    registry.add(QStringLiteral("demo.run"), [&seen](const ps::commands::CommandContext &ctx) {
        seen = ctx.arg;
    });

    if (!registry.contains(QStringLiteral("demo.run")))
        return 1;
    if (!registry.invoke(QStringLiteral("demo.run"), {nullptr, QStringLiteral("payload")}))
        return 1;
    if (seen != QLatin1String("payload"))
        return 1;
    if (registry.invoke(QStringLiteral("demo.missing"), {}))
        return 1;

    std::printf("command registry ok\n");
    return 0;
}
