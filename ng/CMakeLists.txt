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

# === Rust kernel（数据地基）===
# Dataset 的数据本体、typed 表、ring、LOD、缓存、滤波全部由 Rust 实现；
# C++ 侧只 include ps_kernel.h 这一张 C ABI 门。这里把仓库的 ps-abi staticlib
# 接进来，路径相对 ng/ 自身，保证新骨架可独立配置。
find_program(NG_CARGO NAMES cargo HINTS "$ENV{HOME}/.cargo/bin")
if(NOT NG_CARGO)
  message(FATAL_ERROR "找不到 cargo：Dataset 由 Rust kernel 管理，请先安装 rustup")
endif()
set(_ng_kernel_dir "${CMAKE_CURRENT_SOURCE_DIR}/../src/application/kernel")
if(WIN32)
  set(_ng_kernel_lib "${_ng_kernel_dir}/target/release/ps_abi.lib")
else()
  set(_ng_kernel_lib "${_ng_kernel_dir}/target/release/libps_abi.a")
endif()
add_custom_target(ng_kernel_cargo ALL
  COMMAND "${NG_CARGO}" build --manifest-path "${_ng_kernel_dir}/Cargo.toml" -p ps-abi --release
  WORKING_DIRECTORY "${_ng_kernel_dir}"
  BYPRODUCTS "${_ng_kernel_lib}"
  COMMENT "cargo build -p ps-abi (release)"
  VERBATIM)
add_library(ng_kernel INTERFACE)
add_dependencies(ng_kernel ng_kernel_cargo)
target_include_directories(ng_kernel INTERFACE "${_ng_kernel_dir}/include")
target_link_libraries(ng_kernel INTERFACE "${_ng_kernel_lib}")
find_package(Threads REQUIRED)
# Rust std 静态链进 C++ 可执行文件时需要的系统库。
target_link_libraries(ng_kernel INTERFACE Threads::Threads ${CMAKE_DL_LIBS} m)

add_subdirectory(core)
add_subdirectory(host)
add_subdirectory(modules/data)
add_subdirectory(modules/plot)
add_subdirectory(modules/script)
add_subdirectory(extensions/csv)
add_subdirectory(app)

enable_testing()
add_subdirectory(tests)
