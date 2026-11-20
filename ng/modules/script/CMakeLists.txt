find_package(Qt6 REQUIRED COMPONENTS Core)

# Lua 走仓库内的 src/3rdparty/lua-5.4（源码编库，目标 lua::lua）：
# 路径由 ng 顶层的 NG_THIRDPARTY_DIR 统一给出，和平台、deps/ 布局都无关。
if(NOT TARGET lua)
    message(FATAL_ERROR "缺少 lua::lua 目标：ng/CMakeLists.txt 需要先引入 src/3rdparty/lua-5.4")
endif()

# 把 12 个内置过滤器的 Luau 源内嵌进二进制（raw string），无运行时文件/Qt resource 依赖。
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/builtin_filters.luau" BUNDLED_FILTERS_LUA)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/BundledFilters.cpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/BundledFilters.cpp"
    @ONLY
)

# OBJECT 库：保证自注册 TU 一定参与链接（见 data/CMakeLists.txt 的说明）。
add_library(ng_module_script OBJECT
    ScriptModule.cpp
    LuaEngine.cpp
    FilterEngine.cpp
    FilterCatalogue.cpp
    MarkerEngine.cpp
    TransformEvaluator.cpp
    LuaScriptLibrary.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/BundledFilters.cpp
    ScriptModule.h
    LuaEngine.h
    FilterEngine.h
    FilterCatalogue.h
    MarkerEngine.h
    TransformEvaluator.h
    LuaScriptLibrary.h
)
target_include_directories(ng_module_script PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ng_module_script PUBLIC ng_host ng_context lua::lua Qt6::Core ${CMAKE_DL_LIBS})
set_target_properties(ng_module_script PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
