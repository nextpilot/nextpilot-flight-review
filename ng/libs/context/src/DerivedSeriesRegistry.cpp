#include "DerivedSeriesRegistry.h"

namespace nextpilot::plotscope::context
{

DerivedSeriesRegistry::DerivedSeriesRegistry(QObject *parent)
    : QObject(parent)
{
}

int DerivedSeriesRegistry::indexOf(const QString &name) const
{
    for (int i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries.at(i).name == name)
            return i;
    }
    return -1;
}

QString DerivedSeriesRegistry::registerSeries(const Entry &entry)
{
    const int existing = indexOf(entry.name);
    if (existing >= 0)
        m_entries[existing] = entry; // 同名覆盖 = 刷新缓存
    else
        m_entries.append(entry);
    Q_EMIT changed();
    return entry.name;
}

const DerivedSeriesRegistry::Entry *DerivedSeriesRegistry::get(const QString &name) const
{
    const int i = indexOf(name);
    return i >= 0 ? &m_entries.at(i) : nullptr;
}

bool DerivedSeriesRegistry::appendSeries(const QString &name, const QVector<double> &tNew,
                                         const QVector<double> &vNew, qint64 waterUs)
{
    const int i = indexOf(name);
    if (i < 0)
        return false;
    m_entries[i].t += tNew;
    m_entries[i].v += vNew;
    m_entries[i].waterUs = waterUs;
    Q_EMIT changed();
    return true;
}

bool DerivedSeriesRegistry::remove(const QString &name)
{
    const int i = indexOf(name);
    if (i < 0)
        return false;
    m_entries.removeAt(i);
    Q_EMIT changed();
    return true;
}

int DerivedSeriesRegistry::removeAllForDataset(int datasetId)
{
    int removed = 0;
    for (int i = m_entries.size() - 1; i >= 0; --i)
    {
        if (m_entries.at(i).datasetId == datasetId)
        {
            m_entries.removeAt(i);
            ++removed;
        }
    }
    if (removed > 0)
        Q_EMIT changed();
    return removed;
}

QStringList DerivedSeriesRegistry::names() const
{
    QStringList out;
    out.reserve(m_entries.size());
    for (const Entry &e : m_entries)
        out << e.name;
    return out;
}

QVector<DerivedSeriesRegistry::Entry> DerivedSeriesRegistry::all() const
{
    return m_entries;
}

} // namespace nextpilot::plotscope::context
