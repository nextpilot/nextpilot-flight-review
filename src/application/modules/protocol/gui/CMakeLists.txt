find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Core)

# protocol 域的**呈现层**（D18a）：协议编辑器 / 检查器对话框的 QWidget 实现。
#
# 这两个对话框此前住在 gui/shell，属于外壳层；D18a 把它们迁回模块 own 的
# gui 子目录：外壳从此零业务对话框 include/构造，只通过意图触发（见 MainWindow）。
#
# 为什么分成 ps_module_protocol（器官，headless）与 ps_module_protocol_gui（呈现层）：
#   器官是纯逻辑（校验/试算/模板库），CLI 也链；呈现层含 QWidget，只能 GUI 链。
#   分开后 CLI 的依赖树里没有 Qt Widgets（ldd 验证）。
#
# OBJECT 库：保证对话框里可能的自注册 TU 一定参与链接（静态库会丢弃未引用目标文件）。
add_library(ps_module_protocol_gui OBJECT
    ProtocolEditorDialog.cpp
    ProtocolEditorDialog.h
    ProtocolInspectorDialog.cpp
    ProtocolInspectorDialog.h
)
target_include_directories(ps_module_protocol_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# 依赖方向：呈现层 -> libs/机制库 + 共享 UI 基础设施（theme/transform 归 L3）。
# 不依赖 ps_gui —— 模块呈现层不反向依赖整个外壳层（层契约守卫）。
target_link_libraries(ps_module_protocol_gui PUBLIC
    ps_context ps_registry ps_parser ps_intent ps_protocol
    ps_gui_theme ps_gui_transform Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(ps_module_protocol_gui PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
