#include "DataSourceController.h"

#include <QUuid>

#include <algorithm>

namespace ps::data_source
{

void DataSourceController::clearSources()
{
    m_sources.clear();
    m_channels.clear();
    m_activeSource = -1;
}

DataSource *DataSourceController::sourceById(int id)
{
    for (DataSource &src : m_sources)
        if (src.config.id == id)
            return &src;
    return nullptr;
}

const DataSource *DataSourceController::sourceById(int id) const
{
    for (const DataSource &src : m_sources)
        if (src.config.id == id)
            return &src;
    return nullptr;
}

void DataSourceController::upsertTimeMarker(double t, const QString &label)
{
    for (auto &mk : m_timeMarkers)
        if (qAbs(mk.t - t) < 1e-4)
        {
            mk.label = label;
            return;
        }
    m_timeMarkers.append({QStringLiteral("bm-") + QUuid::createUuid().toString(QUuid::WithoutBraces),
                          t, label});
    std::sort(m_timeMarkers.begin(), m_timeMarkers.end(),
              [](const TimeBookmark &a, const TimeBookmark &b) { return a.t < b.t; });
}

bool DataSourceController::removeTimeMarker(const QString &id)
{
    for (int i = 0; i < m_timeMarkers.size(); ++i)
    {
        if (m_timeMarkers.at(i).id != id)
            continue;
        m_timeMarkers.removeAt(i);
        return true;
    }
    return false;
}

bool DataSourceController::renameTimeMarker(const QString &id, const QString &label)
{
    for (auto &mk : m_timeMarkers)
    {
        if (mk.id != id)
            continue;
        mk.label = label;
        return true;
    }
    return false;
}

bool DataSourceController::moveTimeMarker(const QString &id, double to)
{
    for (auto &mk : m_timeMarkers)
    {
        if (mk.id != id)
            continue;
        if (qAbs(to - mk.t) < 1e-4)
            return false;
        mk.t = to;
        std::sort(m_timeMarkers.begin(), m_timeMarkers.end(),
                  [](const TimeBookmark &a, const TimeBookmark &b) { return a.t < b.t; });
        return true;
    }
    return false;
}

} // namespace ps::data_source
