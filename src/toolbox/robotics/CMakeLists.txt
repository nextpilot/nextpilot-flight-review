find_package(Qt6 REQUIRED COMPONENTS Core)

# 机器人领域包：预留位，格式平移时在此目录追加驱动即可，不改其他代码。
add_library(ps_toolbox_robotics OBJECT
    RoboticsPack.cpp
    RoboticsPack.h
)
target_include_directories(ps_toolbox_robotics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ps_toolbox_robotics PUBLIC ps_context)
set_target_properties(ps_toolbox_robotics PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
