#include "InstantiationService.h"

#include <QVector>
#include <algorithm>

namespace nextpilot::plotscope::instantiation {

void InstantiationService::registerSingleton(const QString &id, void *instance, const QString &owner, int version,
                                             const QString &typeTag)
{
    if (id.isEmpty())
    {
        appendDiagnostic(QStringLiteral("registerSingleton: empty id (owner=%1)").arg(owner));
        return;
    }
    if (instance == nullptr)
    {
        appendDiagnostic(QStringLiteral("registerSingleton: null instance (id=%1 owner=%2)").arg(id, owner));
        return;
    }
    if (m_entries.contains(id))
    {
        const Entry &old = m_entries.value(id);
        appendDiagnostic(QStringLiteral("registerSingleton: duplicate id '%1' (old owner=%2, "
                                        "new owner=%3) -> rejected, kept old")
                             .arg(id, old.owner, owner));
        return;
    }
    Entry e;
    e.instance = instance;
    e.owner = owner;
    e.version = version;
    e.typeTag = typeTag;
    e.seq = ++m_seq;
    m_entries.insert(id, e);
}

void InstantiationService::unregisterOwner(const QString &owner)
{
    QVector<QPair<int, QString>> ordered;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
        if (it.value().owner == owner)
            ordered.append(qMakePair(it.value().seq, it.key()));

    std::sort(ordered.begin(), ordered.end(),
              [](const QPair<int, QString> &a, const QPair<int, QString> &b) { return a.first > b.first; });

    for (const auto &kv : ordered)
        if (m_entries.remove(kv.second) > 0)
            m_removedLog.append(kv.second);
}

bool InstantiationService::unregister(const QString &id) { return m_entries.remove(id) > 0; }

const InstantiationService::Entry *InstantiationService::findEntry(const QString &id) const
{
    auto it = m_entries.constFind(id);
    if (it == m_entries.cend())
        return nullptr;
    return &it.value();
}

void *InstantiationService::getServiceUnchecked(const QString &id) const
{
    const Entry *e = findEntry(id);
    return e ? e->instance : nullptr;
}

QStringList InstantiationService::ids() const
{
    QStringList out = m_entries.keys();
    out.sort();
    return out;
}

QStringList InstantiationService::idsByOwner(const QString &owner) const
{
    QStringList out;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
        if (it.value().owner == owner)
            out.append(it.key());
    out.sort();
    return out;
}

QStringList InstantiationService::diagnostics() const { return m_diagnostics; }

void InstantiationService::appendDiagnostic(const QString &msg) const { m_diagnostics.append(msg); }

void InstantiationService::clear()
{
    m_entries.clear();
    m_diagnostics.clear();
    m_removedLog.clear();
    m_seq = 0;
}

} // namespace nextpilot::plotscope::instantiation
