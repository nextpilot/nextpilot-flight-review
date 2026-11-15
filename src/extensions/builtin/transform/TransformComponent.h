#pragma once

#include "Component.h"

namespace ps::components
{

class TransformComponent : public IComponent
{
public:
    ComponentManifest manifest() const override;
    void contribute(Registry &registry, const HostContext &host) override;
};

} // namespace ps::components
