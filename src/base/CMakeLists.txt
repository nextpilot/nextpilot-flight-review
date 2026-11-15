find_package(Qt6 REQUIRED COMPONENTS Network)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_subdirectory(modules)

add_executable(plotscope_app
  main.cpp
  resources/resource/icons.qrc
)

target_include_directories(plotscope_app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/modules/shell/ui
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/api
)

target_link_libraries(plotscope_app PRIVATE
  ps_base_shell
  ps_base_plot
  ps_base_data_source
  Qt6::Widgets Qt6::Network Qt6::Svg
  ps_host ps_kernel ps_cache ps_live
)

if(TARGET ps_builtin_components)
  target_link_libraries(plotscope_app PRIVATE ps_builtin_components)
endif()

# 外部扩展靠宿主导出 ps_* 符号
set_target_properties(plotscope_app PROPERTIES ENABLE_EXPORTS ON)

ps_set_warnings(plotscope_app)

if(WIN32)
  set_target_properties(plotscope_app PROPERTIES WIN32_EXECUTABLE ON)
endif()
