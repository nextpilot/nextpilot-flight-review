# ng_registry：泛型注册表最小核（header-only）。
# 泛型 Registry<T> + 启动期自注册助手，各具体表委托它，不再各写一份容器。
# 只依赖 Qt Core + STL，headless 可用。
add_library(ng_registry INTERFACE)
target_include_directories(ng_registry INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ng_registry INTERFACE Qt6::Core)
