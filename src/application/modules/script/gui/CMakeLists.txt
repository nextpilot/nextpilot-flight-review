find_package(Qt6 REQUIRED COMPONENTS Widgets Gui)

# script 功能的**呈现层**：工作台停靠面板（files/editor/console/debug）。
#
# 为什么与 headless 器官（ps_module_script）分成两个产物：
#   器官是纯逻辑（Lua/Python 执行、过滤、标记），headless 纯净，CLI 也要链接；
#   呈现层含 QWidget，只能被 GUI 链接。
#
# 层契约：不链接 ps_module_script（同层横向禁止）——执行/REPL 经 viewAction
# script.workbench.* 回模块，面板只依赖 ps_context 的机制头。
#
# OBJECT 库：保证 PS_REGISTER_PANEL_T 的自注册 TU 一定参与链接
# （静态库中未被引用的目标文件会被链接器丢弃，自注册会静默失效）。
add_library(ps_module_script_gui OBJECT
    ScriptPanels.cpp
    ScriptPanels.h
    BatchPanel.cpp
    BatchPanel.h
)
target_include_directories(ps_module_script_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# ps_gui_theme：gutter 断点圆点按语义 token 取色（验收 8），主题库归 L3 共享层。
# 依赖方向仍是 呈现层 -> 共享 UI 基础设施，不依赖 ps_gui。
target_link_libraries(ps_module_script_gui PUBLIC ps_context ps_gui_theme Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(ps_module_script_gui PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
