#include "ExtensionHost.h"


namespace ps::components
{

ExtensionHost::~ExtensionHost()
{
    deactivate();
}

void ExtensionHost::activate(const HostContext &context)
{
    if (!m_components.empty())
        return;
    m_errors.clear();
    for (const ExtensionEntry &entry : ExtensionRegistry::instance().extensions())
    {
        std::unique_ptr<IComponent> component = entry.factory();
        if (!component)
        {
            m_errors << QStringLiteral("%1：组件工厂返回空").arg(entry.descriptor.id);
            continue;
        }
        component->contribute(m_registry, context);
        m_components.push_back(std::move(component));
    }
}

void ExtensionHost::deactivate()
{
    m_components.clear();
}

} // namespace ps::components
