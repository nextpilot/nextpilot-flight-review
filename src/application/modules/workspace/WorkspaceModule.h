#pragma once

#include "IModule.h"

// 工作区预设模块：把"源清单 + 画布布局"按名字存成 .psw，并能整体打开。
//
// 定位（只管搬运，不重建模）：源条目原文来自 WorkspaceDataConfig，
// 画布布局来自 PersistentStateHub 的 document 段；本模块只做这两块的
// 组装、落盘、回填，不认识任何解析器参数或绘图语义。
class WorkspaceModule final : public nextpilot::plotscope::host::IModule
{
    Q_OBJECT
public:
    static nextpilot::plotscope::host::ModuleInfo moduleInfo();
    static nextpilot::plotscope::host::IModule *create();

    nextpilot::plotscope::host::ModuleInfo info() const override;
    void initialize(nextpilot::plotscope::host::ModuleContext &ctx) override;

private:
    class Handler;
    Handler *m_handler = nullptr; // parent=this，随模块析构自动回收
};
