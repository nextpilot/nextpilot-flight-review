#pragma once

#include "IModule.h"

// 设置器官：**统一设置管理的机制提供者**。
//
// 定位（"是工具不懂业务"的严格应用）：
//   - 它提供**存取机制**：作用域、类型化读写、变更通知、落盘时机；
//   - 它**不知道任何具体设置项**：主题叫什么、哪些扩展该启用、布局长什么样，
//     都不在这个文件里——那些是各贡献方（器官本地登记 / 知识包贡献）自己声明的；
//   - 键的**集中登记**（避免字符串散落各处）放在各自的声明处，而不是这里。
//
// 三个作用域（见 context::SettingsStore）：
//   App        用户偏好，跨工作区
//   Workspace  会话状态，重启可恢复（窗口几何、dock 排列、最近源）
//   Document   具体工作区，可分享（plot 布局、曲线样式）
class SettingsModule final : public nextpilot::plotscope::host::IModule
{
    Q_OBJECT
public:
    // 静态元数据：供编译期自注册（NG_REGISTER_MODULE）使用，无需实例化。
    static nextpilot::plotscope::host::ModuleInfo moduleInfo();
    static nextpilot::plotscope::host::IModule *create();

    nextpilot::plotscope::host::ModuleInfo info() const override;
    void initialize(nextpilot::plotscope::host::ModuleContext &ctx) override;

private:
    class Handler;
    Handler *m_handler = nullptr; // parent=this，随模块析构自动回收
};
