#pragma once

#include "Component.h"

namespace ps::components
{

// 分析模块：注册“分析”Ribbon 页与分析应用库，不依赖具体数据源模块。
void registerAnalysisContributions(Registry &registry);

class AnalysisComponent : public IComponent
{
public:
    ComponentManifest manifest() const override;
    void contribute(Registry &registry, HostContext &host) override;
};

} // namespace ps::components
