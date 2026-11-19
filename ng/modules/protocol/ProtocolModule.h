#pragma once

#include "IModule.h"

// 协议器官：把 ng/libs/protocol 的纯逻辑能力（新建/打开/校验 .psproto +
// SPI v2 自定义协议解析）以 UI 入口的形式贡献到"协议"Ribbon 页。
//
// 它不认识任何 Widgets：只声明 UiAction（纯数据）。真正的对话框由外壳按 action id
// 特判弹出（复用 ProtocolEditorDialog / ProtocolInspectorDialog），因此 CLI/headless
// 装载本模块也不会引入 Qt Widgets。
class ProtocolModule final : public nextpilot::plotscope::host::IModule
{
    Q_OBJECT
public:
    static nextpilot::plotscope::host::ModuleInfo moduleInfo();
    static nextpilot::plotscope::host::IModule *create();

    nextpilot::plotscope::host::ModuleInfo info() const override;
    void initialize(nextpilot::plotscope::host::ModuleContext &ctx) override;
    void onAttach(nextpilot::plotscope::host::UiContext &ui) override;
};

Q_DECLARE_INTERFACE(ProtocolModule, "nextpilot.ng.ProtocolModule/1.0")
