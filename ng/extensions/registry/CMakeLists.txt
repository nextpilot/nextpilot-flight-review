find_package(Qt6 REQUIRED COMPONENTS Core)

# 工具箱聚合层：提供通用贡献实现 + 外部插件扫描入口。
# 内置知识包各自在领域目录里静态自注册（NG_REGISTER_PACK），本层不枚举它们——
# 新增一个领域包 = 新增一个目录 + 一行 CMake，这里的代码不用动。
add_library(ng_ext_registry STATIC
    Toolbox.cpp
    Toolbox.h
    PluginScanner.cpp
    PluginScanner.h
)
target_include_directories(ng_ext_registry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ng_ext_registry PUBLIC ng_core Qt6::Core)
set_target_properties(ng_ext_registry PROPERTIES AUTOMOC ON CXX_STANDARD 17)
