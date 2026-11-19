# ng_registry：注册表最小核（INTERFACE 库，header-only）
#
# 提供泛型 Registry<T, Key> 的增删查遍历最小 CRUD，以及自注册助手
# StaticRegistry<T> / AutoRegistrar / NG_AUTO_REGISTER 宏。
# 各具体注册表（PanelFactoryRegistry / BuiltinPackRegistry / ParserRegistry…）
# 退化为「定义条目类型 + 薄壳委托」，不再各写一份 QHash 逻辑。
#
# 只依赖 Qt Core + STL，无 QWidget、无 Rust、无 core 业务类型，headless 可用。
# header-only：无 .cpp、无链接产物，仅提供 include 路径。
add_library(ng_registry INTERFACE)
target_include_directories(ng_registry INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ng_registry INTERFACE Qt6::Core)
