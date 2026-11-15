add_library(ps_builtin_workbench_component OBJECT
  MainWindow.cpp
  MainWindow.h
  ShellComponent.cpp
  ShellComponent.h
)
set_target_properties(ps_builtin_workbench_component PROPERTIES AUTOMOC ON)
target_include_directories(ps_builtin_workbench_component PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/base
  ${PROJECT_SOURCE_DIR}/src/base/ui
  ${PROJECT_SOURCE_DIR}/src/base/host
  ${PROJECT_SOURCE_DIR}/src/base/host/extension
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/api
)
target_link_libraries(ps_builtin_workbench_component PUBLIC
  ps_host
  ps_kernel
  ps_cache
  ps_live
  ps_ui_base
  Qt6::Widgets
  Qt6::Network
  Qt6::Svg
  SARibbonBar::SARibbonBar
)
