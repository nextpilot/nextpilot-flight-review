#pragma once

#include "IWorkbenchContribution.h"

// 绘图模块：把"数据集 + 通道"编译成 PlotSpec（纯数据），并提供分析算子入口。
// 它不认识任何具体格式，也不直接画图——渲染由 UI 模块消费同一份 spec。
class PlotModule final : public nextpilot::plotscope::host::IWorkbenchContribution
{
    Q_OBJECT
public:
    // 静态元数据：供编译期自注册（PS_REGISTER_MODULE）使用，无需实例化。
    static nextpilot::plotscope::host::ModuleInfo moduleInfo();
    static nextpilot::plotscope::host::IWorkbenchContribution *create();

    nextpilot::plotscope::host::ModuleInfo info() const override;
    void initialize(nextpilot::plotscope::instantiation::InstantiationService &services) override;

    // GUI 环境下声明本模块的界面入口（headless 时宿主不调用）。
    void onAttach(nextpilot::plotscope::host::IUiContext &ui) override;

private:
    class Handler;
    Handler *m_handler = nullptr; // parent=this，随模块析构自动回收
};
