#pragma once

// S1 只读数据面：脚本侧 HostService 的唯一实现。
// 结构约束：不持有 QWidget*，store 由调用方注入——
// GUI 用 DataSourceSession 的 store，CLI（--run）用一次性局部 store。

#include "ScriptHostService.h"
#include "SourceModel.h"

namespace ps::data_source
{

class ScriptHostBridge final : public scripting::HostService
{
public:
    explicit ScriptHostBridge(SourceStore &store);

    QVector<scripting::SourceInfo> sources() override;
    QVector<scripting::ChannelInfo> channels(int sourceId) override;
    scripting::Result slice(int sourceId, const QString &channel, double t0, double t1,
                            int maxPoints, QVector<double> *t, QVector<double> *v) override;
    scripting::LiveStatus liveStatus(int sourceId) override;

    scripting::JobHandle loadFile(const QString &path, const QString &loaderOptions) override;
    scripting::Result closeSource(int sourceId) override;
    scripting::JobHandle reloadSource(int sourceId) override;
    scripting::JobHandle exportCsv(int sourceId, const QString &path, bool overwrite) override;

    scripting::JobHandle connectLive(const QString &extensionId, const QString &configJson) override;
    scripting::Result disconnectLive(int sourceId) override;
    scripting::Result setPaused(int sourceId, bool paused) override;
    scripting::Result startRecording(int sourceId, const QString &format, const QString &path) override;
    scripting::Result stopRecording(int sourceId) override;

private:
    SourceStore &m_store;
};

} // namespace ps::data_source
