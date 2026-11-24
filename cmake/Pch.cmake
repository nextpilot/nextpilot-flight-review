# 预编译头（PCH）开关的实现。产品默认不启用，见根 CMakeLists 的 PS_USE_PCH。
#
# 为什么不在这里写死具体 target：PCH 是否值得开依赖实测数据（与 ccache 的
# 交互可能是负收益），实验阶段需要按 target 逐个试，因此把「开在谁身上」
# 交给 PS_PCH_TARGETS 列表控制，产品默认列表为空。
#
# === 与 ccache 的耦合（本文件最重要的约束，实测数据见 docs/develop/build-performance.md）===
# 开 PCH 后编译命令行会带 -include <build>/cmake_pch.hxx。实测（SARibbonBar，
# 45 个 TU，2 核 3G）：
#   · 默认 sloppiness：ccache 报 Hits 0/0、Misses 0，全部调用判为 Uncacheable，
#     ccache 被整体旁路——冷编 148 s，第二轮 136 s，一点加速都拿不到。
#   · 放行 sloppiness=pch_defines,time_macros 后 ccache 恢复工作，命中 95.45%。
#
# 放行值的传递有两个坑，都已实测：
#   1. set(ENV{...}) 只在 configure 进程内有效，编译时 ccache 是 make 的子进程，
#      继承不到，等于没设。
#   2. 用 launcher 包装（cmake -E env CCACHE_SLOPPINESS=... ccache）可以把 env
#      传到 ccache 进程，但实测该路径下 ccache 仍判 Uncacheable，未能复现命中。
# 因此这里不做自动注入，改为「配置期硬检查 + 明确报错」，把 export 责任交给
# 调用方（tools 下的构建脚本 / CI 配置），避免出现「以为开了实际没开」的静默失效。

function(ps_enable_pch target)
    if(NOT TARGET ${target})
        message(WARNING "[pch] 目标不存在，跳过：${target}")
        return()
    endif()

    get_target_property(_type ${target} TYPE)
    if(_type STREQUAL "INTERFACE_LIBRARY" OR _type STREQUAL "UTILITY")
        message(WARNING "[pch] 目标类型不支持 PCH，跳过：${target} (${_type})")
        return()
    endif()

    target_precompile_headers(${target} PRIVATE
        "${CMAKE_SOURCE_DIR}/cmake/pch.hpp")

    if(CMAKE_CXX_COMPILER_LAUNCHER MATCHES "ccache")
        if(NOT "$ENV{CCACHE_SLOPPINESS}" MATCHES "pch_defines")
            message(FATAL_ERROR
                "[pch] 已启用 PCH 但同时启用了 ccache，而 CCACHE_SLOPPINESS 未包含 pch_defines。\n"
                "       该组合下 ccache 会把全部编译判为不可缓存（实测 Hits 0/0），PCH 的代价\n"
                "       白付、ccache 的收益全丢，属于净负收益。\n"
                "       二选一：\n"
                "         export CCACHE_SLOPPINESS=pch_defines,time_macros   # 保留两者\n"
                "         -DPS_USE_CCACHE=OFF                                # 只留 PCH\n"
                "       实测数据见 docs/develop/build-performance.md。")
        endif()
        message(STATUS "[pch] ${target} 已启用 PCH（ccache sloppiness 已放行 pch_defines）")
    else()
        message(STATUS "[pch] ${target} 已启用 PCH（未用 ccache）")
    endif()
endfunction()
