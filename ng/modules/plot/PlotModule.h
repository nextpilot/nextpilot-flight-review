#pragma once

#include "IModule.h"

// 绘图器官：headless 下把 "plot.render" 意图编译成一份 PlotSpec 描述。
// GUI 模式下同一个 PlotSpec 交给真正的画布渲染；两条路径共享意图与 spec。
class PlotModule final : public ng::host::IModule
{
    Q_OBJECT
public:
    ng::host::ModuleInfo info() const override;
    void initialize(ng::host::ModuleContext &ctx) override;

private:
    class Handler;
    Handler *m_handler = nullptr;
};

Q_DECLARE_INTERFACE(PlotModule, "nextpilot.ng.PlotModule/1.0")
