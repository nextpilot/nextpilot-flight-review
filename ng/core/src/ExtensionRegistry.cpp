#include "ExtensionRegistry.h"

#include <algorithm>

namespace nextpilot::plotscope::core
{

void ExtensionRegistry::rebuildIndexes()
{
    m_packIndex.clear();
    m_byId.clear();
    m_byKind.clear();
    m_packOrder.clear();
    m_idOrder.clear();

    for (const auto &pack : m_owned)
    {
        m_packIndex.insert(pack->id(), pack.get());
        m_packOrder.append(pack->id());
        for (const auto &c : pack->contributions())
        {
            if (!c)
                continue;
            m_byId.insert(c->id(), c.get());
            m_byKind[c->kind()].append(c.get());
            m_idOrder.append(c->id());
        }
    }
}

bool ExtensionRegistry::registerPack(std::unique_ptr<IToolPack> pack)
{
    if (!pack || pack->id().isEmpty() || m_packIndex.contains(pack->id()))
        return false;

    // 校验贡献 id 全局唯一，且 provider 与包一致。
    QSet<QString> seen;
    for (const auto &c : pack->contributions())
    {
        if (!c || c->id().isEmpty())
            return false;
        if (seen.contains(c->id()) || m_byId.contains(c->id()))
            return false;
        seen.insert(c->id());
    }
    for (const auto &c : pack->contributions())
        if (c)
            c->setProvider(pack->id());

    m_owned.push_back(std::move(pack));
    rebuildIndexes();
    return true;
}

bool ExtensionRegistry::unregisterPack(const QString &packId)
{
    auto it = std::find_if(m_owned.begin(), m_owned.end(),
                           [&packId](const std::unique_ptr<IToolPack> &p) {
                               return p && p->id() == packId;
                           });
    if (it == m_owned.end())
        return false;
    m_owned.erase(it); // 包连同其全部贡献一起释放 —— 原子撤销
    rebuildIndexes();
    return true;
}

QStringList ExtensionRegistry::packIds() const
{
    return m_packOrder;
}

IToolPack *ExtensionRegistry::pack(const QString &packId) const
{
    auto it = m_packIndex.constFind(packId);
    return it == m_packIndex.constEnd() ? nullptr : it.value();
}

int ExtensionRegistry::packCount() const
{
    return static_cast<int>(m_owned.size());
}

QList<IToolContribution *> ExtensionRegistry::contributions() const
{
    QList<IToolContribution *> out;
    out.reserve(m_idOrder.size());
    for (const QString &id : m_idOrder)
        if (auto it = m_byId.constFind(id); it != m_byId.constEnd())
            out.append(it.value());
    return out;
}

QList<IToolContribution *> ExtensionRegistry::contributions(const QString &kind) const
{
    return m_byKind.value(kind, {});
}

QList<IToolContribution *> ExtensionRegistry::contributions(const QString &kind,
                                                            Domain domain) const
{
    QList<IToolContribution *> out;
    for (IToolContribution *c : m_byKind.value(kind, {}))
        if (c->domain() == domain)
            out.append(c);
    return out;
}

QList<IToolContribution *> ExtensionRegistry::contributionsByDomain(Domain domain) const
{
    QList<IToolContribution *> out;
    for (const QString &id : m_idOrder)
    {
        IToolContribution *c = m_byId.value(id);
        if (c && c->domain() == domain)
            out.append(c);
    }
    return out;
}

IToolContribution *ExtensionRegistry::contribution(const QString &id) const
{
    auto it = m_byId.constFind(id);
    return it == m_byId.constEnd() ? nullptr : it.value();
}

QStringList ExtensionRegistry::kinds() const
{
    QStringList out;
    for (auto it = m_byKind.constBegin(); it != m_byKind.constEnd(); ++it)
        if (!it.value().isEmpty())
            out.append(it.key());
    return out;
}

QStringList ExtensionRegistry::contributionIds() const
{
    return m_idOrder;
}

bool ExtensionRegistry::containsContribution(const QString &id) const
{
    return m_byId.contains(id);
}

int ExtensionRegistry::contributionCount() const
{
    return static_cast<int>(m_byId.size());
}

namespace
{
bool producesFrames(const IMessageParser *p)
{
    return p && p->outputs().contains(ParseOutput::FrameBatch);
}
} // namespace

// === 统一解析器视图（所有 kind=message 的 IMessageParser）===

QList<IMessageParser *> ExtensionRegistry::parsers() const
{
    QList<IMessageParser *> out;
    for (IToolContribution *c : m_byKind.value(ContributionKind::Message, {}))
        if (auto *p = dynamic_cast<IMessageParser *>(c))
            out.append(p);
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
    IToolContribution *c = m_byId.value(id, nullptr);
    return (c && c->kind() == ContributionKind::Message)
               ? dynamic_cast<IMessageParser *>(c)
               : nullptr;
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
    auto pack = std::make_unique<ToolPack>(QStringLiteral("auto.") + driver->id(),
                                           driver->displayName(), driver->domain());
    pack->contributions().push_back(std::move(driver));
    return registerPack(std::move(pack));
}

bool ExtensionRegistry::unregisterDriver(const QString &id)
{
    IToolContribution *c = m_byId.value(id, nullptr);
    if (!c || c->kind() != ContributionKind::Message)
        return false;
    return unregisterPack(c->provider());
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
    return m_byId.contains(id);
}

int ExtensionRegistry::count() const
{
    return drivers().size();
}

} // namespace nextpilot::plotscope::core
