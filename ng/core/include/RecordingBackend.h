#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::core
{

// 录制后端：字节**送到哪里**。
//
// 与录制格式（RecordingFormat：字节**长什么样**）严格正交。
//   格式 = 容器结构：raw（裸字节流）/ mcap（带 channel 的消息容器）/ 插件自定义；
//   后端 = 投递方式：文件 / UDP 组播 / 插件自定义。
//
// 正交性带来的直接后果：**一个数据源可以同时开多路录制**——
//   raw→文件、mcap→文件、raw→UDP、mcap→UDP…… 各路互不干扰。
// 这不是特例代码，而是"格式 × 后端"两个独立注册表的自然组合结果。
//
// 归属层与格式相同：app 官方内置一套，插件可注册自己的。
// 但它**不是**知识包贡献（不含领域语义），走独立可选扩展点。
struct RecordingBackend
{
    QString id;            // "file" / "udp"
    QString displayName;   // "本地文件" / "UDP 组播"
    QString description;

    // 流式投递 = 允许丢数据（UDP 丢包即丢），且不保证顺序/到达。
    bool streaming = false;
    // 落盘后是否可随机定位（回放时按时间跳转）。
    bool seekable = false;
    // 是否为多路复用（同一端点上可区分不同录制流）——UDP 组播为真。
    bool multiplexed = false;
};

// 录制后端注册表：与 RecordingFormatRegistry 同构的进程内唯一表。
//
// 两条来源汇入这里：
//   1) 官方内置：file / udp，随 app 发布；
//   2) 插件扩展：插件定义自己的后端，注册进来即可选。
class RecordingBackendRegistry
{
public:
    // 登记一个后端（重复 id 先到者为准）。
    static bool registerBackend(const RecordingBackend &backend);

    // 撤销。
    static bool unregisterBackend(const QString &id);

    // 查询。
    static const RecordingBackend *backend(const QString &id);
    static QStringList ids();
    static int count();

    // 官方内置后端（file / udp）。由 app 启动期调用一次。
    static void installBuiltinBackends();

    // 清空（仅测试用）。
    static void reset();
};

namespace BackendId
{
inline const QString File = QStringLiteral("file");
inline const QString Udp = QStringLiteral("udp");
} // namespace BackendId

} // namespace nextpilot::plotscope::core
