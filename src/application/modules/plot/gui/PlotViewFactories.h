#pragma once

// plot 功能视图装配（A2c）：plot 三点的 provider **注册半**（property-panel 归位
// plot.properties + plot.instrument / plot.workspace editor 化）。
//
// 职责分界（A2b §7 打回线 1 同款）：
//   - views 声明 / customEditors 声明与 provider 登记**都在本 TU**（owner "plot"，
//     物理位置 modules/plot/gui——plot 自声明，壳不代声明）：模块 initialize 在
//     window.build 内 host.activate 才跑，registerEditorProvider 的防冒名校验要求
//     声明先于注册，故声明与注册同 TU 同次完成，声明在注册之前；
//   - 工厂实现留 plot/gui 各面板文件（PropertyPanel / InstrumentView / PlotWorkspace）。
//
// 调用时序（gui_main / test_gui）：App 构造（模块登记）→ 本函数 → MainWindow::build。
//
// 生命周期：IEditorProvider 是借用指针（ViewRegistry 不拥有），本 TU 用静态
// unique_ptr 持有，进程存续期与 Application 一致（ViewRegistry 无 unmount "plot"
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

namespace nextpilot::plotscope::plot {

// 注册 plot 的 view provider（plot.properties → PropertyPanel，owner "plot"）与
// editor provider（plot.instrument / plot.workspace，owner "plot"）。幂等：先到者为准。
// services / appCtx / executor 任一缺失返回 false，不硬造。
bool installPlotViewFactories(instantiation::InstantiationService *services, context::AppContext *appCtx,
                              intent::IIntentExecutor *executor);

} // namespace nextpilot::plotscope::plot
