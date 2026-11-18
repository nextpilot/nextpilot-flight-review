find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(plotscope-ng
    cli_main.cpp
    App.cpp
    App.h
)
target_link_libraries(plotscope-ng PRIVATE
    ng_module_data ng_module_plot ng_module_script ng_ext_registry ng_host ng_core Qt6::Core)
set_target_properties(plotscope-ng PROPERTIES AUTOMOC ON CXX_STANDARD 17)
