#pragma once

/*
 * 协议解码帧——IMessageParser::parse() 的输出。
 *
 * 它与 SPI parser 的 PsFrameBatch 刻意分开：
 *   - PsFrameBatch 是**数值批**（列名 + typed 列值 + 时间戳），进 Rust Dataset 画图；
 *   - DecodedFrame 是**人类可读的单帧解码**（字段树 + 枚举标签 + 校验状态），
 *     给协议检查器 / 打开对话框的预览用。
 * 两者服务不同用途，互不替代，也不互相转换。
 *
 * 纯数据（Qt6::Core），无 QWidget、无回调，headless / CLI 可用、可测试。
 */

#include <QByteArray>
#include <QString>
#include <QVariant>
#include <QVector>

namespace nextpilot::plotscope::parser
{

// 一个解码字段：raw 是线上的原值，value 是缩放/枚举后的可读值。
struct DecodedField
{
    QString name;
    QVariant raw;
    QVariant value;
    QString unit;
    QString description;
    QString enumName;
    QString enumLabel;
    int offset = -1; // 在线上帧中的字节偏移，-1 表示不适用
    int size = 0;    // 占用字节数
};

// 一帧解码结果。
struct DecodedFrame
{
    QString protocol;   // 协议 id，如 "nmea" / "mavlink"
    QString frameName;  // 帧名，如 "GGA" / "HEARTBEAT"
    QString frameKey;   // 帧稳定键，如 "$GPGGA" / msgid
    double timestamp = 0.0;
    QByteArray rawBytes;
    QVector<DecodedField> fields;
    bool checksumOk = true;
    bool unknown = false; // 帧 id 已识别但字段模板缺失
};

} // namespace nextpilot::plotscope::parser
// B7 过渡别名：core 消亡前全仓仍以 core::Xxx 取用。簇 8 删 core 时移除。
namespace nextpilot::plotscope::core
{
using parser::DecodedField;
using parser::DecodedFrame;
} // namespace nextpilot::plotscope::core

