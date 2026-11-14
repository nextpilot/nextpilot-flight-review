find_package(Qt6 REQUIRED COMPONENTS Network)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# 实时会话与录制不依赖 Widgets，单独成库，便于测试。
add_library(ps_live STATIC
  live/LiveSession.h
  live/LiveSession.cpp
  live/record/StreamRecorder.h
  live/record/StreamRecorder.cpp
  live/record/McapWriter.h
  live/record/McapWriter.cpp
)
target_include_directories(ps_live PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/plugin/include
  ${PROJECT_SOURCE_DIR}/src/plugin/data_loader/common
)
target_link_libraries(ps_live PUBLIC Qt6::Core ps_kernel ps_loader_common)

add_executable(plotscope_app
  main.cpp
  shell/MainWindow.h
  shell/MainWindow.cpp
  data_input/DataInputPlugin.h
  data_input/DataInputRegistry.h
  data_input/DataInputRegistry.cpp
  data_input/DataInputDialog.h
  data_input/DataInputDialog.cpp
  data_input/ProtocolEditorDialog.cpp
  plugin_host/DialogEngine.h
  plugin_host/DialogEngine.cpp
  device/DeviceDebugDialog.h
  device/DeviceDebugDialog.cpp
  shell/Theme.h
  shell/Theme.cpp
  transform/TransformDialog.h
  transform/TransformDialog.cpp
  canvas/CanvasWidget.h
  canvas/CanvasWidget.cpp
  script/ScriptEngine.h
  script/ScriptEngine.cpp
  resource/icons.qrc
)

target_include_directories(plotscope_app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/plugin/include
)
target_link_libraries(plotscope_app PRIVATE
  Qt6::Widgets Qt6::Network Qt6::Svg ps_kernel ps_plugin_host ps_cache ps_live
  ps_data_loader_csv ps_data_loader_ulog ps_data_loader_apbin ps_data_loader_asc ps_data_loader_blf ps_data_load_mcap ps_data_loader_nmea ps_data_loader_ubx ps_data_loader_json ps_data_loader_binary ps_data_loader_mdf4 ps_data_loader_frame ps_data_loader_rosbag2
  ps_data_source_sim ps_data_source_mavlink ps_data_source_oem7
)
# 外部插件靠宿主导出 ps_* 符号
set_target_properties(plotscope_app PROPERTIES ENABLE_EXPORTS ON)

# 插件 .ui 由通用 DialogEngine 加载；缺失时降级为提示，不阻断构建
find_package(Qt6 QUIET COMPONENTS UiTools)
if(Qt6UiTools_FOUND)
  target_link_libraries(plotscope_app PRIVATE Qt6::UiTools)
  target_compile_definitions(plotscope_app PRIVATE PS_HAVE_UITOOLS=1)
else()
  target_compile_definitions(plotscope_app PRIVATE PS_HAVE_UITOOLS=0)
  message(WARNING "未找到 Qt6 UiTools，插件自带 .ui 将不可用")
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

# Ribbon 结构层（SARibbon，MIT，vendored）
target_link_libraries(plotscope_app PRIVATE SARibbonBar::SARibbonBar)
ps_set_warnings(plotscope_app)

if(WIN32)
  set_target_properties(plotscope_app PROPERTIES WIN32_EXECUTABLE ON)
endif()
add_custom_command(TARGET plotscope_app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${PROJECT_SOURCE_DIR}/src/plugin/data_source/oem7/oem7_database.json
          $<TARGET_FILE_DIR:plotscope_app>/oem7_database.json)
