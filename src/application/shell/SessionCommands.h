#pragma once

#include <functional>

class SessionManager;

namespace ps::commands
{
class CommandManager;
}

namespace shell
{

void bindSessionCommandState(ps::commands::CommandManager &commands, SessionManager &session,
                             std::function<void()> refreshTitle);

} // namespace shell
