#pragma once

#include "protocol/ProtocolDocument.h"
#include "protocol/TypedVec.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <string>
#include <vector>

namespace nextpilot::plotscope::protocol
{

// 顺序帧协议（layout:"sequential"）解码器——老 CustomProtocolLoader 的解码内核
// 中立形态（不绑定具体数据层）。
//
// 架构边界（见 ARCHITECTURE §3.7.1）：
//   - 协议知识（段/变体/校验/表达式/时间戳）全在这里，是可单测的纯逻辑；
//   - **不认识 Dataset / Rust / PsHostApi**——产出一中立的 channel→(t,v) 结构；
//   - 谁消费这结构由上层决定：SPI 解析器把它转成 PsFrameBatch 吐给摄取管道，
//     编辑器试算把它转成可读文本行。两种消费者共用同一份解码逻辑。

// 一条解码出的通道（对标老 PsChannelData，但去掉 dataset/topic/共享轴等宿主概念）。
// 值列带原生类型：段是 u16 就出 u16，不在这里统一降成 double。
// 时间为整数 ticks（ticksPerSecond 由结果给出），避免浮点时间累积误差。
struct ProtocolChannel
{
    std::string name;
    std::string unit;
    std::vector<int64_t> t; // ticks
    TypedVec v;             // 原生类型值列
};

struct CompiledProgram;

// 解码错误分类（SPEC §8）。只有配置类在加载期致命；其余是运行期事件，流继续。
struct DecodeError
{
    enum Code
    {
        None = 0,
        ConfigInvalid,     // 模板非法（加载期，不进流）
        OverflowMaxsize,   // 候选超过 maxsize 仍无终止符
        SyncMissing,       // 该出现的字面量没出现（如 * 缺失 / 声明的 term 缺失）
        RecordAborted,     // 候选未完成时又出现了帧首字面量（等 * 时来了 $）
        ChecksumMismatch,  // 摘要与 check 值不符
        CheckNotHex,       // check 字段不是合法十六进制
        FieldInvalid,      // 字段无法按声明转换（数值/进制/小数位）
        TooFewFields,      // 字段数少于模板
        TooManyFields,     // 字段数多于模板
        PatternMismatch,   // pattern 校验不通过
        ScriptTimeout,     // 脚本指令数超限
        IncompleteAtEof    // finish 时仍未闭合
    };
    Code code = None;
    quint64 recordOffset = 0; // 该记录在流里的绝对偏移
    std::string segment;      // 出问题的段（配置类为空）
    std::string detail;       // 期望/实际
    int64_t recordIndex = -1; // 帧号（第几条记录，0 基；未知为 -1）
    bool warning = false;     // true=该条只是告警（on_mismatch:warn/skip），数据未整体作废
};

struct ProtocolDecodeResult
{
    bool ok = false;
    QString error;
    QVector<ProtocolChannel> channels;
    int64_t ticksPerSecond = 1000000;
    quint64 rejectedFrames = 0;
    quint64 warnings = 0;
    // 错误明细：只留前 128 条（避免坏数据吃内存），其余仅计数。
    std::vector<DecodeError> errors;
    quint64 dropped = 0;   // 半帧/被新帧首打断
    quint64 overflow = 0;  // 超 maxsize
    quint64 skipped = 0;   // 定位时丢弃的前导字节
};

// 流式解码器（SPEC §7.3）：push 增量、NeedMore 保留尾巴、finish 收尾。
// 这是**唯一**的解析路径；decodeSequential 只是"一次喂完"的薄封装。
class StreamDecoder
{
public:
    explicit StreamDecoder(const CompiledProgram &prog);
    ~StreamDecoder();
    StreamDecoder(const StreamDecoder &) = delete;
    StreamDecoder &operator=(const StreamDecoder &) = delete;

    /// 喂一块数据，把已经完整的记录追加到 out。
    /// 返回 false 只在配置/致命错误时（此时 out.error 有原因）。
    bool push(const uint8_t *data, size_t len, ProtocolDecodeResult &out, std::string &err);
    bool push(const QByteArray &chunk, ProtocolDecodeResult &out, std::string &err);
    /// 输入结束：未闭合的候选按 IncompleteAtEof 计数（不产出）。
    void finish(ProtocolDecodeResult &out);

    quint64 dropped() const;
    quint64 rejected() const;
    quint64 overflow() const;

private:
    const CompiledProgram &m_prog;
    std::vector<uint8_t> m_pending;
    quint64 m_streamOffset = 0;
    quint64 m_dropped = 0;
    quint64 m_rejected = 0;
    quint64 m_overflow = 0;
    int64_t m_recordIndex = 0;
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

// 已编译程序的解码入口：模板只编译一次，之后反复解码（批量/流式都走这里）。
// CompiledProgram 由 compileProgram() 产生，见 protocol/CompiledProgram.h。
struct CompiledProgram;
ProtocolDecodeResult decodeSequentialCompiled(const QByteArray &bytes,
                                              const CompiledProgram &program);

// 便捷：直接吃模板 JSON（内部做 validate + parseDocument）。
ProtocolDecodeResult decodeSequentialJson(const QByteArray &bytes, const QByteArray &templateJson);

} // namespace nextpilot::plotscope::protocol
