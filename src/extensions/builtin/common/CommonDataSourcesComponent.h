#pragma once

#include "Component.h"

namespace ps::components
{

/// 通用数据源组件：只登记 CSV、JSON、binary 等跨领域格式，领域格式由各自工具箱注册。
class CommonDataSourcesComponent : public IComponent
{
public:
    ComponentManifest manifest() const override;
    void contribute(Registry &registry, HostContext &host) override;
};

} // namespace ps::components
