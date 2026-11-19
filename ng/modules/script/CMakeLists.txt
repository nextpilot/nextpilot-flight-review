find_package(Qt6 REQUIRED COMPONENTS Core)

# 复用仓库 deps 下固定版本 Lua 5.4.4 静态库。
set(_ng_lua_root "${CMAKE_SOURCE_DIR}/../deps/lua")
if(NOT EXISTS "${_ng_lua_root}/lib/liblua.a")
    message(FATAL_ERROR "未找到 Lua 静态库: ${_ng_lua_root}/lib/liblua.a，请先运行 tools/bootstrap-deps.sh")
endif()

add_library(ng_lua STATIC IMPORTED GLOBAL)
set_target_properties(ng_lua PROPERTIES
    IMPORTED_LOCATION "${_ng_lua_root}/lib/liblua.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_ng_lua_root}/include"
)

# OBJECT 库：保证自注册 TU 一定参与链接（见 data/CMakeLists.txt 的说明）。
add_library(ng_module_script OBJECT
    ScriptModule.cpp
    LuaEngine.cpp
    TransformEvaluator.cpp
    LuaScriptLibrary.cpp
    ScriptModule.h
    LuaEngine.h
    TransformEvaluator.h
    LuaScriptLibrary.h
)
target_include_directories(ng_module_script PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ng_module_script PUBLIC ng_host ng_context ng_lua Qt6::Core ${CMAKE_DL_LIBS})
set_target_properties(ng_module_script PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
