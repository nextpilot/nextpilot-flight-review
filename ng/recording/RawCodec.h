#pragma once

#include "RecordingFormat.h"
#include "Sink.h"

#include <QByteArray>
#include <QFile>
#include <QString>

#include <memory>

namespace nextpilot::plotscope::rec
{

// RAW 录制：app 内置的落盘格式之一。
//
// 落盘结构：
//   魔数 "PSRAW\0\0" (8B) | u32 version | u32 headerLen | header(JSON UTF-8) | 原始字节…
//
// 特性：
//   - 无损、零转码：录的是源字节，回放逐字节还原 → 结果与原始完全一致；
//   - 通用：任何数据源都能录，不需要各格式专门实现；
//   - 无通道结构：只适合单通道源（多通道源用 mcap）。
//
// 录制器（写）与播放器（读）成对存在，接口见 IRecorder / IPlayer。
class RawCodec
{
public:
    static const char *magic();
    static constexpr int kMagicSize = 8;
    static constexpr quint32 kVersion = 1;

    // 头部 JSON 的最大字节数（防御性上限）。
    static constexpr quint32 kMaxHeaderBytes = 64 * 1024;
};

// RAW 成帧器：把字节流成帧后写到**输出端**。
//
// 这是 **app 内置机制**（格式侧）：器官（mod.source）在用户点"开始录制"时创建它，
// 把数据源的字节旁路写进来。
//
// 关键：它只管**成帧**（加魔数与头），不管**送到哪**——
// 去向由 IByteSink 决定（文件 / UDP）。于是 raw→file 与 raw→udp 走同一条成帧逻辑，
// 这正是"格式 × 后端正交"的落点。
class RawRecorder
{
public:
    RawRecorder() = default;
    ~RawRecorder();

    // 打开输出端并写头部。headerJson 记录来源等元信息。
    // sink 由调用方持有（Recorder 组合 format × backend 时创建它）。
    bool open(std::shared_ptr<IByteSink> sink, const QByteArray &headerJson,
              QString *err = nullptr);

    // 追加原始字节。
    bool write(const uint8_t *data, qint64 size, QString *err = nullptr);
    bool write(const QByteArray &data, QString *err = nullptr);

    // 关闭并收尾（不关闭 sink——sink 归调用方）。
    bool close(QString *err = nullptr);

    bool isOpen() const { return m_sink != nullptr; }
    qint64 bytesWritten() const { return m_bytes; }

private:
    std::shared_ptr<IByteSink> m_sink;
    qint64 m_bytes = 0;
};

// 回放器：从录制文件读回原始字节。
class RawPlayer
{
public:
    RawPlayer() = default;
    ~RawPlayer();

    // 打开录制文件并读头部。
    bool open(const QString &path, QString *err = nullptr);

    // 读取下一块原始字节（最多 maxSize）。返回空表示已读完。
    QByteArray read(qint64 maxSize, QString *err = nullptr);

    // 头部 JSON（来源等元信息）。
    QByteArray headerJson() const { return m_header; }

    bool atEnd() const { return m_file.atEnd(); }
    qint64 totalBytes() const { return m_total; }

private:
    QFile m_file;
    QByteArray m_header;
    qint64 m_total = 0;
};

} // namespace nextpilot::plotscope::rec
