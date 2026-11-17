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
option(PS_BUILD_BENCHMARKS "构建绘图性能基准" OFF)
set(PS_EXTENSION_DIR "${PROJECT_SOURCE_DIR}/src/extensions/dynamic" CACHE PATH "动态扩展源码目录")
# 产物目录是随目录布局派生的默认值，不写进 cache：否则目录重构后旧 cache 会把扩展继续投到已废弃路径。
if(NOT DEFINED PS_EXTENSION_OUTPUT_DIR)
  set(PS_EXTENSION_OUTPUT_DIR "${CMAKE_BINARY_DIR}/src/application/extensions")
endif()
set(PS_EXTENSION_OUTPUT_DIR "${PS_EXTENSION_OUTPUT_DIR}" CACHE PATH "开发树扩展产物目录" FORCE)

add_subdirectory(src)

if(PS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(PS_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks/plot_bench)
endif()

include(Install)
