#pragma once

#include "Component.h"

namespace ps::components
{

/// 汽车能力包：ASC / BLF / MDF4 总线日志。
class AutomotiveComponent : public IComponent
{
public:
    ComponentManifest manifest() const override;
    void contribute(Registry &registry, HostContext &host) override;
};

} // namespace ps::components
