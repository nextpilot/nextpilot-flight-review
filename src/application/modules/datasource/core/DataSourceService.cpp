#include "DataSourceService.h"
#include "DataSourceController.h"

namespace ps::data_source
{

DataSourceService::DataSourceService(const DataSourceController &controller)
    : m_controller(&controller)
{
}

bool DataSourceService::hasSources() const { return m_controller->hasSources(); }
const std::deque<DataSource> &DataSourceService::sources() const { return m_controller->sources(); }
const DataSource *DataSourceService::sourceById(int id) const { return m_controller->sourceById(id); }
int DataSourceService::activeSourceId() const { return m_controller->activeSourceId(); }
const QVector<const PsChannelInfo *> &DataSourceService::channels() const { return m_controller->channels(); }
const QVector<ps::TimeBookmark> &DataSourceService::timeMarkers() const { return m_controller->timeMarkers(); }

} // namespace ps::data_source
