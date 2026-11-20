#include "ExtensionRegistry.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace nextpilot::plotscope::registry
{

namespace
{

// 内置包工厂静态表：函数内 static，避免静态初始化顺序问题；保序（登记顺序即装载顺序）。
struct BuiltinPackStore
{
    QList<QPair<QString, ExtensionRegistry::PackFactory>> ordered;
    QHash<QString, int> indexById;
};

BuiltinPackStore &builtinPacks()
{
    // [静态纪律·合法] 进程一份、无生命周期、自注册表（NG_REGISTER_PACK TU 工厂）。
    static BuiltinPackStore s;
    return s;
}

bool producesFrames(const IMessageParser *p)
{
    return p && p->outputs().contains(ParseOutput::FrameBatch);
}

} // namespace

bool ExtensionRegistry::registerBuiltinPack(const QString &id, PackFactory factory)
{
    if (id.isEmpty() || !factory)
        return false;
    BuiltinPackStore &r = builtinPacks();
    if (r.indexById.contains(id))
        return false; // 重复 id：先到者为准
    r.indexById.insert(id, r.ordered.size());
    r.ordered.append({id, std::move(factory)});
    return true;
}

QStringList ExtensionRegistry::installBuiltinPacks()
{
    QStringList installed;
    for (const auto &entry : builtinPacks().ordered)
    {
        auto pack = entry.second();
        if (!pack)
            continue;
        const QString pid = pack->id;
        if (mount(std::move(*pack)))
            installed.append(pid);
    }
    return installed;
}

void ExtensionRegistry::resetBuiltinPacks()
{
    BuiltinPackStore &r = builtinPacks();
    r.ordered.clear();
    r.indexById.clear();
}

bool ExtensionRegistry::mount(Pack pack)
{
    if (pack.id.isEmpty())
        return false;

    // 校验：包 id 与全部条目 id 全局唯一（先到者为准）。
    if (packs.contains(pack.id))
        return false;
    QSet<QString> seen;
    seen.insert(pack.id);
    const auto checkId = [&seen](const QString &id) {
        if (id.isEmpty() || seen.contains(id))
            return false;
        seen.insert(id);
        return true;
    };
    for (const UiAction &a : pack.uiActions)
        if (!checkId(a.id) || uiActions.contains(a.id))
            return false;
    for (const PanelSpec &p : pack.panels)
        if (!checkId(p.id) || panels.contains(p.id))
            return false;
    for (const ScriptSpec &s : pack.scripts)
        if (!checkId(s.id) || scripts.contains(s.id))
            return false;
    for (const DataAsset &d : pack.dataAssets)
        if (!checkId(d.id) || dataAssets.contains(d.id))
            return false;
    for (const DocEntry &d : pack.docs)
        if (!checkId(d.id) || docs.contains(d.id))
            return false;
    for (const ExampleEntry &e : pack.examples)
        if (!checkId(e.id) || examples.contains(e.id))
            return false;
    for (const auto &p : pack.parsers)
        if (!p || !checkId(p->id()) || parser(p->id()) != nullptr)
            return false;

    // 挂载：写归属字段，各列表分插进对应类型的表。
    const QString provider = pack.id;
    const Domain domain = pack.domain;
    const auto stamp = [provider, domain](auto &entry) {
        entry.provider = provider;
        entry.domain = domain;
    };
    for (UiAction &a : pack.uiActions)
    {
        stamp(a);
        uiActions.insert(a.id, a);
    }
    for (PanelSpec &p : pack.panels)
    {
        stamp(p);
        panels.insert(p.id, p);
    }
    for (ScriptSpec &s : pack.scripts)
    {
        stamp(s);
        scripts.insert(s.id, s);
    }
    for (DataAsset &d : pack.dataAssets)
    {
        stamp(d);
        dataAssets.insert(d.id, d);
    }
    for (DocEntry &d : pack.docs)
    {
        stamp(d);
        docs.insert(d.id, d);
    }
    for (ExampleEntry &e : pack.examples)
    {
        stamp(e);
        examples.insert(e.id, e);
    }
    for (auto &p : pack.parsers)
    {
        p->setProvider(provider);
        m_parsers.push_back(std::move(p));
    }
    packs.insert(pack.id, PackInfo{pack.id, pack.name, pack.domain});
    return true;
}

bool ExtensionRegistry::unmount(const QString &packId)
{
    if (!packs.contains(packId))
        return false;
    // 按包原子撤销：从各表移除 provider == packId 的全部条目，扩展之间互不牵连。
    const auto dropByProvider = [packId](auto &table) {
        for (const QString &id : table.keys())
        {
            const auto *e = table.find(id);
            if (e && e->provider == packId)
                table.remove(id);
        }
    };
    dropByProvider(uiActions);
    dropByProvider(panels);
    dropByProvider(scripts);
    dropByProvider(dataAssets);
    dropByProvider(docs);
    dropByProvider(examples);
    m_parsers.erase(std::remove_if(m_parsers.begin(), m_parsers.end(),
                                   [&packId](const auto &p) {
                                       return p && p->provider() == packId;
                                   }),
                    m_parsers.end());
    packs.remove(packId);
    return true;
}

// === 解析器视图（行为式条目）===

QList<IMessageParser *> ExtensionRegistry::parsers() const
{
    QList<IMessageParser *> out;
    out.reserve(static_cast<int>(m_parsers.size()));
    for (const auto &p : m_parsers)
        out.append(p.get());
    return out;
}

QList<IMessageParser *> ExtensionRegistry::parsersByDomain(Domain domain) const
{
    QList<IMessageParser *> out;
    for (IMessageParser *p : parsers())
        if (p->domain() == domain)
            out.append(p);
    return out;
}

IMessageParser *ExtensionRegistry::parser(const QString &id) const
{
    for (const auto &p : m_parsers)
        if (p && p->id() == id)
            return p.get();
    return nullptr;
}

IMessageParser *ExtensionRegistry::parserForProtocol(const QString &protocolId) const
{
    const QString key = protocolId.trimmed().toLower();
    for (IMessageParser *p : parsers())
        if (p->id().toLower() == key || p->protocolName().toLower() == key)
            return p;
    return nullptr;
}

QList<IMessageParser *> ExtensionRegistry::parsersByTransport(const QString &transport) const
{
    QList<IMessageParser *> out;
    for (IMessageParser *p : parsers())
        if (p->supportedTransports().contains(transport, Qt::CaseInsensitive))
            out.append(p);
    return out;
}

// === 旧 API 兼容别名：筛 outputs() 含 FrameBatch 的解析器 ===

bool ExtensionRegistry::registerDriver(std::unique_ptr<IMessageParser> driver)
{
    if (!driver)
        return false;
    Pack pack;
    pack.id = QStringLiteral("auto.") + driver->id();
    pack.name = driver->displayName();
    pack.domain = driver->domain();
    pack.parsers.push_back(std::move(driver));
    return mount(std::move(pack));
}

bool ExtensionRegistry::unregisterDriver(const QString &id)
{
    IMessageParser *p = parser(id);
    return p ? unmount(p->provider()) : false;
}

IMessageParser *ExtensionRegistry::driverForFormat(const QString &format) const
{
    if (format.isEmpty())
        return nullptr;
    for (IMessageParser *p : parsers())
        if (producesFrames(p) && p->acceptsFormat(format))
            return p;
    return nullptr;
}

IMessageParser *ExtensionRegistry::driverForPath(const QString &path) const
{
    for (IMessageParser *p : parsers())
        if (producesFrames(p) && p->acceptsSuffix(path))
            return p;
    return nullptr;
}

QList<IMessageParser *> ExtensionRegistry::drivers() const
{
    QList<IMessageParser *> out;
    for (IMessageParser *p : parsers())
        if (producesFrames(p))
            out.append(p);
    return out;
}

QList<IMessageParser *> ExtensionRegistry::driversByDomain(Domain domain) const
{
    QList<IMessageParser *> out;
    for (IMessageParser *p : parsers())
        if (producesFrames(p) && p->domain() == domain)
            out.append(p);
    return out;
}

QList<Domain> ExtensionRegistry::domains() const
{
    QList<Domain> out;
    for (IMessageParser *p : drivers())
        if (!out.contains(p->domain()))
            out.append(p->domain());
    return out;
}

QStringList ExtensionRegistry::ids() const
{
    QStringList out;
    for (IMessageParser *p : drivers())
        out.append(p->id());
    return out;
}

bool ExtensionRegistry::contains(const QString &id) const
{
    return parser(id) != nullptr;
}

int ExtensionRegistry::count() const
{
    return drivers().size();
}

} // namespace nextpilot::plotscope::registry
