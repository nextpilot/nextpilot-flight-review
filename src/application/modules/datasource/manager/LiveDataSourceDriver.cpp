#include "LiveDataSourceDriver.h"

#include "DataSourceEntry.h"
#include "DataSourceModel.h"
#include "DataSourceRegistry.h"
#include "LiveFrameSink.h"

namespace ps::data_source
{

LiveDataSourceDriver::LiveDataSourceDriver(QString extensionId, void *handle,
                                           std::shared_ptr<LiveFrameSink> sink,
                                           QVector<QPair<QString, void *>> fanout)
    : m_extensionId(std::move(extensionId)), m_handle(handle),
      m_sink(std::move(sink)), m_fanout(std::move(fanout))
{
}

LiveDataSourceDriver::~LiveDataSourceDriver()
{
    closeResources();
}

void LiveDataSourceDriver::closeHandles()
{
    for (const auto &f : m_fanout)
        if (const DataSourceEntry *p = DataSourceRegistry::instance().findSourceById(f.first))
            if (f.second)
                p->close(f.second);
    m_fanout.clear();

    if (m_handle)
    {
        if (const DataSourceEntry *p = DataSourceRegistry::instance().findSourceById(m_extensionId))
            p->close(m_handle);
        m_handle = nullptr;
    }
}

void LiveDataSourceDriver::closeResources()
{
    closeHandles();
    if (m_sink)
    {
        m_sink->stopRecording();
        m_sink.reset();
    }
}

bool LiveDataSourceDriver::open(DataSource &src, QString *err)
{
    if (!m_handle || !m_sink)
    {
        if (err)
            *err = QStringLiteral("实时句柄已关闭");
        return false;
    }
    src.handle = m_handle;
    src.liveSink = m_sink;
    src.fanout = m_fanout;
    return true;
}

void LiveDataSourceDriver::close(DataSource &src)
{
    closeResources();
    src.handle = nullptr;
    src.liveSink.reset();
    src.ds = nullptr;
    src.fanout.clear();
    src.channels.clear();
}

bool LiveDataSourceDriver::poll(DataSource &, QString *err)
{
    if (!m_handle)
        return false;
    const DataSourceEntry *p = DataSourceRegistry::instance().findSourceById(m_extensionId);
    if (!p || !p->poll)
    {
        if (err)
            *err = QStringLiteral("实时扩展不存在：%1").arg(m_extensionId);
        return false;
    }
    const int rc = p->poll(m_handle, 0, err);
    return rc == 0;
}

bool LiveDataSourceDriver::reload(DataSource &src, QString *err)
{
    if (!m_sink)
    {
        if (err)
            *err = QStringLiteral("实时 sink 已关闭");
        return false;
    }
    // 共享 transport 的 fanout 句柄无法按原拓扑重建，交给上层重新 openLiveInput。
    if (!m_fanout.isEmpty())
    {
        if (err)
            *err = QStringLiteral("共享 transport 源不支持原地重连");
        return false;
    }
    const DataSourceEntry *p = DataSourceRegistry::instance().findSourceById(m_extensionId);
    if (!p || !p->open)
    {
        if (err)
            *err = QStringLiteral("实时扩展不存在：%1").arg(m_extensionId);
        return false;
    }

    closeHandles();
    void *handle = nullptr;
    const int rc = p->open(src.config.configJson, m_sink->sink(), &handle, err);
    if (rc != 0 || !handle)
        return false;
    m_handle = handle;
    src.handle = handle;
    src.liveSink = m_sink;
    src.ds = m_sink->dataset();
    ++src.generation;
    return true;
}

} // namespace ps::data_source
