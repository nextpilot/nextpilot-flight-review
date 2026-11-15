#pragma once

#include "Component.h"

namespace ps::components
{

// 绘图模块：注册“绘图”Ribbon页；画布与页面实现留在本模块。
void registerPlotContributions(Registry &registry);

class PlotComponent : public IComponent
{
public:
    ComponentManifest manifest() const override;
    void contribute(Registry &registry, const HostContext &host) override;
};

} // namespace ps::components
