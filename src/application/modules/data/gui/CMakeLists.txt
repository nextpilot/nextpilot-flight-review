find_package(Qt6 REQUIRED COMPONENTS Widgets Gui)

# data 功能的**呈现层**：领域专属面板的 QWidget 实现。
#
# 为什么与 headless 器官（ps_module_data）分成两个产物：
#   器官是纯数据与协议（headless 纯净），CLI 也要链接；
#   呈现层含 QWidget，只能被 GUI 链接。
#   分开之后，CLI 的依赖树里没有 Qt Widgets（ldd 验证）。
#
# 依赖方向：呈现层 -> core（面板工厂表在 core，纯数据）。
#   不依赖 ps_gui —— 否则模块就要依赖整个外壳层，依赖方向就反了。
#
# OBJECT 库：保证 PS_REGISTER_PANEL 的自注册 TU 一定参与链接
# （静态库中未被引用的目标文件会被链接器丢弃，自注册会静默失效）。
add_library(ps_module_data_gui OBJECT
    DataManagerPanel.cpp
    DataManagerPanel.h
    LiveConnectDialog.cpp
    LiveConnectDialog.h
    DataTablePanel.cpp
    DataTablePanel.h
)
target_include_directories(ps_module_data_gui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
# 主题是横向 GUI 基础设施（状态点等取色走它），单独成库，谁需要谁链——
# 不必为此链接整个外壳库 ps_gui。
target_link_libraries(ps_module_data_gui PUBLIC ps_context ps_data_tablemodel ps_gui_theme ps_gui_transform Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(ps_module_data_gui PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
