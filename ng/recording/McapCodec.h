#pragma once

#include "RecordingFormat.h"
#include "Sink.h"

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QString>

#include <memory>

namespace nextpilot::plotscope::rec
{

// MCAP 录制：app 官方内置的第二种录制格式。
//
// MCAP 是机器人领域（ROS 生态）的通用日志容器。相比 raw：
//   - 保留 channel 与 schema：可按 topic 选择性回放、按类型索引；
//   - chunk 索引：支持按时间随机定位（seek）；
//   - 消息体仍是原始字节 → 回放等价于原始。
//
// 协议实现参考老仓库（MCAP 规范本身也是公开格式）：
//   src/application/modules/datasource/services/live/record/McapWriter.{h,cpp}（写）
//   src/extensions/builtin/robotics/datasources/mcap/McapReader.{h,cpp}（读）
//
// 落盘结构：Header → Schema → Channel → [Chunk(Message…)] → DataEnd → Summary → Footer
class McapCodec
{
public:
    // MCAP 魔数：\x89MCAP0\r\n
    static QByteArray magic();
    static bool hasMagic(const QByteArray &head);
};

// 一条回放出来的消息。
struct McapMessage
{
    QString topic;       // 通道名
    quint16 channelId = 0;
    quint32 sequence = 0;
    quint64 logTimeNs = 0;
    quint64 publishTimeNs = 0;
    QByteArray data;     // 原始字节（消息体）
};

// MCAP 成帧器：写。
//
// 按 topic 分配 channel id；消息按 chunk 聚合（默认 1 MiB）以便索引与定位。
//
// 与 RawRecorder 同构：只管**成帧**（MCAP 记录结构），不管**送到哪**——
// 去向由 IByteSink 决定。于是 mcap→file 与 mcap→udp 共用同一套成帧逻辑。
class McapRecorder
{
public:
    McapRecorder() = default;
    ~McapRecorder();

    bool open(std::shared_ptr<IByteSink> sink,
              const QString &profile = QStringLiteral("plotscope"), QString *err = nullptr);

    // 写一条消息。topic 决定 channel（首次出现时创建）。
    bool writeMessage(const QString &topic, const QByteArray &data, quint64 logTimeNs,
                      QString *err = nullptr);

    bool close(QString *err = nullptr);

    bool isOpen() const { return m_sink != nullptr; }
    int channelCount() const { return m_channels.size(); }

private:
    void writeRecord(quint8 op, const QByteArray &body);
    void flushChunk();
    quint16 channelIdFor(const QString &topic, bool created);
    static void putU16(QByteArray &out, quint16 v);
    static void putU32(QByteArray &out, quint32 v);
    static void putU64(QByteArray &out, quint64 v);
    static void putString(QByteArray &out, const QString &s);

    std::shared_ptr<IByteSink> m_sink;
    QHash<QString, quint16> m_channels;   // topic -> channel id
    QHash<quint16, quint32> m_sequences;  // channel id -> 下一个 sequence
    QByteArray m_chunkRecords;            // 当前 chunk 的 record 缓冲
    quint64 m_chunkStartTime = 0;
    quint64 m_chunkEndTime = 0;
    bool m_chunkHasMessage = false;
    bool m_failed = false;
};

// MCAP 回放器：读。
class McapPlayer
{
public:
    McapPlayer() = default;
    ~McapPlayer();

    bool open(const QString &path, QString *err = nullptr);

    // 读下一条消息；返回 false 表示读完或出错。
    bool nextMessage(McapMessage &out, QString *err = nullptr);

    // 已登记的通道（topic -> channel id）。
    const QHash<QString, quint16> &channels() const { return m_channels; }
    bool atEnd() const { return m_file.atEnd(); }

private:
    bool readRecord(quint8 &op, QByteArray &body, QString *err);
    static quint16 getU16(const QByteArray &b, int off);
    static quint32 getU32(const QByteArray &b, int off);
    static quint64 getU64(const QByteArray &b, int off);
    static QString getString(const QByteArray &b, int &off);

    QFile m_file;
    QHash<QString, quint16> m_channels;
    QHash<quint16, quint32> m_sequences;
    QList<McapMessage> m_pending; // 已解析但尚未吐出的消息（一个 chunk 内有多条）
};

} // namespace nextpilot::plotscope::rec
