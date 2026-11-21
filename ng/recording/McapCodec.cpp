#include "McapCodec.h"

#include <QtEndian>

namespace nextpilot::plotscope::rec
{

namespace
{
// MCAP 协议常量（参考老仓库 McapWriter.cpp，与 MCAP 规范一致）
constexpr quint8 kOpHeader = 0x01;
constexpr quint8 kOpFooter = 0x02;
constexpr quint8 kOpSchema = 0x03;
constexpr quint8 kOpChannel = 0x04;
constexpr quint8 kOpMessage = 0x05;
constexpr quint8 kOpChunk = 0x06;
constexpr quint8 kOpChunkIndex = 0x08;
constexpr quint8 kOpSummaryOffset = 0x0E;
constexpr quint8 kOpDataEnd = 0x0F;
constexpr quint64 kChunkTarget = 1024ull * 1024ull;
constexpr quint16 kSchemaId = 1;
} // namespace

QByteArray McapCodec::magic()
{
    QByteArray m;
    m.append(static_cast<char>(0x89));
    m.append("MCAP0\r\n", 7);
    return m;
}

bool McapCodec::hasMagic(const QByteArray &head)
{
    return head.size() >= 8 && head.startsWith(magic());
}

// === McapRecorder ===

McapRecorder::~McapRecorder()
{
    close();
}

void McapRecorder::putU16(QByteArray &out, quint16 v)
{
    out.append(static_cast<char>(v & 0xFF));
    out.append(static_cast<char>((v >> 8) & 0xFF));
}

void McapRecorder::putU32(QByteArray &out, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        out.append(static_cast<char>((v >> (i * 8)) & 0xFF));
}

void McapRecorder::putU64(QByteArray &out, quint64 v)
{
    for (int i = 0; i < 8; ++i)
        out.append(static_cast<char>((v >> (i * 8)) & 0xFF));
}

void McapRecorder::putString(QByteArray &out, const QString &s)
{
    const QByteArray utf8 = s.toUtf8();
    putU32(out, static_cast<quint32>(utf8.size()));
    out.append(utf8);
}

void McapRecorder::writeRecord(quint8 op, const QByteArray &body)
{
    if (!m_sink)
        return;
    const char opByte = static_cast<char>(op);
    m_sink->write(&opByte, 1, nullptr);
    quint64 len = body.size();
    // 小端 64 位长度
    char lenBytes[8];
    for (int i = 0; i < 8; ++i)
        lenBytes[i] = static_cast<char>((len >> (i * 8)) & 0xFF);
    m_sink->write(lenBytes, 8, nullptr);
    if (!body.isEmpty())
        m_sink->write(body, nullptr);
}

quint16 McapRecorder::channelIdFor(const QString &topic, bool created)
{
    const auto it = m_channels.constFind(topic);
    if (it != m_channels.constEnd())
        return it.value();
    if (!created)
        return 0;

    const quint16 id = static_cast<quint16>(m_channels.size() + 1);
    m_channels.insert(topic, id);
    m_sequences.insert(id, 0);

    // Channel record：id + schema_id + topic + message_encoding
    QByteArray body;
    putU16(body, id);
    putU16(body, kSchemaId);
    putString(body, topic);
    putString(body, QStringLiteral("application/octet-stream"));
    writeRecord(kOpChannel, body);
    return id;
}

bool McapRecorder::open(std::shared_ptr<IByteSink> sink, const QString &profile,
                        const QByteArray &schemaJson, QString *err)
{
    close();
    if (!sink)
    {
        if (err)
            *err = QStringLiteral("mcap 录制缺少输出端");
        return false;
    }
    m_sink = std::move(sink);
    m_channels.clear();
    m_sequences.clear();
    m_chunkRecords.clear();
    m_chunkHasMessage = false;
    m_failed = false;

    // 魔数 + Header record
    m_sink->write(McapCodec::magic(), nullptr);
    QByteArray header;
    putString(header, profile);
    putString(header, QStringLiteral("plotscope"));
    writeRecord(kOpHeader, header);

    // Schema record：plotscope 自包含元信息（parser id + 解析选项）。
    // 回放侧靠它选解析器，否则 mcap 只是一袋字节。
    QByteArray schema;
    putU16(schema, kSchemaId);
    putString(schema, QStringLiteral("plotscope/live"));
    putString(schema, QStringLiteral("jsonschema"));
    putString(schema, QString::fromUtf8(schemaJson.isEmpty() ? QByteArray("{}") : schemaJson));
    writeRecord(kOpSchema, schema);

    return true;
}

bool McapRecorder::writeMessage(const QString &topic, const QByteArray &data, quint64 logTimeNs,
                                QString *err)
{
    if (!m_sink)
    {
        if (err)
            *err = QStringLiteral("MCAP 录制器未打开");
        return false;
    }
    const quint16 cid = channelIdFor(topic, true);
    const quint32 seq = m_sequences.value(cid, 0);
    m_sequences[cid] = seq + 1;

    // Message record body：channel_id + sequence + log_time + publish_time + data
    QByteArray body;
    putU16(body, cid);
    putU32(body, seq);
    putU64(body, logTimeNs);
    putU64(body, logTimeNs);
    body.append(data);

    // 组装进 chunk 缓冲：op + length + body
    m_chunkRecords.append(static_cast<char>(kOpMessage));
    quint64 blen = body.size();
    for (int i = 0; i < 8; ++i)
        m_chunkRecords.append(static_cast<char>((blen >> (i * 8)) & 0xFF));
    m_chunkRecords.append(body);

    if (!m_chunkHasMessage)
    {
        m_chunkStartTime = logTimeNs;
        m_chunkHasMessage = true;
    }
    m_chunkEndTime = logTimeNs;

    if (m_chunkRecords.size() >= static_cast<int>(kChunkTarget))
        flushChunk();
    return true;
}

void McapRecorder::flushChunk()
{
    if (m_chunkRecords.isEmpty() || !m_sink)
        return;
    // Chunk body: start_time + end_time + uncompressed_size + uncompressed_crc
    //             + compression + records
    QByteArray body;
    putU64(body, m_chunkStartTime);
    putU64(body, m_chunkEndTime);
    putU64(body, static_cast<quint64>(m_chunkRecords.size()));
    putU32(body, 0); // uncompressed_crc（未启用）
    putString(body, QString()); // compression
    body.append(m_chunkRecords);
    writeRecord(kOpChunk, body);
    m_chunkRecords.clear();
    m_chunkHasMessage = false;
}

bool McapRecorder::close(QString *err)
{
    if (!m_sink)
        return true;
    flushChunk();

    // DataEnd：data_section_crc
    QByteArray dataEnd;
    putU32(dataEnd, 0);
    writeRecord(kOpDataEnd, dataEnd);

    // Summary：重复 channel 信息（便于不扫全文就索引）
    for (auto it = m_channels.constBegin(); it != m_channels.constEnd(); ++it)
    {
        QByteArray body;
        putU16(body, it.value());
        putU16(body, kSchemaId);
        putString(body, it.key());
        putString(body, QStringLiteral("application/octet-stream"));
        writeRecord(kOpChannel, body);
    }

    // SummaryOffset + Footer
    QByteArray footer;
    putU32(footer, 0);        // summary_start
    putU32(footer, 0);        // summary_offset_start
    putU32(footer, 0);        // summary_crc
    writeRecord(kOpFooter, footer);

    const bool ok = m_sink->flush(nullptr);
    m_sink.reset();
    if (!ok && err)
        *err = QStringLiteral("MCAP 文件刷新失败");
    return ok;
}

// === McapPlayer ===

McapPlayer::~McapPlayer()
{
    if (m_file.isOpen())
        m_file.close();
}

quint16 McapPlayer::getU16(const QByteArray &b, int off)
{
    if (off + 2 > b.size())
        return 0;
    return static_cast<quint16>(static_cast<quint8>(b[off]))
           | (static_cast<quint16>(static_cast<quint8>(b[off + 1])) << 8);
}

quint32 McapPlayer::getU32(const QByteArray &b, int off)
{
    if (off + 4 > b.size())
        return 0;
    quint32 v = 0;
    for (int i = 3; i >= 0; --i)
        v = (v << 8) | static_cast<quint8>(b[off + i]);
    return v;
}

quint64 McapPlayer::getU64(const QByteArray &b, int off)
{
    if (off + 8 > b.size())
        return 0;
    quint64 v = 0;
    for (int i = 7; i >= 0; --i)
        v = (v << 8) | static_cast<quint8>(b[off + i]);
    return v;
}

QString McapPlayer::getString(const QByteArray &b, int &off)
{
    const quint32 len = getU32(b, off);
    off += 4;
    if (off + static_cast<int>(len) > b.size())
    {
        off = b.size();
        return QString();
    }
    const QString s = QString::fromUtf8(b.mid(off, static_cast<int>(len)));
    off += static_cast<int>(len);
    return s;
}

QByteArray McapPlayer::peekSchema(const QString &path, QString *err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        if (err)
            *err = QStringLiteral("无法读取 MCAP 文件: %1").arg(path);
        return QByteArray();
    }
    if (!McapCodec::hasMagic(f.read(8)))
    {
        if (err)
            *err = QStringLiteral("不是 MCAP 文件: %1").arg(path);
        return QByteArray();
    }
    while (!f.atEnd())
    {
        const QByteArray opByte = f.read(1);
        if (opByte.size() < 1)
            break;
        const quint8 op = static_cast<quint8>(opByte[0]);
        const QByteArray lenBytes = f.read(8);
        if (lenBytes.size() < 8)
            break;
        quint64 len = 0;
        for (int i = 0; i < 8; ++i)
            len |= static_cast<quint64>(static_cast<quint8>(lenBytes[i])) << (i * 8);
        const QByteArray body = f.read(static_cast<qint64>(len));
        if (op == kOpSchema)
        {
            int so = 2;
            getString(body, so);  // name
            getString(body, so);  // encoding
            return getString(body, so).toUtf8();
        }
        // 消息都在 chunk 里，schema 一定在 chunk 之前 —— 见到 chunk 就不用再扫。
        if (op == kOpChunk || op == kOpFooter)
            break;
    }
    return QByteArray();
}

bool McapPlayer::open(const QString &path, QString *err)
{
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly))
    {
        if (err)
            *err = QStringLiteral("无法读取 MCAP 文件: %1").arg(path);
        return false;
    }
    const QByteArray head = m_file.read(8);
    if (!McapCodec::hasMagic(head))
    {
        if (err)
            *err = QStringLiteral("不是 MCAP 文件: %1").arg(path);
        m_file.close();
        return false;
    }
    m_channels.clear();
    m_sequences.clear();
    return true;
}

bool McapPlayer::readRecord(quint8 &op, QByteArray &body, QString *err)
{
    const QByteArray opByte = m_file.read(1);
    if (opByte.size() < 1)
        return false; // EOF
    op = static_cast<quint8>(opByte[0]);

    const QByteArray lenBytes = m_file.read(8);
    if (lenBytes.size() < 8)
    {
        if (err)
            *err = QStringLiteral("MCAP 记录长度截断");
        return false;
    }
    quint64 len = 0;
    for (int i = 7; i >= 0; --i)
        len = (len << 8) | static_cast<quint8>(lenBytes[i]);

    body = len ? m_file.read(static_cast<qint64>(len)) : QByteArray();
    if (static_cast<quint64>(body.size()) != len)
    {
        if (err)
            *err = QStringLiteral("MCAP 记录体截断");
        return false;
    }
    return true;
}

bool McapPlayer::nextMessage(McapMessage &out, QString *err)
{
    // 队列里还有（上一个 chunk 解析出的）消息，先吐出去
    if (!m_pending.isEmpty())
    {
        out = m_pending.takeFirst();
        return true;
    }
    while (true)
    {
        quint8 op = 0;
        QByteArray body;
        if (!readRecord(op, body, err))
            return false;

        switch (op)
        {
        case kOpChannel:
        {
            int off = 0;
            const quint16 cid = getU16(body, off);
            off += 2;
            off += 2; // schema_id
            const QString topic = getString(body, off);
            if (!topic.isEmpty() && !m_channels.contains(topic))
                m_channels.insert(topic, cid);
            break;
        }
        case kOpMessage:
        {
            int off = 0;
            out.channelId = getU16(body, off);
            off += 2;
            out.sequence = getU32(body, off);
            off += 4;
            out.logTimeNs = getU64(body, off);
            off += 8;
            out.publishTimeNs = getU64(body, off);
            off += 8;
            out.data = body.mid(off);
            // topic 反查
            out.topic.clear();
            for (auto it = m_channels.constBegin(); it != m_channels.constEnd(); ++it)
                if (it.value() == out.channelId)
                {
                    out.topic = it.key();
                    break;
                }
            return true;
        }
        case kOpChunk:
        {
            // Chunk body: start_time(8) end_time(8) uncompressed_size(8)
            //             uncompressed_crc(4) compression(string) records(剩余全部)
            int off = 0;
            off += 8; // start_time
            off += 8; // end_time
            off += 8; // uncompressed_size
            off += 4; // uncompressed_crc
            const quint32 compLen = getU32(body, off);
            off += 4 + static_cast<int>(compLen); // compression 字符串
            const QByteArray records = body.mid(off);
            // 解析 chunk 内的 record 序列：消息全部入队，由 nextMessage 逐条吐出
            // （chunk 是一次性 record，读完即消失，所以不能只取第一条）
            int p = 0;
            while (p + 9 <= records.size())
            {
                const quint8 rop = static_cast<quint8>(records[p]);
                quint64 rlen = 0;
                for (int i = 7; i >= 0; --i)
                    rlen = (rlen << 8) | static_cast<quint8>(records[p + 1 + i]);
                const int bodyOff = p + 9;
                if (bodyOff + static_cast<int>(rlen) > records.size())
                    break;
                if (rop == kOpMessage)
                {
                    McapMessage msg;
                    const QByteArray mb = records.mid(bodyOff, static_cast<int>(rlen));
                    int mo = 0;
                    msg.channelId = getU16(mb, mo);
                    mo += 2;
                    msg.sequence = getU32(mb, mo);
                    mo += 4;
                    msg.logTimeNs = getU64(mb, mo);
                    mo += 8;
                    msg.publishTimeNs = getU64(mb, mo);
                    mo += 8;
                    msg.data = mb.mid(mo);
                    for (auto it = m_channels.constBegin(); it != m_channels.constEnd(); ++it)
                        if (it.value() == msg.channelId)
                        {
                            msg.topic = it.key();
                            break;
                        }
                    m_pending.append(msg);
                }
                p = bodyOff + static_cast<int>(rlen);
            }
            // chunk 内的消息已入队：先吐一条，剩余的留给下次调用
            if (!m_pending.isEmpty())
            {
                out = m_pending.takeFirst();
                return true;
            }
            break;
        }
        case kOpSchema:
        {
            // Schema body: id(u16) + name(string) + encoding(string) + data(string)
            int so = 2;
            getString(body, so);  // name
            getString(body, so);  // encoding
            const QString data = getString(body, so);
            if (!data.isEmpty())
                m_schemaJson = data.toUtf8();
            break;
        }
        case kOpFooter:
            return false; // 读完
        default:
            break; // Header/Schema/DataEnd/其它：跳过
        }
    }
}

} // namespace nextpilot::plotscope::rec
