cmake_minimum_required(VERSION 3.24)
project(PlotScope VERSION 0.1.0 LANGUAGES C CXX)

# 版本号与告警口径（Version 只读 Git describe，Warnings 只定义 ps_set_warnings 函数）。
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(Version)
include(Warnings)

# 可复现路径前缀：把源码树与构建目录的绝对路径规整成稳定值。
# 目的有两个：
#   1. ccache 的哈希里带 -I / -o / __FILE__ 的绝对路径。换 checkout 目录、
#      或换 build 与 build-debug 时这些前缀就变，缓存立刻全 miss。
#      CCACHE_BASEDIR（见 CMakePresets.json）只能相对化工作目录相关部分，
#      编译期真写进产物的路径仍要靠这里抹平。
#   2. Debug 的 DWARF 不再嵌机器相关前缀，换机器后调试器仍能定位到源码。
# -ffile-prefix-map 同时作用于 __FILE__ 与调试信息（GCC 8+ / Clang 10+）。
# 两个 build 目录统一映射成 "build"，正是为了让 Release 与 Debug 共用缓存条目之外
# 的路径前缀保持一致——注意编译类型不同（-O2 vs -g）仍各是一套哈希，不能互相命中。
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
        "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build")
elseif(MSVC)
    add_compile_options(
        "/pathmap:${CMAKE_SOURCE_DIR}=."
        "/pathmap:${CMAKE_BINARY_DIR}=build")
endif()

# 第三方 C 依赖（zlib / sqlite3）：优先系统或 -DPS_DEPS_ROOT 前缀，
# 都没有且 PS_DEPS_FETCH=ON 时按固定 URL 拉源码编静态库——Windows 上没有
# 包管理器也能一次 configure 到位。必须在 src/ 之前：toolbox 里 mdf/rosbag2 要链它。
include(Dependencies)

# 编译缓存：命中即跳过整段编译，2 核机器收益最明显。
# 探测不到就静默跳过——没装 ccache 的机器必须照常可编。
option(PS_USE_CCACHE "探测 ccache 并启用编译缓存" ON)
if(PS_USE_CCACHE)
    # 不写 cache：旧 build 目录里残留的绝对路径在换机/换 HOME 后失效，
    # 会让 launcher 指向不存在的文件（表现为 /bin/sh: ccache: not found）。
    find_program(CCACHE_PROGRAM ccache NO_CACHE)
    if(CCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        message(STATUS "[ccache] 已启用：${CCACHE_PROGRAM}")
    else()
        message(STATUS "[ccache] 未找到 ccache，使用直连编译器")
    endif()
endif()

# 测试目标开关：日常迭代只编主程序，验收时开回来。
# 关闭时 ctest 无用例可跑，这是预期行为。
option(PS_BUILD_TESTS "构建测试目标（关闭可显著缩短日常迭代构建）" ON)

# 顶层薄壳：src/ 工程本体 + 根 tests/ 测试。
# 全部构建逻辑在 src/CMakeLists.txt（唯一事实源）。
enable_testing()
add_subdirectory(src)
if(PS_BUILD_TESTS)
    add_subdirectory(tests)
else()
    message(STATUS "[tests] PS_BUILD_TESTS=OFF，跳过测试目标")
endif()

# 预编译头（PCH）：实验开关，产品默认关闭。
# 与 ccache 存在耦合（可能负收益），是否启用待 [I4] 实测数据决定。
# PS_PCH_TARGETS 决定开在哪些 target 上，默认空——即默认行为完全不变。
option(PS_USE_PCH "为目标启用预编译头（实验开关）" OFF)
set(PS_PCH_TARGETS "" CACHE STRING "应用 PCH 的目标列表（分号分隔，实验用）")
if(PS_USE_PCH)
    include(Pch)
    if(PS_PCH_TARGETS)
        foreach(_pch_target IN LISTS PS_PCH_TARGETS)
            ps_enable_pch(${_pch_target})
        endforeach()
    else()
        message(WARNING "[pch] PS_USE_PCH=ON 但 PS_PCH_TARGETS 为空，未启用任何 target")
    endif()
endif()

# === 边界守卫（configure 期硬失败）===
# 必须在所有 target 定义之后调用，才能检查其链接关系。
# 规则与豁免说明见 cmake/LibBoundaryGuard.cmake 顶部及 AGENTS.md §5。
include("${CMAKE_SOURCE_DIR}/cmake/LibBoundaryGuard.cmake")
ps_lib_guard_boundary()

# 全层依赖方向守卫：层定义见 src/LAYERS.md，拦跨层倒挂与同层横向。
include("${CMAKE_SOURCE_DIR}/cmake/LayerContractGuard.cmake")
ps_layer_guard_contract()

# 三条铁律源码级守卫（A4a）：include 黑名单 + 文件夹归属，
# 专拦"纸面合规、源码偷编"（ShellLinkGuard 只看 target 链接面，看不见 include）。
include("${CMAKE_SOURCE_DIR}/cmake/ArchitectureGuard.cmake")
ps_arch_guard_contract()

# === 格式化工具（开发机用）===
# 只提供显式的 `format` target（变更行格式化），不挂进默认构建：
# 构建期静默改写源码会让 lint 的 format 门禁失效，也会在并发编译下制造读写竞态。
# 详见 cmake/Format.cmake 顶部说明。
include("${CMAKE_SOURCE_DIR}/cmake/Format.cmake")
