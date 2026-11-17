#pragma once

// 宿主数据访问契约的唯一实现：datasource 拥有 store，这里只做翻译。
// 结构约束：不持有 QWidget*，store 由调用方注入——
// GUI 用 DataSourceSession 的 store，CLI（--run）用一次性局部 store。

#include "HostDataService.h"
#include "SourceModel.h"

namespace ps::data_source
{

class HostDataBridge final : public host::HostDataService
{
public:
    explicit HostDataBridge(SourceStore &store);

    QVector<host::SourceInfo> sources() override;
    QVector<host::ChannelInfo> channels(int sourceId) override;
    host::Result slice(int sourceId, const QString &channel, double t0, double t1,
                            int maxPoints, QVector<double> *t, QVector<double> *v) override;
    host::LiveStatus liveStatus(int sourceId) override;
    host::Result seriesView(int sourceId, const QString &channel, SeriesView *out) override;

    host::JobHandle loadFile(const QString &path, const QString &loaderOptions) override;
    host::Result closeSource(int sourceId) override;
    host::JobHandle reloadSource(int sourceId) override;
    host::JobHandle exportCsv(int sourceId, const QString &path, bool overwrite) override;

    host::JobHandle connectLive(const QString &extensionId, const QString &configJson) override;
    host::Result disconnectLive(int sourceId) override;
    host::Result setPaused(int sourceId, bool paused) override;
    host::Result startRecording(int sourceId, const QString &format, const QString &path) override;
    host::Result stopRecording(int sourceId) override;

private:
    SourceStore &m_store;
};

} // namespace ps::data_source
