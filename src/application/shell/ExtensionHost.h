#pragma once

#include "Component.h"

#include <memory>
#include <vector>

namespace ps::components
{

// 组件宿主：持有已激活组件实例，并托管其贡献。
// 组件生命周期覆盖一次 MainWindow，避免 contribute() 后立即析构。
class ExtensionHost
{
public:
    ExtensionHost() = default;
    ~ExtensionHost();

    void activate(const HostContext &context);
    void deactivate();

    Registry &registry() { return m_registry; }
    const Registry &registry() const { return m_registry; }
    const QStringList &errors() const { return m_errors; }

private:
    Registry m_registry;
    std::vector<std::unique_ptr<IComponent>> m_components;
    QStringList m_errors;
};

} // namespace ps::components
