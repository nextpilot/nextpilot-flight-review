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
    m_lastCacheHits = 0;
    for (const auto &entry : m_entries)
    {
        if (!entry.serialize)
            continue;

        // 可选 revision 短路：版本未变则复用上次序列化结果。
        // 注意语义是"不重算"而非"不写入"——collect 产出的是全量文档，
        // 跳过写入会让该条状态从快照里消失（数据丢失）。
        bool reused = false;
        bool hasRevision = static_cast<bool>(entry.revision);
        const quint64 rev = hasRevision ? entry.revision() : 0; // 只取一次，避免 serialize 期间版本漂移
        if (hasRevision)
        {
            const auto cached = m_serializeCache.constFind(entry.id);
            if (cached != m_serializeCache.cend() && cached->first == rev)
            {
                // 缓存命中：复用上次的 JSON，但仍写入本次文档，保证全量完整。
                if (entry.scope == PersistentStateEntry::Session)
                    session.insert(entry.id, cached->second);
                else
                    document.insert(entry.id, cached->second);
                ++m_lastCacheHits;
                reused = true;
            }
        }
        if (reused)
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
        if (hasRevision)
            m_serializeCache.insert(entry.id, qMakePair(rev, obj));
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
