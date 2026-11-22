#include "RecordingFormat.h"

namespace nextpilot::plotscope::rec
{

// 存储已收编 StaticHub<RecordingFormat>（机制唯一：Registry 原语 + 惰性表）。
// 本文件只剩真正的领域方法：后缀匹配、官方内置清单。

const RecordingFormat *RecordingFormatHub::formatByExtension(const QString &suffix)
{
    if (suffix.isEmpty())
        return nullptr;
    QString key = suffix.toLower();
    if (!key.startsWith(QLatin1Char('.')))
        key.prepend(QLatin1Char('.'));
    for (const QString &id : keys()) // 登记顺序
    {
        const RecordingFormat *f = find(id);
        if (f && f->extension.toLower() == key)
            return f;
    }
    return nullptr;
}

void RecordingFormatHub::installBuiltinFormats()
{
    // raw：原始字节，无损、通用、无通道结构
    RecordingFormat raw;
    raw.id = FormatId::Raw;
    raw.displayName = QStringLiteral("原始字节录制");
    raw.extension = QStringLiteral(".raw");
    raw.description = QStringLiteral("原始字节流，无损零转码；适合单通道数据源");
    raw.structured = false;
    raw.supportsSeek = false;
    registerFormat(raw);

    // mcap：带 schema/channel 的消息容器，可按 topic 选择性回放与按时间定位
    RecordingFormat mcap;
    mcap.id = FormatId::Mcap;
    mcap.displayName = QStringLiteral("MCAP 容器录制");
    mcap.extension = QStringLiteral(".mcap");
    mcap.description = QStringLiteral("带 schema/channel 的消息容器；可按 topic 选择性回放与按时间定位");
    mcap.structured = true;
    mcap.supportsSeek = true;
    registerFormat(mcap);
}

} // namespace nextpilot::plotscope::rec
