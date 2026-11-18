find_package(Qt6 REQUIRED COMPONENTS Core)

# 工具箱聚合层：把内置格式的自注册与外部插件扫描集中在这里。
# app 只依赖这一层，不认识任何具体格式驱动。
add_library(ng_ext_registry STATIC
    FormatToolbox.cpp
    FormatToolbox.h
    PluginScanner.cpp
    PluginScanner.h
)
target_include_directories(ng_ext_registry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ng_ext_registry PUBLIC ng_ext_csv ng_ext_ulog ng_core Qt6::Core)
set_target_properties(ng_ext_registry PROPERTIES AUTOMOC ON CXX_STANDARD 17)
