cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 依赖前缀：用 PS_DEPS_PREFIX（可持久化的独立 Qt/Lua 安装，不被系统镜像恢复）。
# 该前缀下是标准 usr/ 布局（include、lib、lib/cmake、lib/qt6/plugins），
# 一次性铺好后重装系统也不用重新 apt install。前缀由 tools/ubuntu-setup.sh --install --qt 生成。
if(DEFINED ENV{PS_DEPS_PREFIX} AND EXISTS "$ENV{PS_DEPS_PREFIX}")
  set(PS_DEPS_PREFIX "$ENV{PS_DEPS_PREFIX}")
endif()
if(PS_DEPS_PREFIX)
  list(PREPEND CMAKE_PREFIX_PATH "${PS_DEPS_PREFIX}/usr/lib/x86_64-linux-gnu/cmake")
  link_directories("${PS_DEPS_PREFIX}/usr/lib/x86_64-linux-gnu")
  # 运行时（测试/程序）从同一个前缀加载 Qt，不依赖系统里有没有装。
  set(CMAKE_BUILD_RPATH "${PS_DEPS_PREFIX}/usr/lib/x86_64-linux-gnu")
endif()

# 复用仓库 deps/Qt：既有预设未设置 prefix 时自动指向 deps 下固定版本。
if(NOT DEFINED CMAKE_PREFIX_PATH OR CMAKE_PREFIX_PATH STREQUAL "")
  set(_ps_deps_qt "${CMAKE_SOURCE_DIR}/deps/Qt/6.2.4/gcc_64")
  if(EXISTS "${_ps_deps_qt}/lib/cmake/Qt6")
    set(CMAKE_PREFIX_PATH "${_ps_deps_qt}")
  endif()
endif()

# Core：所有层都要。Widgets/Svg：仅 GUI 层需要（CLI/headless 不链接）。
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Svg)

# === Rust kernel（数据地基）===
# Dataset 的数据本体、typed 表、ring、LOD、缓存、滤波全部由 Rust 实现；
# C++ 侧只 include ps_kernel.h 这一张 C ABI 门。ps-abi staticlib 路径相对 src/ 自身。
find_program(PS_CARGO NAMES cargo HINTS "$ENV{HOME}/.cargo/bin")
if(NOT PS_CARGO)
  message(FATAL_ERROR "找不到 cargo：Dataset 由 Rust kernel 管理，请先安装 rustup")
endif()
set(_ps_kernel_dir "${CMAKE_CURRENT_SOURCE_DIR}/kernel")
if(WIN32)
  set(_ps_kernel_lib "${_ps_kernel_dir}/target/release/ps_abi.lib")
else()
  set(_ps_kernel_lib "${_ps_kernel_dir}/target/release/libps_abi.a")
endif()
add_custom_target(ps_kernel_cargo ALL
  COMMAND "${PS_CARGO}" build --manifest-path "${_ps_kernel_dir}/Cargo.toml" -p ps-abi --release
  WORKING_DIRECTORY "${_ps_kernel_dir}"
  BYPRODUCTS "${_ps_kernel_lib}")
add_library(ps_kernel INTERFACE)
add_dependencies(ps_kernel ps_kernel_cargo)
target_include_directories(ps_kernel INTERFACE "${_ps_kernel_dir}/include")
target_link_libraries(ps_kernel INTERFACE "${_ps_kernel_lib}")
find_package(Threads REQUIRED)
# Rust std 静态链进 C++ 可执行文件时需要的系统库。
# libm / libdl 是 POSIX 专有：Windows 上不存在 m.lib，CMAKE_DL_LIBS 也为空，
# 无条件链接会让 MSVC 报 LNK1181 无法打开 "m.lib"。
target_link_libraries(ps_kernel INTERFACE Threads::Threads)
if(UNIX)
  target_link_libraries(ps_kernel INTERFACE ${CMAKE_DL_LIBS} m)
endif()
if(WIN32)
  # Rust std 的 Windows 后端会引用 winsock 与 ntdll 的原生符号，
  # 静态链进 C++ 可执行文件时必须显式补上这些系统库，否则 LNK2019。
  target_link_libraries(ps_kernel INTERFACE ws2_32 ntdll userenv bcrypt advapi32 ole32 oleaut32)
endif()

# === 公共基础头（header-only，最底层）===
# Result.h 归位到这里：dataset / registry / intent / uischema 等机制库共享，
# 又不让 core 与机制库形成循环依赖。必须先于 core 定义。
include("${CMAKE_SOURCE_DIR}/cmake/RuntimeDeploy.cmake")  # 提供 ps_deploy_windows_runtime_all，必须早于任何 add_subdirectory

# === 第三方源码统一根：仓库内 3rdparty ===
# 必须早于 libs：protocol 与 modules/script 都要链 lua::lua。
# 各子目录只引用 ${PS_THIRDPARTY_DIR}/<库>，不再各自拼 ../../../deps 之类的写死路径；
# 换落点、换目录布局只改这一处。
set(PS_THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty"
    CACHE PATH "第三方锁版本库源码根目录")

# Lua 5.4.4（MIT）—— 脚本后端，源码编库（目标 lua::lua），Linux/Windows 吃同一份输入
add_subdirectory("${PS_THIRDPARTY_DIR}/lua-5.4" "${CMAKE_CURRENT_BINARY_DIR}/3rdparty/lua"
                 EXCLUDE_FROM_ALL)

add_subdirectory(libs/foundation)

# Arrow 导入导出通道（nanoarrow，纯转换库；不依赖 Qt/kernel/上层）。
add_subdirectory(libs/arrow)

# === 注册表最小核（header-only，先于 core 定义）===
add_subdirectory(libs/registry)

# === 数据地基（可独立链接，先于 core 定义）===
# Dataset/DataRepository 只依赖 Rust kernel + Qt Core + foundation；
# core  PUBLIC 链它，消费者得以只链 dataset 而不吞整个骨架。
add_subdirectory(libs/dataset)

# 意图契约库：Intent/Job/执行器抽象，只依赖 foundation + Qt Core。
add_subdirectory(libs/intent)

# 命令/键位/when 求值：纯数据契约库，headless 与 GUI 共用（契约 D7/D15）。
add_subdirectory(libs/command)

# schema 契约与渲染：ParamSpec metadata -> FormSchema -> QWidget。
add_subdirectory(libs/uischema)

# 消息解析契约与 C ABI（IMessageParser / ParserHub / ps_*_spi）。
add_subdirectory(libs/parser)

# 运行态容器：AppContext + 设置 + 摄取 + 交互注册（唯一实例在 app）。
add_subdirectory(libs/context)

# 器官契约：IModule/ModuleInfo/UiContext（纯接口面，运行时在 host）。
add_subdirectory(libs/module)

# B7 core 已消亡：类型全部迁入 libs/*，消费者直接链对应库。
add_subdirectory(libs/recording)
add_subdirectory(application/host)

# === 协议逻辑库（顺序段协议解码内核）===
# 纯逻辑（只 Qt Core），SPI 解析器与协议编辑器共用；放在 toolbox 之前。
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/libs/protocol" "${CMAKE_CURRENT_BINARY_DIR}/libs/protocol")
add_subdirectory(application/modules/data)
add_subdirectory(application/modules/plot)
add_subdirectory(application/modules/script)
add_subdirectory(application/modules/protocol)
add_subdirectory(application/modules/analysis)
add_subdirectory(application/modules/settings)
add_subdirectory(application/modules/workspace)
add_subdirectory(application/modules/report)
# 工具箱按业务领域组织：common / autopilot / automotive / robotics / gnss。
# 每个领域一个包，静态自注册（PS_REGISTER_PACK）。
add_subdirectory(toolbox/common)
add_subdirectory(toolbox/autopilot)
add_subdirectory(toolbox/autopilot/apbin)
add_subdirectory(toolbox/automotive)
add_subdirectory(toolbox/automotive/mdf)
add_subdirectory(toolbox/automotive/blf)
add_subdirectory(toolbox/robotics)
add_subdirectory(toolbox/robotics/rosbag2)
add_subdirectory(toolbox/gnss)
# 工具箱装配层（Toolbox/PluginScanner）已并入 host：装配是组合根能力，
# 不属领域包。见 host/CMakeLists.txt。
add_subdirectory(application/app)

# === GUI 第三方组件（复用仓库已 vendored 的源码，不联网、不引新依赖）===
# SARibbon（MIT）—— ribbon 结构层
# Qt-ADS（LGPL-2.1）—— 停靠系统，以共享库方式链接（老仓库同为 SHARED）
set(SARIBBON_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(SARIBBON_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS               OFF CACHE BOOL "" FORCE)
set(SARIBBON_USE_FRAMELESS_LIB OFF CACHE BOOL "" FORCE)
set(SARIBBON_INSTALL_IN_CURRENT_DIR OFF CACHE BOOL "" FORCE)
add_subdirectory("${PS_THIRDPARTY_DIR}/saribbon"
                 "${CMAKE_CURRENT_BINARY_DIR}/3rdparty/saribbon" EXCLUDE_FROM_ALL)
add_subdirectory("${PS_THIRDPARTY_DIR}/ads"
                 "${CMAKE_CURRENT_BINARY_DIR}/3rdparty/ads" EXCLUDE_FROM_ALL)

# === 独立绘图库（fastplot）===
# 只依赖 Qt Widgets，不依赖 ps_context；只允许 GUI 侧链接。
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/libs/fastplot" "${CMAKE_CURRENT_BINARY_DIR}/libs/fastplot")

add_subdirectory(application/gui)
