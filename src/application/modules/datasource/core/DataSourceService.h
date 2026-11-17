#pragma once

#include "DataSourceModel.h"
#include "TimeBookmark.h"

#include <deque>

namespace ps::data_source
{

class DataSourceController;

// 数据源只读服务：消费方（Dock、Ribbon、脚本桥）只查状态，不直接改控制器。
class DataSourceService
{
public:
    explicit DataSourceService(const DataSourceController &controller);

    bool hasSources() const;
    const std::deque<DataSource> &sources() const;
    const DataSource *sourceById(int id) const;
    int activeSourceId() const;
    const QVector<const PsChannelInfo *> &channels() const;
    const QVector<ps::TimeBookmark> &timeMarkers() const;

private:
    const DataSourceController *m_controller = nullptr;
};

} // namespace ps::data_source
