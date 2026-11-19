#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace nextpilot::plotscope::dataset
{

// 数据源种类。
enum class SourceKind
{
    File,   // 有限文件
    Stream, // 长连接流（TCP/UDP/串口）
    Live,   // 实时设备源
    Replay  // 录制文件回放
};

// 数据源状态。
enum class SourceState
{
    Opening,
    Streaming,
    Paused,
    Closed,
    Error
};

// 一个数据源里"一条字节通道"的解析绑定。
//
// 需求原文：「数据解析，一般只能选择一个前端（文件或流），但是可以选择多种
//   解析器（因为源，比如 udp 中可能包含多种格式数据），也就是将这份数据同时
//   喂给多个 parse，出的结果落库 dataset。」
//
// 因此绑定关系是**扇出**而非分流：
//   一个数据源（一个前端）→ N 个解析器，各自独立实例；
//   同一份字节流同时喂给这 N 个解析器（每个解析器自己 sniff/识别自己关心的部分）；
//   每个解析器产出的帧落到**它自己的数据集**（互不覆盖）。
//
// topic 只是**可选的过滤器**：某些解析器只关心某个通道时用它；
// 留空表示"接收全部字节"。它不是路由键——路由键是"喂给谁"。
struct ParserBinding
{
    QString parserId;   // 绑定的解析器 id（ParserRegistry 里的）
    QString topic;      // 可选通道过滤（空 = 接收全部）
    bool active = true;
    QByteArray options; // 解析参数 JSON（对话框收集，透传给 parser->configure）；
                        // 空 = 解析器用内置默认。宿主不解释，只透传。
};

// 一路录制的完整配置：**格式 × 后端**的正交组合。
//
// 格式（RecordingFormat）决定字节长什么样（容器结构：裸字节 / MCAP 消息容器）；
// 后端（RecordingBackend）决定字节送到哪（文件 / UDP 组播）。
//
// 正交性使"同时录制多种格式、多种后端"成为自然结果：
//   {raw, file, "/tmp/a.raw"} + {mcap, file, "/tmp/a.mcap"}
//   + {raw, udp, "239.0.0.1:5600"}
// 三路同时工作，互不干扰——不是特例代码，而是两个独立注册表的组合。
struct RecordingTarget
{
    QString formatId;    // raw / mcap / 插件注册的自定义格式
    QString backendId;   // file / udp  / 插件注册的自定义后端
    QString endpoint;    // 文件路径 或 "host:port"（按后端语义解释）
    bool enabled = true;

    bool isValid() const { return !formatId.isEmpty() && !backendId.isEmpty(); }
};

// 数据源的能力描述（由提供该源的扩展声明）。
struct DataSourceCapability
{
    QString kind;                       // "file" / "stream" / "live" / "replay"
    QString displayName;
    QStringList extensions;             // 认领的文件后缀（带点，小写）
    bool canRecord = false;             // 是否可录制
    bool canReplay = false;             // 是否可回放
    bool canSeek = false;               // 是否支持 seek
    QStringList recordFormats;          // 支持的录制格式 id（raw/mcap/...）
    QStringList recordBackends;         // 支持的录制后端 id（file/udp/...）
};

} // namespace nextpilot::plotscope::dataset
