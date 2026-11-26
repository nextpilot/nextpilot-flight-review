#pragma once

// script 功能视图装配（A2c）：script 5 点的 provider **注册半**（script.files /
// script.console / script.debug 三个 view + script.editor / script.batchSummary
// 两个 editor）。
//
// 职责分界（A2b §7 打回线 1 同款）：
//   - views 声明 / customEditors 声明与 provider 登记**都在本 TU**（owner "script"，
//     物理位置 modules/script/gui——script 自声明，壳不代声明）：模块 initialize 在
//     window.build 内 host.activate 才跑，registerEditorProvider 的防冒名校验要求
//     声明先于注册，故声明与注册同 TU 同次完成，声明在注册之前；
//   - 工厂实现留 script/gui 各面板文件（ScriptPanels / BatchPanel）。
//
// 调用时序（gui_main / test_gui）：App 构造（模块登记）→ 本函数 → MainWindow::build。
//
// 生命周期：IEditorProvider 是借用指针（ViewRegistry 不拥有），本 TU 用静态
// unique_ptr 持有，进程存续期与 Application 一致（ViewRegistry 无 unmount "script"
// 的路径；若未来有，须先接管所有权再撤）。

namespace nextpilot::plotscope::context {
class AppContext;
}
namespace nextpilot::plotscope::intent {
class IIntentExecutor;
}
namespace nextpilot::plotscope::instantiation {
class InstantiationService;
}

namespace nextpilot::plotscope::script {

// 注册 script 的 3 个 view provider 与 2 个 editor provider（owner "script"）。
// 幂等：先到者为准。services / appCtx / executor 任一缺失返回 false，不硬造。
bool installScriptViewFactories(instantiation::InstantiationService *services, context::AppContext *appCtx,
                                intent::IIntentExecutor *executor);

} // namespace nextpilot::plotscope::script
