find_package(Qt6 REQUIRED COMPONENTS Core)

# OBJECT 库：保证自注册 TU 一定参与链接（静态库会丢弃未被引用的 TU）。
add_library(ng_module_settings OBJECT
    SettingsModule.cpp
    SettingsModule.h
)
target_include_directories(ng_module_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ng_module_settings PUBLIC ng_host)
set_target_properties(ng_module_settings PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
