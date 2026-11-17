cmake_minimum_required(VERSION 3.22)
project(PlotScopeNG VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 复用仓库 deps/Qt：既有预设未设置 prefix 时自动指向 deps 下固定版本。
if(NOT DEFINED CMAKE_PREFIX_PATH OR CMAKE_PREFIX_PATH STREQUAL "")
  set(_ng_deps_qt "${CMAKE_CURRENT_SOURCE_DIR}/../deps/Qt/6.2.4/gcc_64")
  if(EXISTS "${_ng_deps_qt}/lib/cmake/Qt6")
    set(CMAKE_PREFIX_PATH "${_ng_deps_qt}")
  endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core)

add_subdirectory(core)
add_subdirectory(host)
add_subdirectory(modules/data)
add_subdirectory(modules/plot)
add_subdirectory(modules/script)
add_subdirectory(extensions/csv)
add_subdirectory(app)

enable_testing()
add_subdirectory(tests)
