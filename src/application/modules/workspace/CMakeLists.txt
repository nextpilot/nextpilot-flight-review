find_package(Qt6 REQUIRED COMPONENTS Core)

# OBJECT 库：保证自注册 TU 一定参与链接（静态库会丢弃未被引用的 TU）。
add_library(ps_module_workspace OBJECT
    WorkspaceModule.cpp
    WorkspaceModule.h
)
target_include_directories(ps_module_workspace PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ps_module_workspace PUBLIC ps_host)
set_target_properties(ps_module_workspace PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
