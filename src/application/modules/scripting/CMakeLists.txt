# 脚本子系统：控制台（REPL）与自动化（Job + 事件）共用一套引擎。
# 依赖边界见 AGENTS.md §3：只依赖 shell 宿主接口、modules/runtime、ps_kernel C ABI、Qt、Lua。
add_library(ps_application_scripting OBJECT
  include/scripting/ScriptingModule.h
  include/scripting/ScriptEngine.h
  manager/ScriptingModule.cpp
  lua/ScriptEngine.cpp
  lua/ScriptLibrary.h
  lua/ScriptLibrary.cpp
  lua/LuaAppApi.h
  lua/LuaAppApi.cpp
  lua/LuaCalcApi.h
  lua/LuaCalcApi.cpp
  include/scripting/ConsolePanel.h
  lua/ConsolePanel.cpp
  include/scripting/ScratchPlot.h
  lua/ScratchPlot.cpp
  include/scripting/ScriptConsole.h
  lua/ScriptConsole.cpp
)
set_target_properties(ps_application_scripting PROPERTIES AUTOMOC ON)
target_include_directories(ps_application_scripting PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/application
  ${PROJECT_SOURCE_DIR}/src/application/modules/runtime/include
  ${PROJECT_SOURCE_DIR}/src/application/kernel/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
# lua/ 是模块内部实现目录，不对外暴露
target_include_directories(ps_application_scripting PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/lua
)
target_link_libraries(ps_application_scripting PUBLIC ps_host ps_kernel Qt6::Widgets)

if(PS_ENABLE_LUA)
  find_package(Lua 5.4 QUIET)
  if(LUA_FOUND)
    target_include_directories(ps_application_scripting PRIVATE ${LUA_INCLUDE_DIR})
    target_link_libraries(ps_application_scripting PUBLIC ${LUA_LIBRARIES})
    target_compile_definitions(ps_application_scripting PRIVATE PS_HAVE_LUA=1)
  endif()
endif()

ps_set_warnings(ps_application_scripting)
