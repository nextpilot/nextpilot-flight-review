add_library(ps_builtin_script_component OBJECT
  ScriptComponent.cpp
  ScriptComponent.h
  ScriptEngine.cpp
  ScriptEngine.h
)
set_target_properties(ps_builtin_script_component PROPERTIES AUTOMOC ON)
target_include_directories(ps_builtin_script_component PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/base
  ${PROJECT_SOURCE_DIR}/src/base/modules/runtime/include
  ${PROJECT_SOURCE_DIR}/src/base/modules/shell/ui
  ${PROJECT_SOURCE_DIR}/src/base/modules/data_source/include
  ${PROJECT_SOURCE_DIR}/src/base/modules/plotting/include
  ${PROJECT_SOURCE_DIR}/src/base/modules/plotting/canvas
)
target_link_libraries(ps_builtin_script_component PUBLIC ps_host Qt6::Widgets)
if(PS_ENABLE_LUA)
  find_package(Lua 5.4 QUIET)
  if(LUA_FOUND)
    target_include_directories(ps_builtin_script_component PRIVATE ${LUA_INCLUDE_DIR})
    target_link_libraries(ps_builtin_script_component PUBLIC ${LUA_LIBRARIES})
    target_compile_definitions(ps_builtin_script_component PRIVATE PS_HAVE_LUA=1)
  endif()
endif()
