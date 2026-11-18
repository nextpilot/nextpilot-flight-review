#include "ExtensionRegistry.h"

#include <algorithm>

namespace ng::core
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
                                                            FormatDomain domain) const
{
    QList<IToolContribution *> out;
    for (IToolContribution *c : m_byKind.value(kind, {}))
        if (c->domain() == domain)
            out.append(c);
    return out;
}

QList<IToolContribution *> ExtensionRegistry::contributionsByDomain(FormatDomain domain) const
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

// === 格式快捷 API ===

bool ExtensionRegistry::registerDriver(std::unique_ptr<IFormatDriver> driver)
{
    if (!driver)
        return false;
    // 单个驱动包装成一个隐式包，复用统一的包登记路径。
    auto pack = std::make_unique<ToolPack>(QStringLiteral("auto.") + driver->id(),
                                           driver->displayName(), driver->domain());
    pack->contributions().push_back(std::move(driver));
    return registerPack(std::move(pack));
}

bool ExtensionRegistry::unregisterDriver(const QString &id)
{
    IToolContribution *c = m_byId.value(id, nullptr);
    if (!c || c->kind() != ContributionKind::Format)
        return false;
    // 找到该贡献所属的包并整体撤销（隐式包只含这一个驱动；显式包的格式驱动
    // 也通过 provider 归属，撤销的是它所在包——语义与"按包装卸"一致）。
    return unregisterPack(c->provider());
}

IFormatDriver *ExtensionRegistry::driverForFormat(const QString &format) const
{
    if (format.isEmpty())
        return nullptr;
    for (IToolContribution *c : m_byKind.value(ContributionKind::Format, {}))
    {
        auto *d = dynamic_cast<IFormatDriver *>(c);
        if (d && d->acceptsFormat(format))
            return d;
    }
    return nullptr;
}

IFormatDriver *ExtensionRegistry::driverForPath(const QString &path) const
{
    for (IToolContribution *c : m_byKind.value(ContributionKind::Format, {}))
    {
        auto *d = dynamic_cast<IFormatDriver *>(c);
        if (d && d->acceptsSuffix(path))
            return d;
    }
    return nullptr;
}

QList<IFormatDriver *> ExtensionRegistry::drivers() const
{
    QList<IFormatDriver *> out;
    for (IToolContribution *c : m_byKind.value(ContributionKind::Format, {}))
        if (auto *d = dynamic_cast<IFormatDriver *>(c))
            out.append(d);
    return out;
}

QList<IFormatDriver *> ExtensionRegistry::driversByDomain(FormatDomain domain) const
{
    QList<IFormatDriver *> out;
    for (IToolContribution *c : m_byKind.value(ContributionKind::Format, {}))
    {
        auto *d = dynamic_cast<IFormatDriver *>(c);
        if (d && d->domain() == domain)
            out.append(d);
    }
    return out;
}

QList<FormatDomain> ExtensionRegistry::domains() const
{
    QList<FormatDomain> out;
    for (IToolContribution *c : m_byKind.value(ContributionKind::Format, {}))
        if (c && !out.contains(c->domain()))
            out.append(c->domain());
    return out;
}

QStringList ExtensionRegistry::ids() const
{
    QStringList out;
    for (IToolContribution *c : m_byKind.value(ContributionKind::Format, {}))
        if (c)
            out.append(c->id());
    return out;
}

bool ExtensionRegistry::contains(const QString &id) const
{
    return m_byId.contains(id);
}

int ExtensionRegistry::count() const
{
    return m_byKind.value(ContributionKind::Format, {}).size();
}

} // namespace ng::core
