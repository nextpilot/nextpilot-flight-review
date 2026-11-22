#include "DerivedSeriesHub.h"

namespace nextpilot::plotscope::context
{
using nextpilot::plotscope::registry::RegistryNode;
using nextpilot::plotscope::registry::RegistryKind;
using nextpilot::plotscope::registry::RegistryTree;

DerivedSeriesHub::DerivedSeriesHub(RegistryTree &tree, QObject *parent)
    : RegistryNode(tree, RegistryKind::DerivedSeries, QStringLiteral("派生序列"), parent)
{
}

QString DerivedSeriesHub::registerSeries(const Entry &entry)
{
    const bool replacing = m_entries.contains(entry.name);
    Entry *existing = m_entries.find(entry.name);
    if (existing)
        *existing = entry; // 同名覆盖 = 刷新缓存（位置不动）
    else
        m_entries.insert(entry.name, entry);
    if (!replacing)
        Q_EMIT objectAdded(entry.name); // M2.8：统一节点信号（带 key）
    Q_EMIT changed();
    return entry.name;
}

const DerivedSeriesHub::Entry *DerivedSeriesHub::get(const QString &name) const
{
    return m_entries.find(name);
}

bool DerivedSeriesHub::appendSeries(const QString &name, const QVector<double> &tNew,
                                         const QVector<double> &vNew, qint64 waterUs)
{
    Entry *e = m_entries.find(name);
    if (!e)
        return false;
    e->t += tNew;
    e->v += vNew;
    e->waterUs = waterUs;
    Q_EMIT changed();
    return true;
}

bool DerivedSeriesHub::remove(const QString &name)
{
    if (!m_entries.remove(name))
        return false;
    Q_EMIT objectRemoved(name); // M2.8：统一节点信号（带 key）
    Q_EMIT changed();
    return true;
}

int DerivedSeriesHub::removeAllForDataset(int datasetId)
{
    int removed = 0;
    const auto entries = m_entries.values();
    for (const Entry &e : entries)
    {
        if (e.datasetId == datasetId && m_entries.remove(e.name))
        {
            Q_EMIT objectRemoved(e.name); // M2.8：统一节点信号（带 key）
            ++removed;
        }
    }
    if (removed > 0)
        Q_EMIT changed();
    return removed;
}

QStringList DerivedSeriesHub::names() const
{
    return m_entries.keys();
}

QVector<DerivedSeriesHub::Entry> DerivedSeriesHub::all() const
{
    const auto vals = m_entries.values();
    return QVector<Entry>(vals.cbegin(), vals.cend());
}

} // namespace nextpilot::plotscope::context
