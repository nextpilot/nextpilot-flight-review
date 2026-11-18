#include "RawCodec.h"

#include <QFileInfo>
#include <cstring>
#include <QJsonDocument>
#include <QJsonObject>

namespace ng::rec
{

// === RAW 编解码 ===

const char *RawCodec::magic()
{
    return "PSRAW\0\0";
}

namespace
{
void putU32(QByteArray &out, quint32 v)
{
    out.append(static_cast<char>(v & 0xFF));
    out.append(static_cast<char>((v >> 8) & 0xFF));
    out.append(static_cast<char>((v >> 16) & 0xFF));
    out.append(static_cast<char>((v >> 24) & 0xFF));
}

quint32 getU32(const QByteArray &b, int off)
{
    if (off + 4 > b.size())
        return 0;
    return static_cast<quint32>(static_cast<quint8>(b[off]))
           | (static_cast<quint32>(static_cast<quint8>(b[off + 1])) << 8)
           | (static_cast<quint32>(static_cast<quint8>(b[off + 2])) << 16)
           | (static_cast<quint32>(static_cast<quint8>(b[off + 3])) << 24);
}
} // namespace

RawRecorder::~RawRecorder()
{
    close();
}

bool RawRecorder::open(std::shared_ptr<IByteSink> sink, const QByteArray &headerJson, QString *err)
{
    close();
    if (!sink)
    {
        if (err)
            *err = QStringLiteral("raw 录制缺少输出端");
        return false;
    }
    m_sink = std::move(sink);

    QByteArray header = headerJson;
    if (header.size() > static_cast<int>(RawCodec::kMaxHeaderBytes))
        header.truncate(RawCodec::kMaxHeaderBytes);

    QByteArray preamble;
    preamble.append(RawCodec::magic(), RawCodec::kMagicSize);
    putU32(preamble, RawCodec::kVersion);
    putU32(preamble, static_cast<quint32>(header.size()));
    preamble.append(header);

    if (!m_sink->write(preamble, err))
    {
        m_sink.reset();
        return false;
    }
    m_bytes = 0;
    return true;
}

bool RawRecorder::write(const uint8_t *data, qint64 size, QString *err)
{
    if (!m_sink || !data || size <= 0)
        return true;
    if (!m_sink->write(reinterpret_cast<const char *>(data), size, err))
        return false;
    m_bytes += size;
    return true;
}

bool RawRecorder::write(const QByteArray &data, QString *err)
{
    if (data.isEmpty())
        return true;
    return write(reinterpret_cast<const uint8_t *>(data.constData()), data.size(), err);
}

bool RawRecorder::close(QString *err)
{
    if (!m_sink)
        return true;
    // 只冲刷，不关闭 sink —— sink 的生命周期归调用方（Recorder 组合层）。
    const bool ok = m_sink->flush(err);
    m_sink.reset();
    m_bytes = 0;
    return ok;
}

RawPlayer::~RawPlayer()
{
    if (m_file.isOpen())
        m_file.close();
}

bool RawPlayer::open(const QString &path, QString *err)
{
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly))
    {
        if (err)
            *err = QStringLiteral("无法读取录制文件: %1").arg(path);
        return false;
    }

    QByteArray preamble = m_file.read(RawCodec::kMagicSize + 8);
    if (preamble.size() < RawCodec::kMagicSize + 8
        || std::memcmp(preamble.constData(), RawCodec::magic(), RawCodec::kMagicSize) != 0)
    {
        if (err)
            *err = QStringLiteral("不是 raw 录制文件: %1").arg(path);
        m_file.close();
        return false;
    }
    const quint32 version = getU32(preamble, RawCodec::kMagicSize);
    const quint32 headerLen = getU32(preamble, RawCodec::kMagicSize + 4);
    if (version != RawCodec::kVersion)
    {
        if (err)
            *err = QStringLiteral("raw 录制版本不支持: %1").arg(version);
        m_file.close();
        return false;
    }
    if (headerLen > RawCodec::kMaxHeaderBytes)
    {
        if (err)
            *err = QStringLiteral("raw 录制头长度异常: %1").arg(headerLen);
        m_file.close();
        return false;
    }
    m_header = m_file.read(headerLen);
    m_total = QFileInfo(path).size();
    return true;
}

QByteArray RawPlayer::read(qint64 maxSize, QString *err)
{
    if (!m_file.isOpen() || m_file.atEnd() || maxSize <= 0)
        return QByteArray();
    QByteArray chunk = m_file.read(maxSize);
    if (chunk.isEmpty() && err && !m_file.atEnd())
        *err = QStringLiteral("读取录制文件失败");
    return chunk;
}

} // namespace ng::rec
