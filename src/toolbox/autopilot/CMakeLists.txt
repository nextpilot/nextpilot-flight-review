find_package(Qt6 REQUIRED COMPONENTS Core)

# OBJECT 库：保证自注册 TU 一定参与链接（见 common/CMakeLists.txt 的说明）。
add_library(ps_toolbox_autopilot OBJECT
    UlogFormatParser.cpp
    UlogStreamParser.cpp
    AutopilotPack.cpp
    AutopilotPack.h
)
target_include_directories(ps_toolbox_autopilot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ps_toolbox_autopilot PUBLIC ps_context)
set_target_properties(ps_toolbox_autopilot PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
