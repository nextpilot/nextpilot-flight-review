add_library(ps_base_plot OBJECT
  include/PlotModule.h
  src/PlotModule.cpp
  ui/CanvasWidget.h
  ui/CanvasWidget.cpp
)
set_target_properties(ps_base_plot PROPERTIES AUTOMOC ON)
target_include_directories(ps_base_plot PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/base
  ${PROJECT_SOURCE_DIR}/src/base/kernel/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/ui
)
target_link_libraries(ps_base_plot PUBLIC ps_host ps_kernel Qt6::Widgets)
