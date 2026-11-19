#pragma once

/*
 * 协议消息解析器接口（"协议检查器"能力）。
 *
 * 与 ps_parser_spi.h 的关系——两套东西，刻意分开：
 *   - SPI parser：字节 → PsFrameBatch（数值批）→ Rust Dataset，供**画图/分析**，纯 C ABI；
 *   - IMessageParser：字节 → DecodedFrame（可读字段树）→ 协议检查器/打开对话框，C++ 接口。
 *
 * 为什么后者是 C++ 虚接口而不进 SPI：
 *   反例——若把"协议名/使用范围/transfer/配置 schema"塞进 SPI 的 C vtable，
 *   SPI 立刻从"不认识宿主的纯字节转换"退化成"带 GUI 配置语义的东西"，
 *   跨语言 / dll 实现全废。协议检查器天然带 Qt 数据（QVariant 字段），本就是 C++ 侧能力。
 *
 * === 自述（供专用协议对话框消费，加协议不动对话框代码）===
 *   protocolName()        协议可读名
 *   domain()              使用范围-业务领域（继承 IToolContribution）
 *   usage()               使用范围-人读用途说明
 *   supportedTransports() 支持的 transfer 协议 id（file/udp/tcp/serial/can...）
 *   configSchema()        可配置参数 schema（与 IFormatDriver::importSchema 同构，JSON 键值对描述）
 *
 * === 生命周期 ===
 *   create() → configure(params) → parse(bytes) → ... → destroy()
 *   configure 必须且只在 create 后、首次 parse 前调用一次。
 *
 * === 配置纪律（硬约束）===
 *   解析器**只从 configure 传入的 params 取参数**，且只取自己 configSchema() 声明的键。
 *   禁止读全局（SettingsStore / QSettings / 环境变量 / 文件）——
 *   这样协议可独立单测、配置可存 Workspace 重放、同一类的两个实例互不串味。
 *   基类提供 param(key, default) 助手，默认实现把 params 存进 m_params；
 *   派生类可从 m_params 读，但不得从别处读。
 */

#include "ContributionKind.h"
#include "DecodedFrame.h"
#include "IToolContribution.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::core
{

class IMessageParser : public IToolContribution
{
public:
    ~IMessageParser() override = default;

    // 贡献类型固定为 message（kind 与 intent 分离，这里只做身份声明）。
    QString kind() const final { return ContributionKind::Message; }

    // === 自述：协议名称 ===
    // 协议的规范名（如 "NMEA 0183" / "MAVLink 2"），与 id 区分：
    // id 是稳定标识（配置/重放用），protocolName 是对人展示的协议名。
    // 基类默认返回 displayName()，需要更精确协议名的派生类可覆盖。
    virtual QString protocolName() const { return displayName(); }

    // === 自述：使用范围 ===
    virtual QString usage() const = 0;                    // 一句话用途说明
    virtual QStringList supportedTransports() const = 0; // transfer 协议 id 列表

    // === 自述：可配置参数 schema ===
    // 形如 {"title":..., "fields":[{key,type,label,default,options?,visible_if?...}], "ui":{...}}
    // type ∈ bool/int/double/enum/string/file。空对象 = 无参数。
    // 与 IFormatDriver::importSchema 同构，对话框渲染器可共用同一套约定。
    virtual QJsonObject configSchema() const { return QJsonObject(); }

    // === 生命周期 ===
    // 把宿主收集的参数灌进本实例。默认实现存进 m_params，派生类一般无需 override——
    // 直接在 parse() 里用 param(key) 取即可。需要派生处理时 override 并调用基类实现。
    // 返回 false 表示参数非法（err 写原因），宿主应中止本次加载。
    virtual bool configure(const QJsonObject &params, QString *err = nullptr);

    // 只从本次 configure 传入的参数取值（支持 "time.mode" 点分嵌套）。
    // 这是解析器的**唯一**配置来源：拿不到就返回 default，绝不回退到全局。
    QJsonValue param(const QString &key, const QJsonValue &fallback = QJsonValue()) const;

    // 探测：给一段头字节，判断是否本协议。宿主据此在对话框里预选协议。
    virtual bool sniff(const QByteArray &head) const
    {
        Q_UNUSED(head);
        return false;
    }

    // === 核心行为：每个具体类主要实现这个函数 ===
    // 把一段字节解码成一帧。成功返回 true 并填 out；失败返回 false。
    virtual bool parse(const QByteArray &bytes, DecodedFrame &out) const = 0;

protected:
    QJsonObject m_params; // configure 传入的参数快照；派生类只读，不写全局
};

// === transfer 协议 id（开放字符串，与 ContributionKind 同风格）===
// 表示"字节从哪来 / 怎么传"，供 supportedTransports() 声明、对话框渲染传输选项。
// 与 IByteSource（读取端）和 RecordingBackend（投递端）的 id 对齐复用。
namespace MessageTransport
{
inline const QString File = QStringLiteral("file");     // 本地文件
inline const QString Memory = QStringLiteral("memory"); // 内存/回放片段
inline const QString Udp = QStringLiteral("udp");       // UDP 组播/单播
inline const QString Tcp = QStringLiteral("tcp");       // TCP 流
inline const QString Serial = QStringLiteral("serial"); // 串口
inline const QString Can = QStringLiteral("can");       // CAN 总线
} // namespace MessageTransport

} // namespace nextpilot::plotscope::core
