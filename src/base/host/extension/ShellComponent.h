#pragma once

#include "Component.h"

namespace ps::components
{

/* 内置壳组件：文件 / 脚本 / 视图这类静态 Ribbon 页。
 * 只描述菜单结构，实际动作由 app 的 CommandRegistry 执行。 */
class ShellComponent : public IComponent
{
public:
    ComponentManifest manifest() const override;
    void contribute(Registry &registry, const HostContext &host) override;
};

} // namespace ps::components
