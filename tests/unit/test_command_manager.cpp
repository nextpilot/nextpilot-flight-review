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

    ps::commands::CommandSpec flag;
    flag.id = QStringLiteral("demo.flag");
    flag.checkable = true;
    flag.checked = [](const ps::commands::CommandContext &) { return true; };
    registry.registerCommand(flag, [](const ps::commands::CommandContext &) {});
    if (!registry.isChecked(QStringLiteral("demo.flag"), {}))
        return 1;

    ps::commands::CommandSpec disabled;
    disabled.id = QStringLiteral("demo.disabled");
    disabled.enabled = [](const ps::commands::CommandContext &) { return false; };
    registry.registerCommand(disabled, [](const ps::commands::CommandContext &) {});
    if (registry.isEnabled(QStringLiteral("demo.disabled"), {}))
        return 1;
    if (registry.invoke(QStringLiteral("demo.disabled"), {}))
        return 1;

    std::printf("command registry ok\n");
    return 0;
}
