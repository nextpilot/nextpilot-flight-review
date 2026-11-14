#include "app/commands/CommandRegistry.h"

namespace ps::commands
{

void CommandRegistry::add(const QString &id, Handler handler)
{
    if (!id.isEmpty())
        m_handlers[id] = std::move(handler);
}

bool CommandRegistry::contains(const QString &id) const
{
    return m_handlers.find(id) != m_handlers.end();
}

bool CommandRegistry::invoke(const QString &id, const CommandContext &ctx) const
{
    const auto it = m_handlers.find(id);
    if (it == m_handlers.end() || !it->second)
        return false;
    it->second(ctx);
    return true;
}

} // namespace ps::commands
