#pragma once

#include "Component.h"

namespace ps::components
{

/* 内置壳组件：文件页与日志 Dock 的静态贡献。
 * 只描述宿主结构，动作由 CommandManager 执行。 */
class ShellComponent : public IComponent
{
public:
    ComponentManifest manifest() const override;
    void contribute(Registry &registry, HostContext &host) override;
};

} // namespace ps::components
