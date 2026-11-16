#pragma once

#include "Component.h"

namespace ps::components
{

/// 飞控工具箱：飞控日志、MAVLink 遥测和 ULog/apbin 数据源。
class AutopilotComponent : public IComponent
{
public:
    ComponentManifest manifest() const override;
    void contribute(Registry &registry, HostContext &host) override;
};

} // namespace ps::components
