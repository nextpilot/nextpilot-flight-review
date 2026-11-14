#pragma once

#include "host/extension/Component.h"

namespace ps::components
{

class CustomProtocolComponent : public IComponent
{
public:
    ComponentManifest manifest() const override;
    void contribute(Registry &registry, const HostServices &host) override;
};

} // namespace ps::components
