#pragma once

#include "IModule.h"
#include "LuaScriptLibrary.h"
#include "FilterCatalogue.h"
#include "ScriptFacade.h"
#include "LuaEngine.h"

#include <memory>

namespace nextpilot::plotscope::script
{
class BatchHandler;
class PythonHandler;
}

// 脚本模块：注册 "script.run" 意图处理器。
// 它不认识任何业务格式或绘图细节——只把门面 ps 注入 Lua，脚本的 ps.* 调用
// 由门面翻译成普通意图，走与 GUI/CLI 完全相同的执行器。
class ScriptModule final : public nextpilot::plotscope::host::IModule
{
    Q_OBJECT
public:
    // 静态元数据：供编译期自注册（PS_REGISTER_MODULE）使用，无需实例化。
    static nextpilot::plotscope::host::ModuleInfo moduleInfo();
    static nextpilot::plotscope::host::IModule *create();

    nextpilot::plotscope::host::ModuleInfo info() const override;
    void initialize(nextpilot::plotscope::host::RuntimeHost &host) override;
    void onAttach(nextpilot::plotscope::host::IUiContext &ui) override;
    ~ScriptModule() override;

private:
    class Handler;
    Handler *m_handler = nullptr; // parent=this，随模块析构自动回收
    nextpilot::plotscope::script::BatchHandler *m_batch = nullptr; // data.batch.run（parent=this）
    nextpilot::plotscope::script::PythonHandler *m_py = nullptr;   // script.py.*（parent=this）
    // 脚本库有状态，模块实例持有；TransformHub 由 AppContext 注入（libs 不藏静态状态）。
    std::unique_ptr<nextpilot::plotscope::script::LuaScriptLibrary> m_scriptLib;
    std::shared_ptr<nextpilot::plotscope::script::FilterCatalogue> m_filterCatalogue; // 内置过滤器目录（注入 FilterOps）

    // 工作台执行通道（viewAction，GUI 面板经 dispatchViewAction 调用，
    // 引擎归模块持有——GUI 层不直接依赖 script 模块内部类，分层守卫才过得去）：
    //   script.workbench.run  {source, name}   运行编辑器当前文档（引擎每次新建，与 script.run 同路径）
    //   script.workbench.repl {code}           控制台 REPL 求值（持久 VM，进程期共享全局状态）
    void registerWorkbenchActions(
        nextpilot::plotscope::host::RuntimeHost &host,
        nextpilot::plotscope::context::ScriptFacade::ViewActionDispatcher dispatcher);
    std::unique_ptr<nextpilot::plotscope::context::ScriptFacade> m_replFacade;
    std::unique_ptr<nextpilot::plotscope::script::LuaEngine> m_replEngine;
    // 调试会话引擎（script.workbench.debug 执行期持有；暂停期控制指令经它恢复/中断）。
    // 同一时刻至多一个调试会话（编辑器调试运行是模态的：执行返回前嵌套等恢复）。
    std::unique_ptr<nextpilot::plotscope::script::LuaEngine> m_dbgEngine;
    // GUI 上下文（onAttach 时注入；headless 恒空）。编辑器面板**按需创建**：
    // 中央区永远锚定画布（模块激活顺序不定，不能让编辑器抢先占位）。
    nextpilot::plotscope::host::IUiContext *m_ui = nullptr;
};

Q_DECLARE_INTERFACE(ScriptModule, "nextpilot.ps.ScriptModule/1.0")
