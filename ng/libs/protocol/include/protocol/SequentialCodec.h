#pragma once

#include "protocol/ProtocolDocument.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

namespace nextpilot::plotscope::protocol
{

// 顺序帧协议（layout:"sequential"）解码器——老 CustomProtocolLoader 的解码内核
// 平移到 ng 的产物中立形态。
//
// 架构边界（见 ARCHITECTURE §3.7.1）：
//   - 协议知识（段/变体/校验/表达式/时间戳）全在这里，是可单测的纯逻辑；
//   - **不认识 Dataset / Rust / PsHostApi**——产出一中立的 channel→(t,v) 结构；
//   - 谁消费这结构由上层决定：SPI 解析器把它转成 PsFrameBatch 吐给摄取管道，
//     编辑器试算把它转成可读文本行。两种消费者共用同一份解码逻辑。

// 一条解码出的通道（对标老 PsChannelData，但去掉 dataset/topic/共享轴等宿主概念）。
struct ProtocolChannel
{
    std::string name;
    std::string unit;
    std::vector<double> t; // 秒
    std::vector<double> v;
};

struct ProtocolDecodeResult
{
    bool ok = false;
    QString error;
    QVector<ProtocolChannel> channels;
    quint64 rejectedFrames = 0;
    quint64 warnings = 0;
};

// 这段 JSON 是不是顺序段协议模板（自检/probe 用）。
bool looksSequentialProtocol(const QByteArray &json);

// 编辑器试算：用与生产解码相同的段/变体/校验逻辑解码单帧，返回可读文本行。
struct SequentialFramePreview
{
    bool ok = false;
    QString error;
    QStringList lines;
};
SequentialFramePreview previewSequentialFrame(const QByteArray &frame,
                                              const ProtocolDocument &doc);

// 生产解码：字节流 + 已解析的协议文档 → 多通道 (t,v)。
// 这是 SPI 解析器的内核：全流一次喂入（流式分片由上层缓冲后调用）。
ProtocolDecodeResult decodeSequential(const QByteArray &bytes, const ProtocolDocument &doc);

// 便捷：直接吃模板 JSON（内部做 validate + parseDocument）。
ProtocolDecodeResult decodeSequentialJson(const QByteArray &bytes, const QByteArray &templateJson);

} // namespace nextpilot::plotscope::protocol
