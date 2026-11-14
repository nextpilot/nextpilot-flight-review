#pragma once

#include "Component.h"

struct PsExtensionDescriptor;

namespace ps::components
{

/// 外部动态扩展的组件适配：只使用现有 ABI（loader / ui），不跨 ABI 返回 QWidget。
class DynamicExtension : public IComponent
{
public:
    explicit DynamicExtension(const PsExtensionDescriptor *descriptor) : m_descriptor(descriptor) {}

    ComponentManifest manifest() const override;
    void contribute(Registry &registry, const HostServices &host) override;

private:
    const PsExtensionDescriptor *m_descriptor = nullptr;
};

} // namespace ps::components
