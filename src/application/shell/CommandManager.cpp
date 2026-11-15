#include "CommandManager.h"

#include <QKeySequence>
#include <QShortcut>
#include <QWidget>

namespace ps::commands
{

void CommandManager::add(const QString &id, Handler handler)
{
    if (!id.isEmpty())
        m_handlers[id] = std::move(handler);
}

void CommandManager::describe(const CommandSpec &spec)
{
    if (spec.id.isEmpty())
        return;
    m_specs[spec.id] = spec;
}

void CommandManager::registerCommand(const CommandSpec &spec, Handler handler)
{
    describe(spec);
    add(spec.id, std::move(handler));
}

void CommandManager::bindShortcuts(QWidget *owner)
{
    if (!owner)
        return;
    for (const auto &entry : m_specs)
    {
        const CommandSpec &spec = entry.second;
        if (spec.shortcut.isEmpty())
            continue;
        auto *shortcut = new QShortcut(QKeySequence(spec.shortcut), owner);
        QObject::connect(shortcut, &QShortcut::activated, owner, [this, id = spec.id, owner] {
            CommandContext ctx;
            ctx.parent = owner;
            invoke(id, ctx);
        });
    }
}

bool CommandManager::contains(const QString &id) const
{
    return m_handlers.find(id) != m_handlers.end();
}

bool CommandManager::invoke(const QString &id, const CommandContext &ctx) const
{
    const auto it = m_handlers.find(id);
    if (it == m_handlers.end() || !it->second || !isEnabled(id, ctx))
        return false;
    it->second(ctx);
    if (m_invokeHook)
        m_invokeHook(id);
    return true;
}

bool CommandManager::isEnabled(const QString &id, const CommandContext &ctx) const
{
    const auto it = m_specs.find(id);
    if (it == m_specs.end() || !it->second.enabled)
        return true;
    return it->second.enabled(ctx);
}

bool CommandManager::isChecked(const QString &id, const CommandContext &ctx) const
{
    const auto it = m_specs.find(id);
    if (it == m_specs.end() || !it->second.checkable || !it->second.checked)
        return false;
    return it->second.checked(ctx);
}

const CommandSpec *CommandManager::spec(const QString &id) const
{
    const auto it = m_specs.find(id);
    return it == m_specs.end() ? nullptr : &it->second;
}

} // namespace ps::commands
