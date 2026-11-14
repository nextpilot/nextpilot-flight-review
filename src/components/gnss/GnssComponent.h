#pragma once

#include "app/components/Component.h"

namespace ps::components
{

/// GNSS 能力包：NMEA / UBX / NovAtel OEM7 数据源。
class GnssComponent : public IComponent
{
public:
    ComponentManifest manifest() const override;
    void contribute(Registry &registry, const HostServices &host) override;
};

} // namespace ps::components
