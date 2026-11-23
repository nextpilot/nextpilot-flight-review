#include "PersistentStateHub.h"

namespace nextpilot::plotscope::context
{

void PersistentStateHub::registerEntry(PersistentStateEntry entry)
{
    m_entries.append(std::move(entry));
}

QJsonObject PersistentStateHub::collect() const
{
    QJsonObject session;
    QJsonObject document;
    for (const auto &entry : m_entries)
    {
        if (!entry.serialize)
            continue;
        QJsonObject obj;
        try
        {
            obj = entry.serialize();
        }
        catch (...)
        {
            continue; // 单条失败不影响其余条目
        }
        if (entry.scope == PersistentStateEntry::Session)
            session.insert(entry.id, obj);
        else
            document.insert(entry.id, obj);
    }

    QJsonObject root;
    root.insert(QStringLiteral("session"), session);
    root.insert(QStringLiteral("document"), document);
    return root;
}

void PersistentStateHub::restore(const QJsonObject &hubDoc)
{
    const QJsonObject session = hubDoc.value(QStringLiteral("session")).toObject();
    const QJsonObject document = hubDoc.value(QStringLiteral("document")).toObject();

    for (const auto &entry : m_entries)
    {
        if (!entry.restore)
            continue;
        const QJsonObject seg = entry.scope == PersistentStateEntry::Session ? session : document;
        if (!seg.contains(entry.id))
            continue;
        try
        {
            entry.restore(seg.value(entry.id).toObject());
        }
        catch (...)
        {
            if (!entry.bestEffort)
                throw;
        }
    }
}

QStringList PersistentStateHub::ids() const
{
    QStringList out;
    out.reserve(m_entries.size());
    for (const auto &entry : m_entries)
        out.append(entry.id);
    return out;
}

} // namespace nextpilot::plotscope::context
