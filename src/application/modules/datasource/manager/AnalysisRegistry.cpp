#include "datasource/AnalysisRegistry.h"

namespace ps::data_source
{

AnalysisRegistry &AnalysisRegistry::instance()
{
    static AnalysisRegistry registry;
    return registry;
}

void AnalysisRegistry::add(const AnalysisEntry &entry)
{
    for (AnalysisEntry &existing : m_entries)
    {
        if (existing.id == entry.id)
        {
            existing = entry;
            return;
        }
    }
    m_entries.append(entry);
}

QVector<AnalysisEntry> AnalysisRegistry::forFormat(const QString &format) const
{
    QVector<AnalysisEntry> out;
    for (const AnalysisEntry &entry : m_entries)
    {
        if (entry.formats.isEmpty() || entry.formats.contains(format, Qt::CaseInsensitive))
            out.append(entry);
    }
    return out;
}

} // namespace ps::data_source
