#pragma once

#include "Component.h"

namespace ps::components
{

/// 飞控能力包：MAVLink 全协议解析。
class AutopilotComponent : public IComponent
{
public:
    ComponentManifest manifest() const override;
    void contribute(Registry &registry, HostContext &host) override;
};

} // namespace ps::components
