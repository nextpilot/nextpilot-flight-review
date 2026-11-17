#pragma once

#include "IModule.h"

// 数据器官：管理"打开了哪些数据集"，不认识任何具体格式。
// 格式解析由扩展完成，data 模块只消费仓库里的 Dataset。
class DataModule final : public ng::host::IModule
{
    Q_OBJECT
public:
    ng::host::ModuleInfo info() const override;
    void initialize(ng::host::ModuleContext &ctx) override;
};

Q_DECLARE_INTERFACE(DataModule, "nextpilot.ng.DataModule/1.0")
