#include "SettingsManager.h"
#include "CommandManager.h"
#include "SettingsDialog.h"

void SettingsManager::registerCommand(ps::commands::CommandManager &commands, MainWindow *parent)
{
    commands.registerCommand(
        ps::commands::CommandSpec{QStringLiteral("file.settings"), QStringLiteral("设置"),
                                  QStringLiteral("settings"), QStringLiteral("Ctrl+,"),
                                  false, {}, {}, false},
        [this, parent](const ps::commands::CommandContext &) { openDialog(parent); });
}

void SettingsManager::openDialog(MainWindow *parent)
{
    SettingsDialog dialog(parent);
    dialog.exec();
}
