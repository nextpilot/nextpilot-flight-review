add_library(ps_application_shell OBJECT
  MainWindow.h
  MainWindow.cpp
  ShellComponent.h
  ShellComponent.cpp
  SettingsDialog.h
  SettingsDialog.cpp
  UiDialogEngine.h
  UiDialogEngine.cpp
  Theme.h
  Theme.cpp
)
set_target_properties(ps_application_shell PROPERTIES AUTOMOC ON)
target_include_directories(ps_application_shell PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application
  ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
)
target_link_libraries(ps_application_shell PUBLIC
  ps_host
  ps_kernel
  Qt6::Widgets
  Qt6::Network
  Qt6::Svg
  SARibbonBar::SARibbonBar
)

find_package(Qt6 QUIET COMPONENTS UiTools)
if(Qt6UiTools_FOUND)
  target_link_libraries(ps_application_shell PUBLIC Qt6::UiTools)
  target_compile_definitions(ps_application_shell PUBLIC PS_HAVE_UITOOLS=1)
else()
  target_compile_definitions(ps_application_shell PUBLIC PS_HAVE_UITOOLS=0)
  message(WARNING "未找到 Qt6 UiTools，扩展自带 .ui 将不可用")
endif()
