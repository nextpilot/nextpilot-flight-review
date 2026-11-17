#include "SessionCommands.h"

#include "CommandManager.h"
#include "SessionManager.h"

#include <utility>

namespace shell
{

void bindSessionCommandState(ps::commands::CommandManager &commands, SessionManager &session,
                             std::function<void()> refreshTitle)
{
    commands.setInvokeHook([&session, &commands, refreshTitle = std::move(refreshTitle)](
                               const QString &id) {
        if (id == QLatin1String("layout.save") || id == QLatin1String("layout.import"))
        {
            session.markClean();
            if (refreshTitle)
                refreshTitle();
            return;
        }
        const auto *spec = commands.spec(id);
        if (spec && spec->sessionDirty)
        {
            session.markDirty();
            if (refreshTitle)
                refreshTitle();
        }
    });
}

} // namespace shell
