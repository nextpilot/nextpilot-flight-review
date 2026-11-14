#include "Component.h"

#include "app/data_input/DataInputRegistry.h"
#include "ps_plugin_api.h"

namespace ps::components
{

void Registry::addProtocol(const ProtocolContribution &protocol)
{
    m_protocols.push_back(protocol);
}

void Registry::addRibbon(const RibbonContribution &ribbon)
{
    m_ribbon.push_back(ribbon);
}

void Registry::addAction(const QString &id, std::function<void(const ActionContext &)> handler)
{
    m_actions.push_back({id, std::move(handler)});
}

void Registry::addLoader(const PsPluginDescriptor *descriptor)
{
    if (descriptor)
        DataInputRegistry::instance().registerDescriptor(descriptor);
}

const ProtocolContribution *Registry::protocol(const QString &kind) const
{
    for (const ProtocolContribution &p : m_protocols)
        if (p.kind == kind)
            return &p;
    return nullptr;
}

bool Registry::invoke(const QString &action, const ActionContext &ctx) const
{
    for (const auto &entry : m_actions)
    {
        if (entry.first == action)
        {
            entry.second(ctx);
            return true;
        }
    }
    return false;
}

} // namespace ps::components
