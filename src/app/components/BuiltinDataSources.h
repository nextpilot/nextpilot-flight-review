#pragma once

#include "Component.h"

namespace ps::components
{

/// 过渡组件：集中登记内建插件入口，后续由 GNSS / FlightLog 组件接管各自部分。
class BuiltinDataSourcesComponent : public IComponent
{
public:
    ComponentManifest manifest() const override;
    void contribute(Registry &registry, const HostServices &host) override;
};

} // namespace ps::components
