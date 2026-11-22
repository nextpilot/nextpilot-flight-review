find_package(Qt6 REQUIRED COMPONENTS Core)

# OBJECT 库：保证自注册 TU 一定参与链接（见 data/CMakeLists.txt 的说明）。
add_library(ps_module_plot OBJECT PlotModule.cpp PlotModule.h PlotLayout.cpp PlotLayout.h)
target_include_directories(ps_module_plot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ps_module_plot PUBLIC ps_host)
set_target_properties(ps_module_plot PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
