#pragma once

#include "DataSourceModel.h"
#include "TimeBookmark.h"

#include <QVector>
#include <deque>

namespace ps::data_source
{

// 数据源模块的非 UI 状态：源、当前源、字段、时间书签。
// 不持有 QWidget，不依赖 shell；加载/连接仍由 widget 层调用 DataSourceDock。
class DataSourceController
{
public:
    DataSourceController() = default;
    ~DataSourceController() = default;

    std::deque<DataSource> &sources() { return m_sources; }
    const std::deque<DataSource> &sources() const { return m_sources; }
    bool hasSources() const { return !m_sources.empty(); }
    void clearSources();

    DataSource *sourceById(int id);
    const DataSource *sourceById(int id) const;

    int activeSourceId() const { return m_activeSource; }
    void setActiveSource(int id) { m_activeSource = id; }

    QVector<const PsChannelInfo *> &channels() { return m_channels; }
    const QVector<const PsChannelInfo *> &channels() const { return m_channels; }
    void setChannels(const QVector<const PsChannelInfo *> &channels) { m_channels = channels; }
    void clearChannels() { m_channels.clear(); }

    QVector<TimeBookmark> &timeMarkers() { return m_timeMarkers; }
    const QVector<TimeBookmark> &timeMarkers() const { return m_timeMarkers; }

    void upsertTimeMarker(double t, const QString &label);
    bool removeTimeMarker(const QString &id);
    bool renameTimeMarker(const QString &id, const QString &label);
    bool moveTimeMarker(const QString &id, double to);

private:
    std::deque<DataSource> m_sources;
    QVector<const PsChannelInfo *> m_channels;
    QVector<TimeBookmark> m_timeMarkers;
    int m_activeSource = -1;
};

} // namespace ps::data_source
