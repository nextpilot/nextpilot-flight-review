#pragma once

// 壳内置工作台视图的 **声明半**（A2b 笔2b 第②步返工，复审必修 2）：只构造
// ContributesDoc 调 views.registry->declare，不 include 任何 gui 头、不注册
// provider。工厂实现（registerViewProvider）在 gui 侧：
//   src/application/gui/shell/BuiltinViewProviders.{h,cpp}
// —— ps_assembly 不得 include gui/views：装配层反向依赖 gui 实现后，headless
// 纯净只靠"CLI 碰巧不引用该符号、静态库不拉目标文件"维持，不是约束。
//
// 为什么是独立 TU、由 gui_main 显式调用、而不是塞进 WorkbenchServices 构造：
// WorkbenchServices 是 App（CLI 与 GUI 共用组合根）的成员——CLI 二进制的链接
// 闭包不该被 gui 侧装配拉宽。本 TU 纯数据、无 Qt Widgets 依赖，CLI 链接无害；
// provider 注册 TU 只被 plotscope-gui 的装配钩子引用。
//
// owner = "core.workbench"（v3 表 A：工作台自带视图 views 声明放 assembly；
// 业务视图一律由模块自己声明 + 注册，壳不替模块声明——属性面板 plot.properties
// 归 plot 模块，笔 4 随 modules/plot/gui 迁入，本声明不含它）。

namespace nextpilot::plotscope::instantiation {
class InstantiationService;
}

namespace nextpilot::plotscope::assembly {

// 声明 core.workbench 的 viewsContainers/views（2 容器 + 4 视图）。幂等：重复
// 调用按 ViewRegistry 先到者为准，不覆盖。services 无 views.registry（未装配）
// 时返回 false，不硬造。
bool declareBuiltinWorkbenchViews(instantiation::InstantiationService *services);

} // namespace nextpilot::plotscope::assembly
