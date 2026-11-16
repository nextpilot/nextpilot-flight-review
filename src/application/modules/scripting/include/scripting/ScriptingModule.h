#pragma once

#include "Component.h"

namespace ps::components
{

// 脚本子系统：控制台（REPL）与自动化（Job + 事件）共用一个模块。
// 本阶段只提供引擎搬迁、受限 require 与数据面契约，REPL/Job 见 roadmap/script-automation.md。
class ScriptingModule : public IComponent
{
public:
    ComponentManifest manifest() const override;
    void contribute(Registry &registry, HostContext &host) override;
};

} // namespace ps::components
