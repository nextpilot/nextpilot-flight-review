#pragma once

#include <functional>

class QWidget;
class ExtensionManager;

namespace ps::commands
{
class CommandManager;
}

namespace shell
{

void registerSettingsCommand(ps::commands::CommandManager &commands, QWidget *parent,
                             ExtensionManager &extensions, std::function<void()> applyTheme);

} // namespace shell
