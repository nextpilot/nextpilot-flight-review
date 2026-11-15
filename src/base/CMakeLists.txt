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
  ui/device/DeviceDebugDialog.h
  ui/device/DeviceDebugDialog.cpp
  ui/transform/TransformDialog.h
  ui/transform/TransformDialog.cpp
  ui/canvas/CanvasWidget.h
  ui/canvas/CanvasWidget.cpp
  ui/script/ScriptEngine.h
  ui/script/ScriptEngine.cpp
  ui/video/VideoSyncWidget.h
  ui/video/VideoSyncWidget.cpp
  host/extension/ShellComponent.h
  host/extension/ShellComponent.cpp
  host/extension/BuiltinDataSources.h
  host/extension/BuiltinDataSources.cpp
  resources/resource/icons.qrc
)

target_include_directories(plotscope_app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/ui
  ${CMAKE_CURRENT_SOURCE_DIR}/host
  ${PROJECT_SOURCE_DIR}/src/extensions/builtin/custom_protocol
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/extensions/dynamic/api
)

target_link_libraries(plotscope_app PRIVATE
  Qt6::Widgets Qt6::Network Qt6::Svg
  ps_host ps_kernel ps_cache ps_live
  ps_data_loader_csv ps_data_loader_ulog ps_data_loader_apbin ps_data_loader_asc ps_data_loader_blf ps_data_load_mcap ps_data_loader_json ps_data_loader_binary ps_data_loader_mdf4 ps_data_loader_rosbag2
  ps_data_source_sim ps_data_source_mavlink
)

if(TARGET ps_custom_protocol_component)
  target_link_libraries(plotscope_app PRIVATE ps_custom_protocol_component ps_custom_protocol)
  target_link_libraries(plotscope_app PRIVATE ps_protocol)
endif()
if(TARGET ps_gnss_component)
  target_link_libraries(plotscope_app PRIVATE ps_gnss_component ps_gnss)
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

if(PS_ENABLE_LUA)
  find_package(Lua 5.4)
  if(LUA_FOUND)
    target_include_directories(plotscope_app PRIVATE ${LUA_INCLUDE_DIR})
    target_link_libraries(plotscope_app PRIVATE ${LUA_LIBRARIES})
    target_compile_definitions(plotscope_app PRIVATE PS_HAVE_LUA=1)
  else()
    if(WIN32)
      message(WARNING "未找到 Lua 5.4：脚本面板不可用，构建继续；不需要脚本面板可加 -DPS_ENABLE_LUA=OFF")
    else()
      message(WARNING "未找到 Lua 5.4，脚本面板运行时会提示缺少 liblua5.4-dev")
    endif()
  endif()
endif()

target_link_libraries(plotscope_app PRIVATE SARibbonBar::SARibbonBar)
ps_set_warnings(plotscope_app)

if(WIN32)
  set_target_properties(plotscope_app PROPERTIES WIN32_EXECUTABLE ON)
endif()
if(TARGET ps_gnss)
  add_custom_command(TARGET plotscope_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PROJECT_SOURCE_DIR}/src/extensions/builtin/gnss/oem7_database.json
            $<TARGET_FILE_DIR:plotscope_app>/oem7_database.json)
endif()
