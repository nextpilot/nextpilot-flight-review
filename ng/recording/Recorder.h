#pragma once

#include "RecordingFormat.h"
#include "Result.h"
#include "Sink.h"

#include <QByteArray>
#include <QString>

#include <memory>

namespace ng::rec
{

class RawRecorder;
class McapRecorder;

// 一路录制：**格式 × 后端**的组合体。
//
// 这是需求「数据录制，可以同时录制多种格式（raw，mcap）、多种后端（文件,udp）」
// 的直接落点：多路录制 = 多个 Recorder 实例，各自带自己的格式与后端。
//
// 组合关系：
//
//   Recorder
//     ├── 成帧器（格式侧）：RawRecorder / McapRecorder —— 决定字节长什么样
//     └── 输出端（后端侧）：FileSink / UdpSink        —— 决定字节送到哪
//
// 两侧独立注册（RecordingFormatRegistry / RecordingBackendRegistry），
// 组合时按 id 查表创建——新增格式或后端都不需要改本类。
class Recorder
{
public:
    Recorder() = default;
    ~Recorder();

    // 按 (格式, 后端) 组合创建。
    // 未知格式或后端返回 false，并在 err 里说明缺哪一个。
    static ng::core::Result<std::shared_ptr<Recorder>> create(const QString &formatId,
                                                    const QString &backendId,
                                                    const QString &endpoint,
                                                    const QByteArray &headerJson = QByteArray(),
                                                    QString *err = nullptr);

    // 写入一条消息（带通道名与时间戳）。
    // raw 格式忽略通道与时间（它只保真字节流）；mcap 保留。
    bool writeMessage(const QString &topic, const QByteArray &payload, quint64 timeNs,
                      QString *err = nullptr);

    // 写入原始字节（仅 raw 格式有意义）。
    bool writeBytes(const QByteArray &data, QString *err = nullptr);

    // 收尾（冲刷 + 写尾记录）。
    bool close(QString *err = nullptr);

    bool isOpen() const;
    qint64 bytesWritten() const { return m_bytes; }
    QString formatId() const { return m_formatId; }
    QString backendId() const { return m_backendId; }

private:
    QString m_formatId;
    QString m_backendId;
    std::shared_ptr<IByteSink> m_sink;
    std::shared_ptr<RawRecorder> m_raw;
    std::shared_ptr<McapRecorder> m_mcap;
    qint64 m_bytes = 0;
};

} // namespace ng::rec
