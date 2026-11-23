#pragma once

#include "IModule.h"

// 分析模块：提供"分析应用调度"机制（`analysis.run` 意图）+ 分析页 gallery 入口。
//
// 它不懂业务：具体"算什么"由领域扩展（autopilot 等）注册的应用条目决定；
// 本模块只提供 gallery 外壳 + 一个通用 default 应用（把活动源通道铺成默认曲线）。
// 不依赖 Widgets：只声明 UiAction，对话框/复盘面板由外壳或领域面板承接。
class AnalysisModule final : public nextpilot::plotscope::host::IModule
{
    Q_OBJECT
public:
    static nextpilot::plotscope::host::ModuleInfo moduleInfo();
    static nextpilot::plotscope::host::IModule *create();

    nextpilot::plotscope::host::ModuleInfo info() const override;
    void initialize(nextpilot::plotscope::host::RuntimeHost &host) override;
    void onAttach(nextpilot::plotscope::host::IUiContext &ui) override;

private:
    class Handler;
    Handler *m_handler = nullptr;
};

Q_DECLARE_INTERFACE(AnalysisModule, "nextpilot.ps.AnalysisModule/1.0")
