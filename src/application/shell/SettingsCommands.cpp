#include "SettingsCommands.h"

#include "CommandManager.h"
#include "ExtensionManager.h"
#include "SettingsDialog.h"

#include <utility>

namespace shell
{

void registerSettingsCommand(ps::commands::CommandManager &commands, QWidget *parent,
                             ExtensionManager &extensions, std::function<void()> applyTheme)
{
    commands.registerCommand(
        ps::commands::CommandSpec{QStringLiteral("file.settings"), QStringLiteral("设置"),
                                  QStringLiteral("settings"), QStringLiteral("Ctrl+,"),
                                  false, {}, {}, false},
        [parent, &extensions, applyTheme = std::move(applyTheme)](
            const ps::commands::CommandContext &) {
            SettingsDialog dialog(parent, extensions, applyTheme);
            dialog.exec();
        });
}

} // namespace shell
