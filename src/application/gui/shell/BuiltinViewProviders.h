#pragma once

// 壳内置工作台视图的 **注册半**（A2b 笔2b 第②步返工，复审必修 2）：五个（现
// 四个，属性面板归 plot 笔 4）registerViewProvider 在 gui 侧 TU——声明在
// assembly（BuiltinWorkbenchViews）、工厂实现在 gui/views，装配层次不倒挂，
// ps_assembly 不 include 任何 gui 头。
//
// 调用时序（gui_main / test_gui）：先 declareBuiltinWorkbenchViews（声明进
// views.registry），后本函数（provider 登记）——两半都先于 MainWindow::build。

namespace nextpilot::plotscope::instantiation {
class InstantiationService;
}

namespace nextpilot::plotscope::gui {

// 注册 core.workbench 的 view provider。幂等：registerViewProvider 先到者为准。
// services 无 views.registry（未装配）时返回 false，不硬造。
bool installBuiltinWorkbenchViewProviders(instantiation::InstantiationService *services);

} // namespace nextpilot::plotscope::gui
