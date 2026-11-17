#pragma once

#include "DataSourceDriver.h"

#include <QPair>
#include <QString>
#include <QVector>

#include <memory>

class LiveFrameSink;

namespace ps::data_source
{

// 实时源驱动：拥有扩展 handle、共享 transport 的 fanout 句柄和整帧 sink。
// poll 由宿主 30ms 定时器调用；驱动不解析协议。
class LiveDataSourceDriver : public DataSourceDriver
{
public:
    LiveDataSourceDriver(QString extensionId, void *handle,
                         std::shared_ptr<LiveFrameSink> sink,
                         QVector<QPair<QString, void *>> fanout);
    ~LiveDataSourceDriver() override;

    bool open(DataSource &src, QString *err) override;
    void close(DataSource &src) override;
    bool poll(DataSource &src, QString *err) override;
    bool reload(DataSource &src, QString *err) override;

    const QString &extensionId() const { return m_extensionId; }
    void *handle() const { return m_handle; }
    LiveFrameSink *sink() const { return m_sink.get(); }

private:
    void closeHandles();
    void closeResources();

    QString m_extensionId;
    void *m_handle = nullptr;
    std::shared_ptr<LiveFrameSink> m_sink;
    QVector<QPair<QString, void *>> m_fanout;
};

} // namespace ps::data_source
