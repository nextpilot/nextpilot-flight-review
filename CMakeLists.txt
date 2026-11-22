cmake_minimum_required(VERSION 3.22)
project(PlotScope VERSION 0.1.0 LANGUAGES C CXX)

# 顶层薄壳：src/ 工程本体 + 根 tests/ 测试。
# 全部构建逻辑在 src/CMakeLists.txt（唯一事实源，源自原 ng 工程）。
enable_testing()
add_subdirectory(src)
add_subdirectory(tests)

# === 边界守卫（configure 期硬失败）===
# 必须在所有 target 定义之后调用，才能检查其链接关系。
# 规则与豁免说明见 cmake/LibBoundaryGuard.cmake 顶部及 AGENTS.md §5。
include("${CMAKE_SOURCE_DIR}/cmake/LibBoundaryGuard.cmake")
ps_lib_guard_boundary()

# 全层依赖方向守卫：层定义见 src/LAYERS.md，拦跨层倒挂与同层横向。
include("${CMAKE_SOURCE_DIR}/cmake/LayerContractGuard.cmake")
ps_layer_guard_contract()
