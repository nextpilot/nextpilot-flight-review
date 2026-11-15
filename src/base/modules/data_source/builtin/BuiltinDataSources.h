#pragma once

#include "Component.h"

namespace ps::components
{

/// 过渡组件：集中登记内建扩展入口，后续由 GNSS / FlightLog 组件接管各自部分。
class BuiltinDataSourcesComponent : public IComponent
{
public:
    ComponentManifest manifest() const override;
    void contribute(Registry &registry, const HostContext &host) override;
};

} // namespace ps::components
