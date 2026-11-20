#include "ParserRegistry.h"

#include "Registry.h"

#include <algorithm>

namespace nextpilot::plotscope::parser
{

namespace
{
// 函数内 static：避免静态初始化顺序问题（解析器 TU 的静态对象可能先于本表构造）。
// 容器本体委托泛型注册表（机制唯一，保序：登记顺序即 ids() 顺序，order 列表不再自备）。
// 插件句柄与条目同 id 存放，unregister/reset 随条目一起释放。
using nextpilot::plotscope::registry::Registry;
struct ParserStore
{
    Registry<ParserEntry> entries;
    Registry<std::shared_ptr<void>> handles;
};

ParserStore &store()
{
    static ParserStore s;
    return s;
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

    const QString id = QString::fromUtf8(info.id);
    ParserEntry entry;
    entry.info = info;
    entry.vtable = vtable;
    entry.provider = provider;
    entry.isPlugin = false;
    return store().entries.insert(id, std::move(entry)); // 重复 id：先到者为准
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

    const QString id = QString::fromUtf8(info.id);
    ParserEntry entry;
    entry.info = info;
    entry.vtable = vtable;
    entry.provider = provider;
    entry.isPlugin = true;
    if (!store().entries.insert(id, std::move(entry)))
        return false;
    store().handles.insert(id, std::move(pluginHandle));
    return true;
}

bool ParserRegistry::unregisterParser(const QString &id)
{
    if (!store().entries.remove(id))
        return false;
    store().handles.remove(id); // 插件句柄随之释放
    return true;
}

const ParserEntry *ParserRegistry::entry(const QString &id)
{
    return store().entries.find(id);
}

const PsParserVtable *ParserRegistry::vtable(const QString &id)
{
    const ParserEntry *e = entry(id);
    return e ? e->vtable : nullptr;
}

QStringList ParserRegistry::ids()
{
    return store().entries.keys();
}

int ParserRegistry::count()
{
    return store().entries.count();
}

const ParserEntry *ParserRegistry::findByExtension(const QString &suffix)
{
    if (suffix.isEmpty())
        return nullptr;
    QString key = suffix.toLower();
    if (!key.startsWith(QLatin1Char('.')))
        key.prepend(QLatin1Char('.'));

    // 精确匹配优先；同后缀多解析器时取先登记者（values() 保序）。
    for (const ParserEntry &e : store().entries.values())
    {
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
    for (const ParserEntry &e : store().entries.values())
    {
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
    for (const ParserEntry &e : store().entries.values())
    {
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
    store().entries.clear();
    store().handles.clear();
}

} // namespace nextpilot::plotscope::parser
