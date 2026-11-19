#include "ParserRegistry.h"

#include <algorithm>

namespace nextpilot::plotscope::core
{

namespace
{
// 函数内 static：避免静态初始化顺序问题（解析器 TU 的静态对象可能先于本表构造）。
struct Registry
{
    QHash<QString, ParserEntry> entries;
    QStringList order;
    QHash<QString, std::shared_ptr<void>> handles;
};

Registry &registry()
{
    static Registry r;
    return r;
}
} // namespace

bool ParserRegistry::registerParser(const PsParserInfo &info, const PsParserVtable *vtable,
                                    const QString &provider)
{
    if (!info.id || !*info.id || !vtable)
        return false;
    if (vtable->abi_version != PS_PARSER_ABI_VERSION)
        return false;
    if (!vtable->create || !vtable->destroy || !vtable->feed)
        return false; // 必需槽位缺失

    Registry &r = registry();
    const QString id = QString::fromUtf8(info.id);
    if (r.entries.contains(id))
        return false; // 重复 id：先到者为准

    ParserEntry entry;
    entry.info = info;
    entry.vtable = vtable;
    entry.provider = provider;
    entry.isPlugin = false;
    r.entries.insert(id, entry);
    r.order.append(id);
    return true;
}

bool ParserRegistry::addPluginParser(const PsParserInfo &info, const PsParserVtable *vtable,
                                     std::shared_ptr<void> pluginHandle, const QString &provider)
{
    if (!info.id || !*info.id || !vtable)
        return false;
    if (vtable->abi_version != PS_PARSER_ABI_VERSION)
        return false;
    if (!vtable->create || !vtable->destroy || !vtable->feed)
        return false;

    Registry &r = registry();
    const QString id = QString::fromUtf8(info.id);
    if (r.entries.contains(id))
        return false;

    ParserEntry entry;
    entry.info = info;
    entry.vtable = vtable;
    entry.provider = provider;
    entry.isPlugin = true;
    r.entries.insert(id, entry);
    r.order.append(id);
    r.handles.insert(id, std::move(pluginHandle));
    return true;
}

bool ParserRegistry::unregisterParser(const QString &id)
{
    Registry &r = registry();
    if (!r.entries.contains(id))
        return false;
    r.entries.remove(id);
    r.order.removeAll(id);
    r.handles.remove(id); // 插件句柄随之释放
    return true;
}

const ParserEntry *ParserRegistry::entry(const QString &id)
{
    const auto it = registry().entries.constFind(id);
    return it == registry().entries.constEnd() ? nullptr : &it.value();
}

const PsParserVtable *ParserRegistry::vtable(const QString &id)
{
    const ParserEntry *e = entry(id);
    return e ? e->vtable : nullptr;
}

QStringList ParserRegistry::ids()
{
    return registry().order;
}

int ParserRegistry::count()
{
    return registry().entries.size();
}

const ParserEntry *ParserRegistry::findByExtension(const QString &suffix)
{
    if (suffix.isEmpty())
        return nullptr;
    QString key = suffix.toLower();
    if (!key.startsWith(QLatin1Char('.')))
        key.prepend(QLatin1Char('.'));

    // 精确匹配优先；同后缀多解析器时取先登记者。
    for (const QString &id : registry().order)
    {
        const ParserEntry &e = registry().entries[id];
        if (!e.info.extensions || !*e.info.extensions)
            continue;
        const QStringList exts = QString::fromUtf8(e.info.extensions)
                                     .split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &ext : exts)
            if (ext.trimmed().toLower() == key)
                return &e;
    }
    return nullptr;
}

const ParserEntry *ParserRegistry::findBySniff(const QByteArray &head)
{
    if (head.isEmpty())
        return nullptr;
    for (const QString &id : registry().order)
    {
        const ParserEntry &e = registry().entries[id];
        if (!e.info.can_sniff || !e.vtable->sniff)
            continue;
        // 用一个临时实例做探测：探测不应改变解析器状态。
        void *ctx = e.vtable->create();
        if (!ctx)
            continue;
        const int32_t ok = e.vtable->sniff(ctx, reinterpret_cast<const uint8_t *>(head.constData()),
                                           static_cast<uint32_t>(head.size()));
        e.vtable->destroy(ctx);
        if (ok > 0)
            return &e;
    }
    return nullptr;
}

QStringList ParserRegistry::unresolvedExtensions()
{
    QStringList out;
    for (const QString &id : registry().order)
    {
        const ParserEntry &e = registry().entries[id];
        if (!e.info.extensions || !*e.info.extensions)
            continue;
        const QStringList exts = QString::fromUtf8(e.info.extensions)
                                     .split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &ext : exts)
        {
            const QString key = ext.trimmed().toLower();
            if (!key.isEmpty() && !findByExtension(key))
                out.append(key);
        }
    }
    return out;
}

void ParserRegistry::reset()
{
    Registry &r = registry();
    r.entries.clear();
    r.order.clear();
    r.handles.clear();
}

} // namespace nextpilot::plotscope::core
