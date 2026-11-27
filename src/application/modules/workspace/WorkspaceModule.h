#pragma once

#include "IWorkbenchContribution.h"

#include <QStringList>

// 工作区预设模块：把"源清单 + 画布布局"按名字存成 .psw，并能整体打开。
//
// 定位（只管搬运，不重建模）：源条目原文来自 WorkspaceDataConfig，
// 画布布局来自 PersistentStateHub 的 document 段；本模块只做这两块的
// 组装、落盘、回填，不认识任何解析器参数或绘图语义。
namespace nextpilot::plotscope::workbench {
class ICommands;
}

class WorkspaceModule final : public nextpilot::plotscope::host::IWorkbenchContribution
{
    Q_OBJECT
public:
    static nextpilot::plotscope::host::ModuleInfo moduleInfo();
    static nextpilot::plotscope::host::IWorkbenchContribution *create();

    nextpilot::plotscope::host::ModuleInfo info() const override;
    void initialize(nextpilot::plotscope::instantiation::InstantiationService &services) override;

private:
    class Handler;
    Handler *m_handler = nullptr; // parent=this，随模块析构自动回收
    // 卸载时按 id 注销本模块注册的命令，避免命令表指向已销毁 handler。
    void onDetach() override;
    nextpilot::plotscope::workbench::ICommands *m_cmds = nullptr;
    QStringList m_commandIds;
};
