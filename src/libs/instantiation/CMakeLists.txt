# ps_instantiation：服务实例化与发现（对标 VSCode platform/instantiation）。
# registerSingleton / getService：按 id 注册实例，按类型安全取用。
# 纯 Qt Core，无 QObject、无上层依赖，headless 与 GUI 共用。
add_library(ps_instantiation STATIC
    src/InstantiationService.cpp
    include/InstantiationService.h
)
target_include_directories(ps_instantiation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ps_instantiation PUBLIC Qt6::Core)
set_target_properties(ps_instantiation PROPERTIES CXX_STANDARD 17 AUTOMOC OFF)
