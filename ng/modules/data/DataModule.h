#pragma once

#include "IModule.h"

// 数据器官：管理"打开了哪些数据集"，不认识任何具体格式。
// 格式解析由扩展完成，data 模块只消费仓库里的 Dataset。
class DataModule final : public nextpilot::plotscope::host::IModule
{
    Q_OBJECT
public:
    // 静态元数据：供编译期自注册（NG_REGISTER_MODULE）使用，无需实例化。
    static nextpilot::plotscope::host::ModuleInfo moduleInfo();
    static nextpilot::plotscope::host::IModule *create();

    nextpilot::plotscope::host::ModuleInfo info() const override;
    void initialize(nextpilot::plotscope::host::ModuleContext &ctx) override;

    // GUI 环境下请求自己的界面部件（headless 时宿主不调用）。
    void onAttach(nextpilot::plotscope::host::UiContext &ui) override;
};
