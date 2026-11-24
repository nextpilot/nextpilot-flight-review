#include "command/CommandRegistry.h"

#include <QDebug>

namespace nextpilot::plotscope::command
{

bool CommandRegistry::add(const CommandDescriptor &command)
{
    // D7：命令必须携带可执行载荷（本地动作 id 或意图 op 至少其一），
    // 但载荷是数据不是代码——handler 已彻底移除。
    if (command.id.isEmpty() || command.title.isEmpty() ||
        (command.localActionId.isEmpty() && command.intentOp.isEmpty()))
        return false;
    for (const CommandDescriptor &existing : m_commands)
        if (existing.id == command.id)
            return false;

    m_commands.append(command);

    // 装载期编译一次：同一个 when 串在多条命令间共享同一个编译结果。
    QString parseError;
    const WhenExpression compiled = compileCached(command.when, &parseError);
    m_whenById.insert(command.id, compiled);
    return true;
}

WhenExpression CommandRegistry::compileCached(const QString &source, QString *error)
{
    const auto it = m_whenCache.constFind(source);
    if (it != m_whenCache.constEnd())
        return it.value();

    const WhenExpression compiled = WhenExpression::compile(source, error);
    m_whenCache.insert(source, compiled);
    ++m_parseCount;
    return compiled;
}

bool CommandRegistry::contains(const QString &id) const
{
    return find(id) != nullptr;
}

const CommandDescriptor *CommandRegistry::find(const QString &id) const
{
    for (const CommandDescriptor &command : m_commands)
        if (command.id == id)
            return &command;
    return nullptr;
}

void CommandRegistry::setContextValue(const QString &key, const QVariant &value)
{
    checkKey(key);
    m_context.set(ContextScope::Shell, key, value);
}

void CommandRegistry::setContextValue(ContextScope scope, const QString &key, const QVariant &value)
{
    checkKey(key);
    m_context.set(scope, key, value);
}

void CommandRegistry::checkKey(const QString &key)
{
    // N3：键必须登记在 WhenKeys 唯一表。未知键警告 + 计数（不硬拒绝，兼容
    // 运行期扩展与测试），生产代码写入未知键时测试据 unknownKeyCount 断言为 0。
    if (WhenKeys::isKnown(key))
        return;
    ++m_unknownKeys;
    m_lastUnknownKey = key;
    const QString hint = WhenKeys::suggest(key);
    qWarning("[when] 未登记的上下文键 '%s'%s", qPrintable(key),
             hint.isEmpty() ? "" : qPrintable(QStringLiteral("（应为 '%1'）").arg(hint)));
}

QVariant CommandRegistry::contextValue(const QString &key) const
{
    return m_context.value(key);
}

void CommandRegistry::clearContext()
{
    m_context.clearAll();
}

const WhenExpression &CommandRegistry::whenFor(const QString &id) const
{
    static const WhenExpression alwaysTrue;
    const auto it = m_whenById.constFind(id);
    return it == m_whenById.constEnd() ? alwaysTrue : it.value();
}

bool CommandRegistry::isAvailable(const QString &id, QString *reason) const
{
    const CommandDescriptor *command = find(id);
    if (!command)
    {
        if (reason)
            *reason = QStringLiteral("命令不存在: %1").arg(id);
        return false;
    }

    const WhenExpression &when = whenFor(id);
    if (!when.evaluate(m_context))
    {
        if (reason)
            *reason = when.isValid()
                          ? QStringLiteral("命令当前不可用: %1").arg(command->when)
                          : QStringLiteral("when 表达式无效: %1").arg(command->when);
        return false;
    }

    if (command->disabledReason)
    {
        const QString dynamicReason = command->disabledReason();
        if (!dynamicReason.isEmpty())
        {
            if (reason)
                *reason = dynamicReason;
            return false;
        }
    }
    return true;
}

bool CommandRegistry::execute(const QString &id, QString *error)
{
    if (!isAvailable(id, error))
        return false;

    if (!m_executor)
    {
        if (error)
            *error = QStringLiteral("命令执行器未注入: %1").arg(id);
        return false;
    }

    const CommandDescriptor *command = find(id);
    return m_executor(*command, error);
}

bool CommandRegistry::evaluateWhen(const QString &expression, const QHash<QString, QVariant> &context,
                                   bool *ok)
{
    ContextStack stack;
    for (auto it = context.constBegin(); it != context.constEnd(); ++it)
        stack.set(ContextScope::Shell, it.key(), it.value());

    QString parseError;
    const WhenExpression compiled = WhenExpression::compile(expression, &parseError);
    const bool result = compiled.evaluate(stack);
    if (ok)
        *ok = parseError.isEmpty();
    return result;
}

} // namespace nextpilot::plotscope::command
