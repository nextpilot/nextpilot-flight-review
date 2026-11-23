#include "command/ContextStack.h"

namespace nextpilot::plotscope::command
{

void ContextStack::set(ContextScope scope, const QString &key, const QVariant &value)
{
    if (key.isEmpty())
        return;
    QHash<QString, QVariant> &layer = m_layers[int(scope)];
    if (!value.isValid() || value.isNull())
        layer.remove(key);
    else
        layer.insert(key, value);
}

void ContextStack::clear(ContextScope scope)
{
    m_layers[int(scope)].clear();
}

void ContextStack::clearAll()
{
    for (auto &layer : m_layers)
        layer.clear();
}

QVariant ContextStack::value(const QString &key) const
{
    for (int i = 2; i >= 0; --i)
    {
        const auto it = m_layers[i].constFind(key);
        if (it != m_layers[i].constEnd())
            return it.value();
    }
    return {};
}

bool ContextStack::contains(const QString &key) const
{
    for (const auto &layer : m_layers)
        if (layer.contains(key))
            return true;
    return false;
}

QHash<QString, QVariant> ContextStack::merged() const
{
    QHash<QString, QVariant> out;
    for (const auto &layer : m_layers)   // Shell → Panel → View，内层覆盖外层
        for (auto it = layer.constBegin(); it != layer.constEnd(); ++it)
            out.insert(it.key(), it.value());
    return out;
}

} // namespace nextpilot::plotscope::command
