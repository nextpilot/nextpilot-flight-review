#include "DerivedSeriesRegistry.h"

namespace nextpilot::plotscope::context
{

DerivedSeriesRegistry::DerivedSeriesRegistry(QObject *parent)
    : QObject(parent)
{
}

QString DerivedSeriesRegistry::registerSeries(const Entry &entry)
{
    Entry *existing = m_entries.find(entry.name);
    if (existing)
        *existing = entry; // 同名覆盖 = 刷新缓存（位置不动）
    else
        m_entries.insert(entry.name, entry);
    Q_EMIT changed();
    return entry.name;
}

const DerivedSeriesRegistry::Entry *DerivedSeriesRegistry::get(const QString &name) const
{
    return m_entries.find(name);
}

bool DerivedSeriesRegistry::appendSeries(const QString &name, const QVector<double> &tNew,
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

bool DerivedSeriesRegistry::remove(const QString &name)
{
    if (!m_entries.remove(name))
        return false;
    Q_EMIT changed();
    return true;
}

int DerivedSeriesRegistry::removeAllForDataset(int datasetId)
{
    int removed = 0;
    const auto entries = m_entries.values();
    for (const Entry &e : entries)
    {
        if (e.datasetId == datasetId && m_entries.remove(e.name))
            ++removed;
    }
    if (removed > 0)
        Q_EMIT changed();
    return removed;
}

QStringList DerivedSeriesRegistry::names() const
{
    return m_entries.keys();
}

QVector<DerivedSeriesRegistry::Entry> DerivedSeriesRegistry::all() const
{
    const auto vals = m_entries.values();
    return QVector<Entry>(vals.cbegin(), vals.cend());
}

} // namespace nextpilot::plotscope::context
