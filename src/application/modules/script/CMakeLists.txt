find_package(Qt6 REQUIRED COMPONENTS Core)

# Lua 走仓库内的 3rdparty/lua-5.4（源码编库，目标 lua::lua）：
# 路径由主工程顶层的 PS_THIRDPARTY_DIR 统一给出，和平台、deps/ 布局都无关。
if(NOT TARGET lua)
    message(FATAL_ERROR "缺少 lua::lua 目标：主工程 CMakeLists.txt 需要先引入 3rdparty/lua-5.4")
endif()

# 把 12 个内置过滤器的 Luau 源内嵌进二进制（raw string），无运行时文件/Qt resource 依赖。
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/builtin_filters.luau" BUNDLED_FILTERS_LUA)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/BundledFilters.cpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/BundledFilters.cpp"
    @ONLY
)

# OBJECT 库：保证自注册 TU 一定参与链接（见 data/CMakeLists.txt 的说明）。
add_library(ps_module_script OBJECT
    ScriptModule.cpp
    LuaEngine.cpp
    BatchRunner.cpp
    PythonBridge.cpp
    PythonHandler.cpp
    FilterEngine.cpp
    FilterCatalogue.cpp
    MarkerEngine.cpp
    TransformEvaluator.cpp
    LuaScriptLibrary.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/BundledFilters.cpp
    ScriptModule.h
    LuaEngine.h
    BatchRunner.h
    PythonBridge.h
    PythonHandler.h
    FilterEngine.h
    FilterCatalogue.h
    MarkerEngine.h
    TransformEvaluator.h
    LuaScriptLibrary.h
)
target_include_directories(ps_module_script PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ps_module_script PUBLIC ps_host ps_context lua::lua Qt6::Core ${CMAKE_DL_LIBS})
set_target_properties(ps_module_script PROPERTIES AUTOMOC ON CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)

# Python 桥模块内嵌 qrc：运行时解压到临时目录，PYTHONPATH 指过去（python -m 启动）。
qt_add_resources(ps_module_script "ps_python_bridge"
    PREFIX "/script"
    BASE resources
    FILES resources/nextpilot_bridge.py
)

# 呈现层（工作台停靠面板的 QWidget 实现）：单独一个产物，
# 只被 GUI 与 GUI 测试链接——headless（CLI）不碰 Widgets。
# 见 gui/CMakeLists.txt 的说明。
add_subdirectory(gui)
