find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui)

# 汽车领域的**呈现层**：领域专属面板的 QWidget 实现。
#
# 为什么与知识层（ps_toolbox_automotive）分成两个产物：
#   知识层是纯数据与协议（headless 纯净），CLI 也要链接；
#   呈现层含 QWidget，只能被 GUI 链接。
#   分开之后，CLI 的依赖树里没有 Qt Widgets（ldd 验证），
#   而 GUI 侧却能拿到领域专属的面板实现。
#
# 依赖方向：呈现层 -> core（面板工厂表在 core，纯数据）。
#   不依赖 ps_gui —— 否则扩展就要依赖整个 GUI 层，依赖方向就反了。
#
# OBJECT 库：保证 PS_REGISTER_PANEL 的自注册 TU 一定参与链接
# （静态库中未被引用的目标文件会被链接器丢弃，自注册会静默失效）。
add_library(ps_toolbox_automotive_gui OBJECT
    CanBrowserPanel.cpp
    CanBrowserPanel.h
)
target_include_directories(ps_toolbox_automotive_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ps_toolbox_automotive_gui PUBLIC ps_context Qt6::Widgets Qt6::Gui Qt6::Core)
set_target_properties(ps_toolbox_automotive_gui PROPERTIES AUTOMOC ON CXX_STANDARD 17
                                                  POSITION_INDEPENDENT_CODE ON)
