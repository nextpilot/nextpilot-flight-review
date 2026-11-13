find_package(Qt6 REQUIRED COMPONENTS Network)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(plotscope_app
  main.cpp
  shell/MainWindow.h
  shell/MainWindow.cpp
  loader/LoaderPlugin.h
  loader/LoaderRegistry.h
  loader/LoaderRegistry.cpp
  loader/LoaderOptionsDialog.h
  loader/LoaderOptionsDialog.cpp
  loader/DialogEngine.h
  loader/DialogEngine.cpp
  source/LiveStream.h
  source/LiveStream.cpp
  source/SourcePlugin.h
  source/SourceRegistry.h
  source/SourceRegistry.cpp
  source/SourceConfigDialog.h
  source/SourceConfigDialog.cpp
  source/SourceDebugDialog.h
  source/SourceDebugDialog.cpp
  shell/Theme.h
  shell/Theme.cpp
  transform/TransformDialog.h
  transform/TransformDialog.cpp
  plot/CanvasWidget.h
  plot/CanvasWidget.cpp
  script/ScriptEngine.h
  script/ScriptEngine.cpp
  res/icons.qrc
)

target_include_directories(plotscope_app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/include
)
target_link_libraries(plotscope_app PRIVATE
  Qt6::Widgets Qt6::Network Qt6::Svg ps_kernel ps_runtime ps_dataset_cache
  ps_data_loader_csv ps_data_loader_ulog ps_data_loader_apbin ps_data_loader_asc ps_data_loader_blf
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
    message(WARNING "未找到 Lua 5.4，脚本面板运行时会提示缺少 liblua5.4-dev")
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
          ${PROJECT_SOURCE_DIR}/src/plugins/data_source/oem7/oem7_database.json
          $<TARGET_FILE_DIR:plotscope_app>/oem7_database.json)
