#pragma once

#include "IModule.h"

// 脚本器官：注册 "script.run" 意图处理器。
// 它不认识任何业务格式或绘图细节——只把门面 ps 注入 Lua，脚本的 ps.* 调用
// 由门面翻译成普通意图，走与 GUI/CLI 完全相同的执行器。
class ScriptModule final : public ng::host::IModule
{
    Q_OBJECT
public:
    // 静态元数据：供编译期自注册（NG_REGISTER_MODULE）使用，无需实例化。
    static ng::host::ModuleInfo moduleInfo();
    static ng::host::IModule *create();

    ng::host::ModuleInfo info() const override;
    void initialize(ng::host::ModuleContext &ctx) override;

private:
    class Handler;
    Handler *m_handler = nullptr; // parent=this，随模块析构自动回收
};

Q_DECLARE_INTERFACE(ScriptModule, "nextpilot.ng.ScriptModule/1.0")
