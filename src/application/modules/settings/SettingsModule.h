#pragma once

#include "IWorkbenchContribution.h"

#include <QStringList>

// 设置模块：**统一设置管理的机制提供者**。
//
// 定位（"是工具不懂业务"的严格应用）：
//   - 它提供**存取机制**：作用域、类型化读写、变更通知、落盘时机；
//   - 它**不知道任何具体设置项**：主题叫什么、哪些扩展该启用、布局长什么样，
//     都不在这个文件里——那些是各贡献方（模块本地登记 / 扩展包贡献）自己声明的；
//   - 键的**集中登记**（避免字符串散落各处）放在各自的声明处，而不是这里。
//
// 三个作用域（见 context::SettingsStore）：
//   App        用户偏好，跨工作区
//   Workspace  会话状态，重启可恢复（窗口几何、dock 排列、最近源）
//   Document   具体工作区，可分享（plot 布局、曲线样式）
namespace nextpilot::plotscope::workbench {
class ICommands;
}

class SettingsModule final : public nextpilot::plotscope::host::IWorkbenchContribution
{
    Q_OBJECT
public:
    // 静态元数据：供编译期自注册（PS_REGISTER_MODULE）使用，无需实例化。
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
