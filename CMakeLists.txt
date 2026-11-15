cmake_minimum_required(VERSION 3.22)
project(PlotScope VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(Version)
include(Warnings)
include(PsDependencies)

option(PS_BUILD_GUI   "构建 Qt Widgets 主程序" ON)
option(PS_BUILD_TESTS "构建测试"                ON)
option(PS_ENABLE_LUA  "启用 Lua 5.4 脚本（M2 接）" ON)
option(PS_BUILD_GNSS "构建 GNSS 内置组件" ON)
option(PS_BUILD_CUSTOM_PROTOCOL "构建自定义协议内置组件" ON)
set(PS_EXTENSION_DIR "${PROJECT_SOURCE_DIR}/src/extensions/dynamic" CACHE PATH "动态扩展源码目录")
set(PS_EXTENSION_OUTPUT_DIR "${CMAKE_BINARY_DIR}/src/application/extensions" CACHE PATH "开发树扩展产物目录")

add_subdirectory(src)

if(PS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

include(Install)
