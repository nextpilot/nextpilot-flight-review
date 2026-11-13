cmake_minimum_required(VERSION 3.22)
project(PlotScope VERSION 0.1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(Version)
include(Warnings)

option(PS_BUILD_GUI   "构建 Qt Widgets 主程序" ON)
option(PS_BUILD_TESTS "构建测试"                ON)
option(PS_ENABLE_LUA  "启用 Lua 5.4 脚本（M2 接）" ON)
set(PS_PLUGIN_DIR "${PROJECT_SOURCE_DIR}/src/plugins" CACHE PATH "官方插件目录")

add_subdirectory(src)

if(PS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

include(Install)
