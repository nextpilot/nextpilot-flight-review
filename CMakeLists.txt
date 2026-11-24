cmake_minimum_required(VERSION 3.22)
project(PlotScope VERSION 0.1.0 LANGUAGES C CXX)

# 版本号与告警口径（Version 只读 Git describe，Warnings 只定义 ps_set_warnings 函数）。
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(Version)
include(Warnings)

# 第三方 C 依赖（zlib / sqlite3）：优先系统或 -DPS_DEPS_ROOT 前缀，
# 都没有且 PS_DEPS_FETCH=ON 时按固定 URL 拉源码编静态库——Windows 上没有
# 包管理器也能一次 configure 到位。必须在 src/ 之前：toolbox 里 mdf/rosbag2 要链它。
include(Dependencies)

# 编译缓存：命中即跳过整段编译，2 核机器收益最明显。
# 探测不到就静默跳过——没装 ccache 的机器必须照常可编。
option(PS_USE_CCACHE "探测 ccache 并启用编译缓存" ON)
if(PS_USE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
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

# === 边界守卫（configure 期硬失败）===
# 必须在所有 target 定义之后调用，才能检查其链接关系。
# 规则与豁免说明见 cmake/LibBoundaryGuard.cmake 顶部及 AGENTS.md §5。
include("${CMAKE_SOURCE_DIR}/cmake/LibBoundaryGuard.cmake")
ps_lib_guard_boundary()

# 全层依赖方向守卫：层定义见 src/LAYERS.md，拦跨层倒挂与同层横向。
include("${CMAKE_SOURCE_DIR}/cmake/LayerContractGuard.cmake")
ps_layer_guard_contract()
