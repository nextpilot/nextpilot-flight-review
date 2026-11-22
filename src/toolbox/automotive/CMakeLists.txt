find_package(Qt6 REQUIRED COMPONENTS Core)

# 汽车领域包：预留位，格式平移时在此目录追加驱动即可，不改其他代码。
add_library(ps_toolbox_automotive OBJECT
    AutomotivePack.cpp
    AutomotivePack.h
)
target_include_directories(ps_toolbox_automotive PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ps_toolbox_automotive PUBLIC ps_context)
set_target_properties(ps_toolbox_automotive PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)

# 呈现层（领域专属面板的 QWidget 实现）：单独一个产物，只被 GUI 链接。
# 见 gui/CMakeLists.txt 的说明。
add_subdirectory(gui)
