#include "analysis/AnalysisRegistry.h"

namespace ps::analysis
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

} // namespace ps::analysis
