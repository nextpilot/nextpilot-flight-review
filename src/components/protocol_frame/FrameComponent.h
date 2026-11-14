#pragma once

#include "app/components/Component.h"

namespace ps::components
{

class FrameComponent : public IComponent
{
public:
    void contribute(Registry &registry, const HostServices &host) override;
};

} // namespace ps::components
