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

# Core：所有层都要。Widgets/Svg：仅 GUI 层需要（CLI/headless 不链接）。
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Svg)

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
  BYPRODUCTS "${_ng_kernel_lib}")
add_library(ng_kernel INTERFACE)
add_dependencies(ng_kernel ng_kernel_cargo)
target_include_directories(ng_kernel INTERFACE "${_ng_kernel_dir}/include")
target_link_libraries(ng_kernel INTERFACE "${_ng_kernel_lib}")
find_package(Threads REQUIRED)
# Rust std 静态链进 C++ 可执行文件时需要的系统库。
# libm / libdl 是 POSIX 专有：Windows 上不存在 m.lib，CMAKE_DL_LIBS 也为空，
# 无条件链接会让 MSVC 报 LNK1181 无法打开 "m.lib"。
target_link_libraries(ng_kernel INTERFACE Threads::Threads)
if(UNIX)
  target_link_libraries(ng_kernel INTERFACE ${CMAKE_DL_LIBS} m)
endif()
if(WIN32)
  # Rust std 的 Windows 后端会引用 winsock 与 ntdll 的原生符号，
  # 静态链进 C++ 可执行文件时必须显式补上这些系统库，否则 LNK2019。
  target_link_libraries(ng_kernel INTERFACE ws2_32 ntdll userenv bcrypt advapi32 ole32 oleaut32)
endif()

add_subdirectory(core)
add_subdirectory(recording)
add_subdirectory(host)

# === 协议逻辑库（顺序段协议解码内核）===
# 纯逻辑（只 Qt Core），SPI 解析器与协议编辑器共用；放在 extensions 之前。
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/libs/protocol" "${CMAKE_CURRENT_BINARY_DIR}/libs/protocol")
add_subdirectory(modules/data)
add_subdirectory(modules/plot)
add_subdirectory(modules/script)
add_subdirectory(modules/protocol)
add_subdirectory(modules/analysis)
add_subdirectory(modules/settings)
# 工具箱按业务领域组织：common / autopilot / automotive / robotics / gnss。
# 每个领域一个包，静态自注册（NG_REGISTER_PACK）。
add_subdirectory(extensions/common)
add_subdirectory(extensions/autopilot)
add_subdirectory(extensions/automotive)
add_subdirectory(extensions/robotics)
add_subdirectory(extensions/gnss)
add_subdirectory(extensions/registry)
add_subdirectory(app)

# === GUI 第三方组件（复用仓库已 vendored 的源码，不联网、不引新依赖）===
# SARibbon（MIT）—— ribbon 结构层
# Qt-ADS（LGPL-2.1）—— 停靠系统，以共享库方式链接（老仓库同为 SHARED）
set(SARIBBON_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(SARIBBON_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS               OFF CACHE BOOL "" FORCE)
set(SARIBBON_USE_FRAMELESS_LIB OFF CACHE BOOL "" FORCE)
set(SARIBBON_INSTALL_IN_CURRENT_DIR OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../src/3rdparty/saribbon"
                 "${CMAKE_CURRENT_BINARY_DIR}/3rdparty/saribbon" EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../src/3rdparty/ads"
                 "${CMAKE_CURRENT_BINARY_DIR}/3rdparty/ads" EXCLUDE_FROM_ALL)

# === 独立绘图库（fastplot）===
# 只依赖 Qt Widgets，不依赖 ng_core；只允许 GUI 侧链接。
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/libs/fastplot" "${CMAKE_CURRENT_BINARY_DIR}/libs/fastplot")

add_subdirectory(gui)

enable_testing()
add_subdirectory(tests)
