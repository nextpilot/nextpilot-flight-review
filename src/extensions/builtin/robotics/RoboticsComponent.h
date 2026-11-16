#pragma once

#include "Component.h"

namespace ps::components
{

/// 机器人能力包：ROS 2 bag / MCAP 运行日志。
class RoboticsComponent : public IComponent
{
public:
    ComponentManifest manifest() const override;
    void contribute(Registry &registry, HostContext &host) override;
};

} // namespace ps::components
