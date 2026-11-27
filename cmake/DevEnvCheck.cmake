# 配置期开发环境门禁。
#
# 强制项（非 CI）：
#   1. Debian/Ubuntu 上 tools/ubuntu-setup.sh --check 必须返回 0
#      （该脚本自身负责可选的 Git hook 检查）；
#   2. 是 Git 工作树且仓库存在 .githooks 时，core.hooksPath 必须指向它，
#      且 hook 可执行；非 Git 工作树只跳过 hook 检查，环境检查照常执行。
#
# 为什么放在 configure：首次 cmake -S . -B build 一定经过这里，IDE/CMake Tools
# 打开工程也会自动 configure；不放在 build 阶段，避免每次编译重复检查。
#
# CI 跳过：CI 不安装客户端 hook，且环境由 workflow 自己准备；跳过不削弱门禁，
# CI 仍会跑与 pre-push 同口径的检查。
include_guard(GLOBAL)

if(DEFINED ENV{CI} AND NOT "$ENV{CI}" STREQUAL "")
    message(STATUS "[dev-env] CI 环境，跳过本地开发环境检查")
    return()
endif()

find_package(Git QUIET)
if(NOT GIT_EXECUTABLE)
    message(STATUS "[dev-env] 未找到 git，跳过本地开发环境检查")
    return()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" rev-parse --is-inside-work-tree
    OUTPUT_VARIABLE _ps_git_worktree
    ERROR_QUIET
    RESULT_VARIABLE _ps_git_rc)
string(STRIP "${_ps_git_worktree}" _ps_git_worktree)
if(_ps_git_rc EQUAL 0 AND _ps_git_worktree STREQUAL "true")
    set(_ps_is_git_worktree TRUE)
else()
    set(_ps_is_git_worktree FALSE)
    message(STATUS "[dev-env] 非 Git 工作树，跳过 Git hooks 检查（环境检查继续）")
endif()

# 非 Debian/Ubuntu 没有 ubuntu-setup.sh --check 可用，这里保留一个最小 hook 检查。
function(_ps_check_git_hooks_native)
    if(NOT _ps_is_git_worktree)
        return()
    endif()
    set(_ps_hooks_expected "${CMAKE_SOURCE_DIR}/.githooks")
    if(NOT EXISTS "${_ps_hooks_expected}")
        message(STATUS "[dev-env] 未发现 .githooks，跳过 Git hooks 检查")
        return()
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" config --get core.hooksPath
        OUTPUT_VARIABLE _ps_hooks_path
        ERROR_QUIET
        RESULT_VARIABLE _ps_hooks_rc)
    string(STRIP "${_ps_hooks_path}" _ps_hooks_path)
    if(_ps_hooks_rc EQUAL 0 AND NOT _ps_hooks_path STREQUAL "")
        get_filename_component(_ps_hooks_abs "${_ps_hooks_path}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    else()
        set(_ps_hooks_abs "")
    endif()

    if(NOT _ps_hooks_abs STREQUAL _ps_hooks_expected)
        message(FATAL_ERROR
            "[dev-env] Git hooks 未注册：core.hooksPath='${_ps_hooks_path}'\n"
            "[dev-env] 期望指向 ${_ps_hooks_expected}\n"
            "[dev-env] 修复：tools/ubuntu-setup.sh --install")
    endif()
    if(UNIX)
        foreach(_ps_hook pre-commit pre-push)
            execute_process(
                COMMAND test -x "${_ps_hooks_expected}/${_ps_hook}"
                RESULT_VARIABLE _ps_hook_x_rc)
            if(NOT _ps_hook_x_rc EQUAL 0)
                message(FATAL_ERROR
                    "[dev-env] Git hook 不可执行：${_ps_hooks_expected}/${_ps_hook}\n"
                    "[dev-env] 修复：tools/ubuntu-setup.sh --install")
            endif()
        endforeach()
    endif()
    message(STATUS "[dev-env] Git hooks 已注册：core.hooksPath=${_ps_hooks_path}")
endfunction()

# ---- ccache 在 CMake 当前环境里是否真的能用 ----
# ubuntu-setup.sh --check 可能是在仓库外、没有 CCACHE_DIR 的 shell 里跑的；
# 它通过不代表 CMake 这份环境能命中缓存。这里用当前 C 编译器实编两次验收。
function(_ps_ccache_hits ccache out)
    execute_process(
        COMMAND "${ccache}" --print-stats
        OUTPUT_VARIABLE _stats
        ERROR_QUIET
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        set(${out} 0 PARENT_SCOPE)
        return()
    endif()
    string(REPLACE "\t" " " _stats "${_stats}")
    set(_hits 0)
    foreach(_key direct_cache_hit preprocessed_cache_hit)
        if(_stats MATCHES "${_key} ([0-9]+)")
            math(EXPR _hits "${_hits} + ${CMAKE_MATCH_1}")
        endif()
    endforeach()
    set(${out} "${_hits}" PARENT_SCOPE)
endfunction()

function(_ps_check_ccache_usable ccache)
    if(NOT EXISTS "${ccache}")
        message(FATAL_ERROR "[ccache] ccache 不存在：${ccache}")
    endif()

    if(NOT DEFINED ENV{CCACHE_DIR} OR "$ENV{CCACHE_DIR}" STREQUAL "")
        message(FATAL_ERROR
            "[ccache] CCACHE_DIR 未设置；CMake 无法判断缓存落在哪。\n"
            "[ccache] 用 cmake --preset default，或先 . \"$ENV{HOME}/.ps-deps/env.sh\" 再 configure。")
    endif()
    set(_dir "$ENV{CCACHE_DIR}")
    if(NOT IS_DIRECTORY "${_dir}")
        message(FATAL_ERROR "[ccache] CCACHE_DIR 不存在：${_dir}；先跑 tools/ubuntu-setup.sh --install-toolchain")
    endif()

    string(RANDOM LENGTH 8 ALPHABET "0123456789" _suffix)
    set(_write_probe "${_dir}/.ps-write-${_suffix}")
    file(WRITE "${_write_probe}" "ok")
    if(NOT EXISTS "${_write_probe}")
        message(FATAL_ERROR "[ccache] CCACHE_DIR 不可写：${_dir}")
    endif()
    file(REMOVE "${_write_probe}")

    if(DEFINED ENV{CCACHE_DISABLE} AND NOT "$ENV{CCACHE_DISABLE}" STREQUAL "0")
        message(FATAL_ERROR "[ccache] CCACHE_DISABLE=$ENV{CCACHE_DISABLE}，缓存被显式禁用")
    endif()

    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}")
    set(_src "${CMAKE_BINARY_DIR}/.ps-ccache-probe-${_suffix}.c")
    file(WRITE "${_src}" "int ps_ccache_probe_${_suffix}(void) { return 0; }\n")
    if(MSVC)
        set(_compile_args /c "/Fo${CMAKE_BINARY_DIR}/.ps-ccache-probe-${_suffix}.obj")
    else()
        set(_compile_args -c -o "${CMAKE_BINARY_DIR}/.ps-ccache-probe-${_suffix}.o")
    endif()

    _ps_ccache_hits("${ccache}" _before)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "CCACHE_RECACHE=1"
                "${ccache}" "${CMAKE_C_COMPILER}" ${_compile_args} "${_src}"
        OUTPUT_VARIABLE _first_out
        ERROR_VARIABLE _first_err
        RESULT_VARIABLE _first_rc)
    if(NOT _first_rc EQUAL 0)
        message(FATAL_ERROR
            "[ccache] 无法用 ${CMAKE_C_COMPILER} 编译探针（返回 ${_first_rc}）：\n${_first_err}${_first_out}")
    endif()

    execute_process(
        COMMAND "${ccache}" "${CMAKE_C_COMPILER}" ${_compile_args} "${_src}"
        OUTPUT_VARIABLE _second_out
        ERROR_VARIABLE _second_err
        RESULT_VARIABLE _second_rc)
    if(NOT _second_rc EQUAL 0)
        message(FATAL_ERROR
            "[ccache] 第二次编译探针失败（返回 ${_second_rc}）：\n${_second_err}${_second_out}")
    endif()

    _ps_ccache_hits("${ccache}" _after)
    file(REMOVE "${_src}")
    if(NOT _after GREATER _before)
        message(FATAL_ERROR
            "[ccache] 实测没有命中（hits ${_before} -> ${_after}）；"
            "检查 CCACHE_DIR=${_dir} 与 ccache.conf 权限，或 CCACHE_DISABLE。")
    endif()
    message(STATUS "[ccache] 当前 CMake 环境实测命中：hits ${_before} -> ${_after}（CCACHE_DIR=${_dir}）")
endfunction()

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND EXISTS "/etc/debian_version")
    find_program(_ps_bash bash)
    if(NOT _ps_bash)
        message(FATAL_ERROR "[dev-env] 未找到 bash，无法执行 tools/ubuntu-setup.sh --check")
    endif()

    execute_process(
        COMMAND "${_ps_bash}" "${CMAKE_SOURCE_DIR}/tools/ubuntu-setup.sh" --check
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE _ps_env_out
        ERROR_VARIABLE _ps_env_err
        RESULT_VARIABLE _ps_env_rc)
    if(NOT _ps_env_rc EQUAL 0)
        message(FATAL_ERROR
            "[dev-env] tools/ubuntu-setup.sh --check 未通过（返回 ${_ps_env_rc}）：\n"
            "${_ps_env_out}${_ps_env_err}\n"
            "[dev-env] 修复：tools/ubuntu-setup.sh --install；环境检查失败时先加载 PS_DEPS_ROOT/env.sh，或改用 cmake --preset default")
    endif()
    message(STATUS "[dev-env] tools/ubuntu-setup.sh --check 通过")
else()
    message(STATUS "[dev-env] 非 Debian/Ubuntu Linux，跳过 tools/ubuntu-setup.sh --check")
    _ps_check_git_hooks_native()
endif()
