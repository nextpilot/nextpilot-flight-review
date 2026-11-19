#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::rec
{

// 录制格式描述。
//
// 架构定位（重要）：录制格式与**解析格式同构**——都是"可选的能力实现"，
// 官方默认提供一套，插件可定义自己的、注册进来即可选。
//
//   需求原文：「其实就是配置是否需要录制，以及录制格式是什么。官方默认提供了
//     raw 和 mcap，插件可以定义其他的格式，注册过来就可以选。跟解析格式一样的，
//     工具箱提供了很多解析器，注册进来就可以选。」
//
// 但两者**归属层不同**，这个区别很重要：
//   - 解析器（Parser）是**领域知识**：飞控的 ULog、汽车的 DBC……由知识包（extension）贡献；
//   - 录制格式是**app 的机制选项**：raw/mcap 是通用容器，不含任何领域语义，
//     由 app 官方提供；插件可扩展自己的格式，但这走的是**独立的可选扩展点**，
//     与"知识包贡献"不是同一套东西。
//
// 一份录制文件包含三样东西：
//   ① 字节（消息体）  ② 通道/topic 归属  ③ 时间戳
// raw 与 mcap 的差别在②③如何编码；①都是原始字节——所以**回放等价于原始**。
struct RecordingFormat
{
    QString id;            // "raw" / "mcap"
    QString displayName;   // "原始字节录制" / "MCAP 容器录制"
    QString extension;     // ".raw" / ".mcap"
    QString description;
    bool structured = false;  // 是否保留通道结构（mcap=true）
    bool supportsSeek = false; // 是否支持按时间随机定位
};

// 录制格式注册表：进程内唯一，与 ParserRegistry 同构。
//
// 两条来源汇入这里：
//   1) 官方内置：raw / mcap，app 启动期登记；
//   2) 插件扩展：插件定义自己的格式，注册进来即可选。
//
// 注意它**不是**贡献点注册表（extension 体系）：录制格式不含领域知识，
// 官方内置的两种随 app 一起发布，插件扩展走独立钩子。
class RecordingFormatRegistry
{
public:
    // 登记一个格式（重复 id 先到者为准）。
    static bool registerFormat(const RecordingFormat &format);

    // 撤销。
    static bool unregisterFormat(const QString &id);

    // 查询。
    static const RecordingFormat *format(const QString &id);
    static const RecordingFormat *formatByExtension(const QString &suffix);
    static QStringList ids();
    static int count();

    // 官方内置格式（raw / mcap）。由 app 启动期调用一次。
    static void installBuiltinFormats();

    // 清空（仅测试用）。
    static void reset();
};

// 注册宏：录制格式实现 TU 末尾一行。
//   NG_REGISTER_RECORDING_FORMAT(RawRecordingFormat)
#define NG_REGISTER_RECORDING_FORMAT(Cls)                                               \
    namespace {                                                                        \
    const bool g_ng_recfmt_reg_ =                                                      \
        ::nextpilot::plotscope::rec::RecordingFormatRegistry::registerFormat(Cls::recordingFormat());   \
    }

namespace FormatId
{
inline const QString Raw = QStringLiteral("raw");
inline const QString Mcap = QStringLiteral("mcap");
} // namespace FormatId

} // namespace nextpilot::plotscope::rec
