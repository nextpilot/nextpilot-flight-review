find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(ng_test_architecture test_architecture.cpp)
target_include_directories(ng_test_architecture PRIVATE ${CMAKE_SOURCE_DIR}/app)
target_link_libraries(ng_test_architecture PRIVATE
    ng_module_data ng_module_plot ng_module_script ng_ext_csv ng_host ng_core Qt6::Core)
# 复用 CLI 里的 App 装配逻辑
target_sources(ng_test_architecture PRIVATE ${CMAKE_SOURCE_DIR}/app/App.cpp)
set_target_properties(ng_test_architecture PROPERTIES AUTOMOC ON CXX_STANDARD 17)
add_test(NAME ng_architecture COMMAND ng_test_architecture)
