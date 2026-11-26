#pragma once

// data 功能视图装配（A2b 笔2b 第③步）：data 两点的 provider **注册半**。
//
// 职责分界（§7 打回线 1）：
//   - views 声明 / customEditors 声明与 provider 登记**都在本 TU**（owner "data"，
//     物理位置 modules/data/gui——data 自声明，壳不代声明）：模块 initialize 在
//     host.activate（build 内）才跑，registerEditorProvider 的防冒名校验要求
//     声明先于注册，故声明与注册同 TU 同次完成，声明在注册之前；
//   - 工厂实现留 data/gui 各面板文件。
//
// 调用时序（gui_main / test_gui）：App 构造（模块登记）→ 本函数 → MainWindow::build。
//
// 生命周期：IEditorProvider 是借用指针（ViewRegistry 不拥有），本 TU 用静态
// unique_ptr 持有，进程存续期与 Application 一致（ViewRegistry 无 unmount "data"
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

namespace nextpilot::plotscope::data {

// 注册 data 的 view provider（data.datasetTree → DataManagerPanel）与 editor
// provider（data.table → DataTablePanel，owner "data"）。幂等：先到者为准。
// services / appCtx / executor 任一缺失返回 false，不硬造。
bool installDataViewFactories(instantiation::InstantiationService *services, context::AppContext *appCtx,
                              intent::IIntentExecutor *executor);

} // namespace nextpilot::plotscope::data
