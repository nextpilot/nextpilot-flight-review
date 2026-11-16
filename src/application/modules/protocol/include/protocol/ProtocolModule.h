#pragma once

#include "Component.h"

namespace ps::components
{

class ProtocolModule : public IComponent
{
public:
    ComponentManifest manifest() const override;
    void contribute(Registry &registry, HostContext &host) override;
};

} // namespace ps::components
