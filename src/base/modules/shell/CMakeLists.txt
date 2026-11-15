add_library(ps_base_shell OBJECT
  ui/MainWindow.h
  ui/MainWindow.cpp
  ui/ShellComponent.h
  ui/ShellComponent.cpp
  ui/Theme.h
  ui/Theme.cpp
  ui/DialogEngine.h
  ui/DialogEngine.cpp
  ui/TransformDialogInterface.h
)
set_target_properties(ps_base_shell PROPERTIES AUTOMOC ON)
target_include_directories(ps_base_shell PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/base
  ${PROJECT_SOURCE_DIR}/src/base/host
  ${PROJECT_SOURCE_DIR}/src/base/host/extension
  ${PROJECT_SOURCE_DIR}/src/base/modules/runtime/include
  ${PROJECT_SOURCE_DIR}/src/base/modules/data_source/include
  ${PROJECT_SOURCE_DIR}/src/base/modules/data_source/ui
  ${PROJECT_SOURCE_DIR}/src/base/modules/plot/include
  ${PROJECT_SOURCE_DIR}/src/base/modules/plot/ui
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/api
)
target_link_libraries(ps_base_shell PUBLIC
  ps_host
  ps_kernel
  ps_cache
  ps_live
  ps_base_data_source
  ps_base_plot
  Qt6::Widgets
  Qt6::Network
  Qt6::Svg
  SARibbonBar::SARibbonBar
)

find_package(Qt6 QUIET COMPONENTS UiTools)
if(Qt6UiTools_FOUND)
  target_link_libraries(ps_base_shell PUBLIC Qt6::UiTools)
  target_compile_definitions(ps_base_shell PUBLIC PS_HAVE_UITOOLS=1)
else()
  target_compile_definitions(ps_base_shell PUBLIC PS_HAVE_UITOOLS=0)
  message(WARNING "未找到 Qt6 UiTools，扩展自带 .ui 将不可用")
endif()
