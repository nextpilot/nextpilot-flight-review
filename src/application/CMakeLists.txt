find_package(Qt6 REQUIRED COMPONENTS Network)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_subdirectory(modules)
add_subdirectory(shell)

add_executable(plotscope_app
  main.cpp
  resources/resource/icons.qrc
)

target_include_directories(plotscope_app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/shell
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
  ${PROJECT_SOURCE_DIR}/src/application/modules/protocol/include
)

target_link_libraries(plotscope_app PRIVATE
  ps_application_shell
  ps_application_plotting
  ps_application_datasource
  Qt6::Widgets Qt6::Network Qt6::Svg
  ps_host ps_kernel ps_cache ps_live
)

if(TARGET ps_application_protocol)
  target_link_libraries(plotscope_app PRIVATE ps_application_protocol)
endif()

if(TARGET ps_builtin_components)
  target_link_libraries(plotscope_app PRIVATE ps_builtin_components)
endif()

# 外部扩展靠宿主导出 ps_* 符号
set_target_properties(plotscope_app PROPERTIES ENABLE_EXPORTS ON)

ps_set_warnings(plotscope_app)

if(WIN32)
  set_target_properties(plotscope_app PROPERTIES WIN32_EXECUTABLE ON)
endif()

# ADS 按共享库编译（LGPL），Windows 下 DLL 落在自身输出目录，必须投到 exe 旁边
if(WIN32 AND TARGET ads)
  add_custom_command(TARGET plotscope_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ads> $<TARGET_FILE_DIR:plotscope_app>)
endif()
