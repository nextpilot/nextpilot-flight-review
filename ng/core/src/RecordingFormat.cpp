#include "RecordingFormat.h"

#include <QHash>

namespace ng::core
{

namespace
{
// 函数内 static：避免静态初始化顺序问题。
struct Registry
{
    QHash<QString, RecordingFormat> formats;
    QStringList order;
};

Registry &registry()
{
    static Registry r;
    return r;
}
} // namespace

bool RecordingFormatRegistry::registerFormat(const RecordingFormat &format)
{
    if (format.id.isEmpty())
        return false;
    Registry &r = registry();
    if (r.formats.contains(format.id))
        return false; // 重复 id：先到者为准
    r.formats.insert(format.id, format);
    r.order.append(format.id);
    return true;
}

bool RecordingFormatRegistry::unregisterFormat(const QString &id)
{
    Registry &r = registry();
    if (!r.formats.contains(id))
        return false;
    r.formats.remove(id);
    r.order.removeAll(id);
    return true;
}

const RecordingFormat *RecordingFormatRegistry::format(const QString &id)
{
    const auto it = registry().formats.constFind(id);
    return it == registry().formats.constEnd() ? nullptr : &it.value();
}

const RecordingFormat *RecordingFormatRegistry::formatByExtension(const QString &suffix)
{
    if (suffix.isEmpty())
        return nullptr;
    QString key = suffix.toLower();
    if (!key.startsWith(QLatin1Char('.')))
        key.prepend(QLatin1Char('.'));
    for (const QString &id : registry().order)
    {
        const RecordingFormat &f = registry().formats[id];
        if (f.extension.toLower() == key)
            return &f;
    }
    return nullptr;
}

QStringList RecordingFormatRegistry::ids()
{
    return registry().order;
}

int RecordingFormatRegistry::count()
{
    return registry().formats.size();
}

void RecordingFormatRegistry::installBuiltinFormats()
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

void RecordingFormatRegistry::reset()
{
    Registry &r = registry();
    r.formats.clear();
    r.order.clear();
}

} // namespace ng::core
