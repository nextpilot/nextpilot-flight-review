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
)
target_link_libraries(plotscope_app PRIVATE Qt6::Widgets Qt6::Svg ps_kernel ps_loader_csv)

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
