#pragma once

#include "Component.h"

namespace ps::components
{

// 数据源模块：注册数据源 Dock 与“数据”Ribbon页。
void registerDataSourceContributions(Registry &registry);

class DataSourceComponent : public IComponent
{
public:
    ComponentManifest manifest() const override;
    void contribute(Registry &registry, const HostContext &host) override;
};

} // namespace ps::components

