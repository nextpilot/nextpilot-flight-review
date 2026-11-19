#include "Sink.h"

#include <QFile>
#include <QHostAddress>
#include <QUdpSocket>

namespace nextpilot::plotscope::rec
{

namespace
{

// 文件后端：写到本地文件。顺序保证、无丢失、可回放。
class FileSink final : public IByteSink
{
public:
    ~FileSink() override { close(nullptr); }

    bool open(const QString &endpoint, QString *err) override
    {
        close(nullptr);
        m_file.setFileName(endpoint);
        if (!m_file.open(QIODevice::WriteOnly))
        {
            if (err)
                *err = QStringLiteral("无法写入录制文件: %1").arg(endpoint);
            return false;
        }
        m_path = endpoint;
        m_bytes = 0;
        return true;
    }

    bool write(const char *data, qint64 size, QString *err) override
    {
        if (!m_file.isOpen() || !data || size <= 0)
            return true;
        const qint64 written = m_file.write(data, size);
        if (written != size)
        {
            if (err)
                *err = QStringLiteral("录制写入失败: %1").arg(m_path);
            return false;
        }
        m_bytes += written;
        return true;
    }

    bool flush(QString *err) override
    {
        if (!m_file.isOpen())
            return true;
        if (!m_file.flush() && err)
        {
            *err = QStringLiteral("录制文件刷新失败: %1").arg(m_path);
            return false;
        }
        return true;
    }

    bool close(QString *err) override
    {
        if (!m_file.isOpen())
            return true;
        const bool ok = m_file.flush();
        m_file.close();
        m_path.clear();
        if (!ok && err)
            *err = QStringLiteral("录制文件刷新失败");
        return ok;
    }

    qint64 bytesWritten() const override { return m_bytes; }

private:
    QFile m_file;
    QString m_path;
    qint64 m_bytes = 0;
};

// UDP 后端：组播/单播投递。
//
// 特性：网络投递天然多路复用（同一组播地址可承载不同录制流），
// 但可能丢包、且有单包大小上限——因此这里按上限切块逐包发出。
class UdpSink final : public IByteSink
{
public:
    // UDP 单包上限（保守取 60KB，避开 IP 分片）。
    static constexpr qint64 kMaxDatagram = 60 * 1024;

    ~UdpSink() override { close(nullptr); }

    bool open(const QString &endpoint, QString *err) override
    {
        close(nullptr);
        const int sep = endpoint.lastIndexOf(':');
        if (sep <= 0)
        {
            if (err)
                *err = QStringLiteral("UDP 端点格式应为 host:port: %1").arg(endpoint);
            return false;
        }
        const QString host = endpoint.left(sep);
        const quint16 port = static_cast<quint16>(endpoint.mid(sep + 1).toUShort());
        if (port == 0)
        {
            if (err)
                *err = QStringLiteral("UDP 端口非法: %1").arg(endpoint);
            return false;
        }

        m_socket = std::make_unique<QUdpSocket>();
        // connectToHost 无返回值（异步连接），这里用 state 立即判定：
        // 目标地址非法时 socket 进入 UnconnectedState。
        m_socket->connectToHost(host, port);
        if (m_socket->state() == QAbstractSocket::UnconnectedState
            && m_socket->error() != QAbstractSocket::UnknownSocketError)
        {
            if (err)
                *err = QStringLiteral("无法连接 UDP 目标: %1 (%2)")
                           .arg(endpoint, m_socket->errorString());
            m_socket.reset();
            return false;
        }
        m_endpoint = endpoint;
        m_bytes = 0;
        return true;
    }

    bool write(const char *data, qint64 size, QString *err) override
    {
        if (!m_socket || !data || size <= 0)
            return true;
        qint64 offset = 0;
        while (offset < size)
        {
            const qint64 chunk = qMin(kMaxDatagram, size - offset);
            const qint64 sent = m_socket->write(data + offset, chunk);
            if (sent < 0)
            {
                if (err)
                    *err = QStringLiteral("UDP 发送失败: %1").arg(m_endpoint);
                return false;
            }
            offset += sent;
        }
        m_bytes += size;
        return true;
    }

    bool flush(QString *err) override
    {
        if (!m_socket)
            return true;
        if (!m_socket->flush() && err)
        {
            *err = QStringLiteral("UDP 冲刷失败: %1").arg(m_endpoint);
            return false;
        }
        return true;
    }

    bool close(QString *err) override
    {
        if (!m_socket)
            return true;
        m_socket->flush();
        m_socket->close();
        m_socket.reset();
        m_endpoint.clear();
        return true;
    }

    qint64 bytesWritten() const override { return m_bytes; }

private:
    std::unique_ptr<QUdpSocket> m_socket;
    QString m_endpoint;
    qint64 m_bytes = 0;
};

} // namespace

std::unique_ptr<IByteSink> createByteSink(const QString &backendId, QString *err)
{
    if (backendId == nextpilot::plotscope::rec::BackendId::File)
        return std::make_unique<FileSink>();
    if (backendId == nextpilot::plotscope::rec::BackendId::Udp)
        return std::make_unique<UdpSink>();
    if (err)
        *err = QStringLiteral("未注册的录制后端: %1").arg(backendId);
    return nullptr;
}

} // namespace nextpilot::plotscope::rec
