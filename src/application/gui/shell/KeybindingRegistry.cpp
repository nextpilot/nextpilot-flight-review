#include "KeybindingRegistry.h"

#include <QKeySequence>

#include <algorithm>

namespace nextpilot::plotscope::gui
{

void KeybindingRegistry::setDefault(const QString &commandId, const QString &sequence)
{
    if (commandId.isEmpty())
        return;
    const QString normalized = normalize(sequence);
    if (normalized.isEmpty())
        m_defaults.remove(commandId);
    else
        m_defaults.insert(commandId, normalized);
}

bool KeybindingRegistry::setOverride(const QString &commandId, const QString &sequence)
{
    if (commandId.isEmpty())
        return false;
    if (sequence.trimmed().isEmpty())
    {
        m_overrides.remove(commandId);
        return true;
    }
    const QString normalized = normalize(sequence);
    if (normalized.isEmpty())
        return false;
    m_overrides.insert(commandId, normalized);
    return true;
}

bool KeybindingRegistry::clearOverride(const QString &commandId)
{
    return m_overrides.remove(commandId) > 0;
}

bool KeybindingRegistry::hasOverride(const QString &commandId) const
{
    return m_overrides.contains(commandId);
}

void KeybindingRegistry::setOverrides(const QHash<QString, QString> &overrides)
{
    m_overrides.clear();
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it)
        setOverride(it.key(), it.value());
}

QString KeybindingRegistry::sequenceFor(const QString &commandId) const
{
    if (m_overrides.contains(commandId))
        return m_overrides.value(commandId);
    return m_defaults.value(commandId);
}

QHash<QString, QString> KeybindingRegistry::bindings() const
{
    QHash<QString, QString> out = m_defaults;
    for (auto it = m_overrides.constBegin(); it != m_overrides.constEnd(); ++it)
        out.insert(it.key(), it.value());
    out.remove(QString());
    return out;
}

QStringList KeybindingRegistry::commandIds() const
{
    QStringList ids = bindings().keys();
    ids.sort();
    return ids;
}

QList<KeybindingConflict> KeybindingRegistry::conflicts() const
{
    QHash<QString, QStringList> bySequence;
    const auto all = bindings();
    for (auto it = all.constBegin(); it != all.constEnd(); ++it)
        bySequence[it.value()] << it.key();

    QList<KeybindingConflict> out;
    for (auto it = bySequence.constBegin(); it != bySequence.constEnd(); ++it)
    {
        if (it.value().size() < 2)
            continue;
        QStringList ids = it.value();
        ids.sort();
        out.append(KeybindingConflict{it.key(), ids});
    }
    std::sort(out.begin(), out.end(), [](const KeybindingConflict &a, const KeybindingConflict &b) {
        return a.sequence < b.sequence;
    });
    return out;
}

QString KeybindingRegistry::normalize(const QString &sequence)
{
    const QStringList raw = sequence.split(QLatin1Char(','));
    QStringList parts;
    for (const QString &part : raw)
    {
        const QString trimmed = part.trimmed();
        if (trimmed.isEmpty())
            return QString();
        const QKeySequence keySequence(trimmed);
        if (keySequence.isEmpty())
            return QString();
        parts << keySequence.toString(QKeySequence::PortableText);
    }
    return parts.join(QStringLiteral(", "));
}

bool KeybindingRegistry::isChord(const QString &sequence)
{
    return sequence.contains(QLatin1Char(','));
}

QStringList KeybindingRegistry::chordSteps(const QString &sequence)
{
    QStringList out;
    const QString normalized = normalize(sequence);
    if (normalized.isEmpty())
        return out;
    for (const QString &part : normalized.split(QStringLiteral(", ")))
        out << part;
    return out;
}

} // namespace nextpilot::plotscope::gui
