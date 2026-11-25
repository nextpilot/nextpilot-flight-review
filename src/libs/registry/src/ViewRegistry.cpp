#include "ViewRegistry.h"

#include <algorithm>

namespace nextpilot::plotscope::registry
{

bool ViewRegistry::registerViewProvider(const QString &viewId, ViewFactory factory,
                                        const QString &owner)
{
    if (viewId.isEmpty() || !factory)
        return false;
    if (m_providers.contains(viewId))
        return false; // 先到者为准
    m_providers.insert(viewId, ProviderEntry{std::move(factory), owner});
    return true;
}

int ViewRegistry::unregisterOwner(const QString &owner)
{
    int removed = 0;
    for (auto it = m_providers.begin(); it != m_providers.end();)
    {
        if (it.value().owner == owner)
        {
            it = m_providers.erase(it);
            ++removed;
        }
        else
            ++it;
    }
    return removed;
}

QWidget *ViewRegistry::create(const QString &viewId, context::AppContext &appCtx,
                              intent::IIntentExecutor &executor, QWidget *parent) const
{
    const ProviderEntry *e = provider(viewId);
    return e ? e->factory(appCtx, executor, parent) : nullptr;
}

bool ViewRegistry::registerActionProvider(const QString &owner, const QString &slotId,
                                          IActionProvider *p)
{
    if (owner.isEmpty() || slotId.isEmpty() || p == nullptr)
        return false;
    // 同 owner 同 slot 只允许一个：先到者为准，与 registerViewProvider 口径一致。
    for (const auto &e : m_actionProviders)
        if (e.owner == owner && e.slotId == slotId)
            return false;
    m_actionProviders.append(ActionProviderEntry{p, owner, slotId});
    return true;
}

QList<ViewRegistry::ActionProviderEntry> ViewRegistry::actionProviders() const
{
    return m_actionProviders;
}

QList<ViewRegistry::ActionProviderEntry> ViewRegistry::actionProvidersFor(const QString &slotId) const
{
    QList<ActionProviderEntry> out;
    for (const auto &e : m_actionProviders)
        if (e.slotId == slotId)
            out.append(e);
    return out;
}

void ViewRegistry::declare(const ContributesDoc &doc)
{
    if (doc.owner.isEmpty())
        return;
    if (!m_ownerOrder.contains(doc.owner))
        m_ownerOrder.append(doc.owner);
    for (auto c : doc.viewContainers)
    {
        c.owner = doc.owner;
        m_containers.append(c);
    }
    for (auto v : doc.views)
    {
        v.owner = doc.owner;
        m_views.append(v);
    }
}

void ViewRegistry::undeclare(const QString &owner)
{
    m_ownerOrder.removeAll(owner);
    for (int i = m_containers.size() - 1; i >= 0; --i)
        if (m_containers.at(i).owner == owner)
            m_containers.removeAt(i);
    for (int i = m_views.size() - 1; i >= 0; --i)
        if (m_views.at(i).owner == owner)
            m_views.removeAt(i);
    unregisterOwner(owner);
    for (int i = m_actionProviders.size() - 1; i >= 0; --i)
        if (m_actionProviders.at(i).owner == owner)
            m_actionProviders.removeAt(i);
}

QList<ContributedViewContainer> ViewRegistry::containersOf(const QString &owner) const
{
    QList<ContributedViewContainer> out;
    for (const auto &c : m_containers)
        if (c.owner == owner)
            out.append(c);
    return out;
}

QList<ContributedView> ViewRegistry::viewsOf(const QString &owner) const
{
    QList<ContributedView> out;
    for (const auto &v : m_views)
        if (v.owner == owner)
            out.append(v);
    return out;
}

QList<ContributedView> ViewRegistry::viewsIn(const QString &containerId) const
{
    QList<ContributedView> out;
    for (const auto &v : m_views)
        if (v.container == containerId)
            out.append(v);
    std::stable_sort(out.begin(), out.end(), [](const ContributedView &a, const ContributedView &b) {
        if (a.order != b.order)
            return a.order < b.order;
        return a.id < b.id;
    });
    return out;
}

} // namespace nextpilot::plotscope::registry
