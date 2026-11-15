find_package(Qt6 REQUIRED COMPONENTS Network)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(plotscope_app
  main.cpp
  ui/shell/MainWindow.h
  ui/shell/MainWindow.cpp
  ui/shell/Theme.h
  ui/shell/Theme.cpp
  ui/data_input/DataInputDialog.h
  ui/data_input/DataInputDialog.cpp
  ui/extension_dialog/DialogEngine.h
  ui/extension_dialog/DialogEngine.cpp
  ui/canvas/CanvasWidget.h
  ui/canvas/CanvasWidget.cpp
  host/extension/ShellComponent.h
  host/extension/ShellComponent.cpp
  resources/resource/icons.qrc
)

target_include_directories(plotscope_app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/ui
  ${CMAKE_CURRENT_SOURCE_DIR}/host
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/api
)

target_link_libraries(plotscope_app PRIVATE
  Qt6::Widgets Qt6::Network Qt6::Svg
  ps_host ps_kernel ps_cache ps_live
)

if(TARGET ps_builtin_components)
  target_link_libraries(plotscope_app PRIVATE ps_builtin_components)
endif()

# 外部扩展靠宿主导出 ps_* 符号
set_target_properties(plotscope_app PROPERTIES ENABLE_EXPORTS ON)

find_package(Qt6 QUIET COMPONENTS UiTools)
if(Qt6UiTools_FOUND)
  target_link_libraries(plotscope_app PRIVATE Qt6::UiTools)
  target_compile_definitions(plotscope_app PRIVATE PS_HAVE_UITOOLS=1)
else()
  target_compile_definitions(plotscope_app PRIVATE PS_HAVE_UITOOLS=0)
  message(WARNING "未找到 Qt6 UiTools，扩展自带 .ui 将不可用")
endif()

target_link_libraries(plotscope_app PRIVATE SARibbonBar::SARibbonBar)
ps_set_warnings(plotscope_app)

if(WIN32)
  set_target_properties(plotscope_app PROPERTIES WIN32_EXECUTABLE ON)
endif()
