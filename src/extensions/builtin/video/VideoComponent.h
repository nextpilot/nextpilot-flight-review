#pragma once

#include "host/extension/Component.h"

namespace ps::components
{

class VideoComponent : public IComponent
{
public:
    ComponentManifest manifest() const override;
    void contribute(Registry &registry, const HostContext &host) override;
};

} // namespace ps::components
