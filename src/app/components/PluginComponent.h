#pragma once

#include "Component.h"

struct PsPluginDescriptor;

namespace ps::components
{

/// 外部插件的组件适配：只使用现有 ABI（loader / ui），不跨 ABI 返回 QWidget。
class PluginComponent : public IComponent
{
public:
    explicit PluginComponent(const PsPluginDescriptor *descriptor) : m_descriptor(descriptor) {}

    void contribute(Registry &registry, const HostServices &host) override;

private:
    const PsPluginDescriptor *m_descriptor = nullptr;
};

} // namespace ps::components
