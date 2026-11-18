find_package(Qt6 REQUIRED COMPONENTS Core)

# OBJECT 库：模块自注册依赖 TU 静态对象的构造函数，
# 静态库中未被引用的目标文件会被链接器丢弃，注册会失效。
add_library(ng_module_data OBJECT DataModule.cpp DataModule.h)
target_include_directories(ng_module_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ng_module_data PUBLIC ng_host)
set_target_properties(ng_module_data PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
