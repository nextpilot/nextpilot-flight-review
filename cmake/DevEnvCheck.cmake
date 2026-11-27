# 配置期开发环境门禁（小白零门槛：clone 后只敲 cmake，环境自动装好配齐）。
#
# 根路径约定：开发环境根目录默认 $HOME/.ps-deps，可用环境变量 PS_DEPS_ROOT
# 覆盖（tools/ubuntu-setup.sh 与 env.sh 同样遵守该变量）；本文件不硬编码路径。
#
# 行为分层（非 CI）：
#   0. CI=1 → 跳过（CI 环境由 workflow 自己准备）；
#   1. 环境没装（根目录下没有 env.sh）→ 自动跑 tools/ubuntu-setup.sh --install
#      （装进 PS_DEPS_ROOT 指定的根，首次数分钟，取决于网络）；
#   2. 变量没加载（忘了 source / 不用 preset）→ 自动加载 env.sh 并白名单导入
#      进程环境；已 source（PS_DEPS_ENV_LOADED 哨兵 + 关键变量齐全）则零噪音跳过；
#   3. git hook 没注册 / 不可执行 → 自动注册（git config）+ 自动 chmod；
#   4. PS_QT_DIR / CCACHE_DIR 缺失 → 自动推导；Qt / ccache 缺失损坏 → 自动补装
#      （--install-qt / --install-toolchain），补完复查；
#   5. CMAKE_PREFIX_PATH 兜底注入（防 find_package 静默回退系统 Qt）；
#      cache_dir 钉入 ccache.conf（构建 shell 没 source 也能落统一缓存）；
#   6. ubuntu-setup.sh --check 全量校验，未通过自动 --install 修复后复查；
#   7. ccache 用当前编译器实编两次验收（见 CMakeLists 的 PS_REQUIRE_CCACHE）。
#
# 自动化护栏：每个安装步骤在一次 configure 里至多执行一次；失败立即 FATAL 并
# 展示输出尾部（附手动命令），绝不循环重试。为什么放在 configure：首次 cmake
# 一定经过这里，IDE（CMake Tools 等）打开工程也会自动 configure。
include_guard(GLOBAL)

if(DEFINED ENV{CI} AND NOT "$ENV{CI}" STREQUAL "")
    message(STATUS "[dev-env] CI 环境，跳过本地开发环境检查")
    return()
endif()

# ---- 开发环境根：PS_DEPS_ROOT 可覆盖，默认 $HOME/.ps-deps ----
function(_ps_deps_root out)
    if(DEFINED ENV{PS_DEPS_ROOT} AND NOT "$ENV{PS_DEPS_ROOT}" STREQUAL "")
        set(${out} "$ENV{PS_DEPS_ROOT}" PARENT_SCOPE)
    else()
        set(${out} "$ENV{HOME}/.ps-deps" PARENT_SCOPE)
    endif()
endfunction()

# ---- 统一的 ubuntu-setup.sh 调用助手 ----
# 结果放 _ps_setup_out / _ps_setup_rc（PARENT_SCOPE）。
function(_ps_setup)
    find_program(_ps_bash bash QUIET)
    if(NOT _ps_bash)
        message(FATAL_ERROR "[dev-env] 未找到 bash，无法执行 tools/ubuntu-setup.sh ${ARGV}")
    endif()
    execute_process(
        COMMAND "${_ps_bash}" "${CMAKE_SOURCE_DIR}/tools/ubuntu-setup.sh" ${ARGV}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE _ps_out
        ERROR_VARIABLE _ps_err
        RESULT_VARIABLE _ps_rc)
    set(_ps_setup_out "${_ps_out}${_ps_err}" PARENT_SCOPE)
    set(_ps_setup_rc "${_ps_rc}" PARENT_SCOPE)
endfunction()

# ---- 加载 env.sh 并白名单导入进程环境 ----
# 白名单只收 env.sh 管理的变量（PS_* / CCACHE_* / CMAKE_PREFIX_PATH /
# LD_LIBRARY_PATH / PATH），不重写 cmake 继承的其余进程环境。
function(_ps_import_env env_sh)
    find_program(_ps_env_bash bash QUIET)
    if(NOT _ps_env_bash)
        message(FATAL_ERROR "[dev-env] 未找到 bash，无法自动加载 ${env_sh}")
    endif()
    execute_process(
        # 先清掉防重复哨兵再 source：保证 env.sh 的导出一定发生（父环境可能残留）。
        COMMAND "${_ps_env_bash}" -c "unset PS_DEPS_ENV_LOADED; set -a; . '${env_sh}'; env"
        OUTPUT_VARIABLE _ps_env_dump
        ERROR_VARIABLE _ps_env_err
        RESULT_VARIABLE _ps_env_rc)
    if(NOT _ps_env_rc EQUAL 0)
        message(FATAL_ERROR "[dev-env] 自动加载 ${env_sh} 失败（返回 ${_ps_env_rc}）：\n${_ps_env_err}")
    endif()
    set(_ps_imported "")
    string(REPLACE "\n" ";" _ps_env_lines "${_ps_env_dump}")
    foreach(_ps_line IN LISTS _ps_env_lines)
        if(_ps_line MATCHES "^([A-Za-z_][A-Za-z0-9_]*)=(.*)$")
            # 先保存值：下面的 MATCHES 会覆盖 CMAKE_MATCH_*。
            set(_ps_val "${CMAKE_MATCH_2}")
            set(_ps_key "${CMAKE_MATCH_1}")
            if(_ps_key MATCHES "^(PS_[A-Za-z0-9_]*|CCACHE_[A-Za-z0-9_]*|CMAKE_PREFIX_PATH|LD_LIBRARY_PATH|PATH)$")
                set(ENV{${_ps_key}} "${_ps_val}")
                list(APPEND _ps_imported "${_ps_key}")
            endif()
        endif()
    endforeach()
    set(_ps_imported_list "${_ps_imported}" PARENT_SCOPE)
endfunction()

# ---- 层 1+2：环境没装 → 自动 --install；变量没加载 → 自动导入 ----
# 跳过判据：PS_DEPS_ENV_LOADED 哨兵在（用户 source 过 env.sh）且关键变量齐全
# ——标准路径零噪音。否则从根目录（PS_DEPS_ROOT 可覆盖）加载 env.sh 补齐；
# env.sh 不存在则先自动安装再加载。
function(_ps_bootstrap_env)
    set(_ps_loaded TRUE)
    foreach(_ps_var PS_DEPS_ROOT PS_QT_DIR CCACHE_DIR)
        if(NOT DEFINED ENV{${_ps_var}} OR "$ENV{${_ps_var}}" STREQUAL "")
            set(_ps_loaded FALSE)
        endif()
    endforeach()
    if(_ps_loaded AND DEFINED ENV{PS_DEPS_ENV_LOADED} AND NOT "$ENV{PS_DEPS_ENV_LOADED}" STREQUAL "")
        return()
    endif()

    _ps_deps_root(_ps_root)
    set(_ps_env_sh "${_ps_root}/env.sh")
    set(_ps_installed FALSE)
    if(NOT EXISTS "${_ps_env_sh}")
        message(STATUS
            "[dev-env] 未检测到开发环境（${_ps_env_sh} 不存在），"
            "开始自动安装 tools/ubuntu-setup.sh --install（首次需数分钟，取决于网络）...")
        _ps_setup(--install)
        if(NOT _ps_setup_rc EQUAL 0)
            message(FATAL_ERROR
                "[dev-env] 自动安装失败（返回 ${_ps_setup_rc}），输出尾部：\n${_ps_setup_out}\n"
                "[dev-env] 也可手动安装：bash tools/ubuntu-setup.sh --install")
        endif()
        if(NOT EXISTS "${_ps_env_sh}")
            message(FATAL_ERROR "[dev-env] --install 已执行但仍未生成 ${_ps_env_sh}，请检查上方安装输出")
        endif()
        set(_ps_installed TRUE)
    endif()

    _ps_import_env("${_ps_env_sh}")
    if(_ps_installed)
        message(STATUS "[dev-env] 自动安装完成，已加载 ${_ps_env_sh}")
    else()
        message(STATUS "[dev-env] 已自动加载 env.sh（此前未 source）：导入 ${_ps_imported_list}")
    endif()
endfunction()
_ps_bootstrap_env()

# ---- 层 3：环境变量/路径自动配齐（缺啥补啥，补不了才 FATAL 教小白）----
function(_ps_check_env_vars)
    _ps_deps_root(_ps_root)

    # env.sh 缺失 → 自动重装一次（--install 幂等，只重生成配置不动已有工具）
    if(NOT EXISTS "${_ps_root}/env.sh")
        message(STATUS "[dev-env] ${_ps_root}/env.sh 缺失，自动修复（--install）...")
        _ps_setup(--install)
        if(NOT _ps_setup_rc EQUAL 0 OR NOT EXISTS "${_ps_root}/env.sh")
            message(FATAL_ERROR
                "[dev-env] env.sh 自动修复失败（返回 ${_ps_setup_rc}），输出尾部：\n${_ps_setup_out}\n"
                "[dev-env] 修复：bash tools/ubuntu-setup.sh --install")
        endif()
    endif()

    set(_ps_manifest "${CMAKE_SOURCE_DIR}/deps-version.json")
    set(_ps_qt_rel "")
    if(EXISTS "${_ps_manifest}")
        file(READ "${_ps_manifest}" _ps_json)
        string(JSON _ps_qt_ver ERROR_VARIABLE _ps_json_err GET "${_ps_json}" toolchains qt version)
        string(JSON _ps_qt_abi ERROR_VARIABLE _ps_json_err GET "${_ps_json}" toolchains qt abi)
        string(REGEX REPLACE "^=+" "" _ps_qt_ver "${_ps_qt_ver}")
        set(_ps_qt_rel "/toolchain/qt/${_ps_qt_ver}/${_ps_qt_abi}")
    endif()

    # PS_QT_DIR 缺失 → 自动从 deps-version.json 推导；
    # 推导出来仍要过版本一致性校验，防 env.sh/preset 过期漂移。
    if(NOT DEFINED ENV{PS_QT_DIR} OR "$ENV{PS_QT_DIR}" STREQUAL "")
        if(_ps_qt_rel STREQUAL "")
            message(FATAL_ERROR
                "[dev-env] PS_QT_DIR 未设置且 deps-version.json 缺失/损坏，无法自动推导 Qt 路径。\n"
                "[dev-env] 修复：bash tools/ubuntu-setup.sh --install 重新生成 env.sh")
        endif()
        set(ENV{PS_QT_DIR} "${_ps_root}${_ps_qt_rel}")
        message(STATUS "[dev-env] 已自动推导 PS_QT_DIR=$ENV{PS_QT_DIR}")
    endif()

    # 版本一致性：PS_QT_DIR 必须包含 deps-version.json 声明的 Qt 版本。
    if(NOT _ps_qt_rel STREQUAL "" AND NOT "$ENV{PS_QT_DIR}" MATCHES "${_ps_qt_rel}")
        message(FATAL_ERROR
            "[dev-env] PS_QT_DIR=$ENV{PS_QT_DIR} 与 deps-version.json 声明的"
            " ${_ps_qt_rel} 不一致：检查 env.sh/preset 是否过期。")
    endif()

    # Qt 缺失/损坏 → 自动补装（--install-qt），补完复查。
    if(NOT EXISTS "$ENV{PS_QT_DIR}/lib/cmake/Qt6/Qt6Config.cmake")
        message(STATUS "[dev-env] Qt 缺失/损坏（$ENV{PS_QT_DIR}），自动补装 --install-qt...")
        _ps_setup(--install-qt)
        if(NOT _ps_setup_rc EQUAL 0 OR NOT EXISTS "$ENV{PS_QT_DIR}/lib/cmake/Qt6/Qt6Config.cmake")
            message(FATAL_ERROR
                "[dev-env] Qt 自动补装失败（返回 ${_ps_setup_rc}），输出尾部：\n${_ps_setup_out}\n"
                "[dev-env] 修复：bash tools/ubuntu-setup.sh --install-qt")
        endif()
        message(STATUS "[dev-env] Qt 已自动补装")
    endif()

    # ccache 二进制缺失 → 自动补装（find_program 靠导入的 PATH 找 managed ccache）。
    if(NOT EXISTS "${_ps_root}/toolchain/ccache/bin/ccache")
        message(STATUS "[dev-env] ccache 缺失，自动补装 --install-toolchain...")
        _ps_setup(--install-toolchain)
        if(NOT _ps_setup_rc EQUAL 0 OR NOT EXISTS "${_ps_root}/toolchain/ccache/bin/ccache")
            message(FATAL_ERROR
                "[dev-env] ccache 自动补装失败（返回 ${_ps_setup_rc}），输出尾部：\n${_ps_setup_out}\n"
                "[dev-env] 修复：bash tools/ubuntu-setup.sh --install-toolchain")
        endif()
        message(STATUS "[dev-env] ccache 已自动补装")
    endif()

    # CCACHE_DIR 缺失 → 自动补默认路径；目录不存在 → 自动补装 toolchain 后复查。
    if(NOT DEFINED ENV{CCACHE_DIR} OR "$ENV{CCACHE_DIR}" STREQUAL "")
        set(ENV{CCACHE_DIR} "${_ps_root}/caches/ccache")
        message(STATUS "[dev-env] 已自动补 CCACHE_DIR=$ENV{CCACHE_DIR}")
    endif()
    if(NOT IS_DIRECTORY "$ENV{CCACHE_DIR}")
        message(STATUS "[dev-env] 缓存目录 $ENV{CCACHE_DIR} 不存在，自动补装 --install-toolchain...")
        _ps_setup(--install-toolchain)
        if(NOT _ps_setup_rc EQUAL 0 OR NOT IS_DIRECTORY "$ENV{CCACHE_DIR}")
            message(FATAL_ERROR
                "[dev-env] 缓存目录自动修复失败（返回 ${_ps_setup_rc}），输出尾部：\n${_ps_setup_out}\n"
                "[dev-env] 修复：bash tools/ubuntu-setup.sh --install-toolchain")
        endif()
    endif()

    # CMAKE_PREFIX_PATH 兜底：没包含 PS_QT_DIR 就前置注入（CMake 变量 + 环境变量
    # 双通道），否则 find_package(Qt6) 会静默回退到系统 Qt（版本不受控）——
    # preset 忘注入、env.sh 过期、手动 export 都会触发这条路径，在门禁里堵死。
    set(_ps_cpp "")
    if(DEFINED ENV{CMAKE_PREFIX_PATH})
        set(_ps_cpp "$ENV{CMAKE_PREFIX_PATH}")
    endif()
    string(FIND "${_ps_cpp}" "$ENV{PS_QT_DIR}" _ps_qt_in_cpp)
    if(_ps_qt_in_cpp LESS 0)
        if(_ps_cpp STREQUAL "")
            set(ENV{CMAKE_PREFIX_PATH} "$ENV{PS_QT_DIR}")
        else()
            set(ENV{CMAKE_PREFIX_PATH} "$ENV{PS_QT_DIR}:${_ps_cpp}")
        endif()
        message(STATUS "[dev-env] 已将 PS_QT_DIR 注入 CMAKE_PREFIX_PATH（防回退系统 Qt）")
    endif()
    list(FIND CMAKE_PREFIX_PATH "$ENV{PS_QT_DIR}" _ps_qt_in_cmake)
    if(_ps_qt_in_cmake LESS 0)
        list(PREPEND CMAKE_PREFIX_PATH "$ENV{PS_QT_DIR}")
    endif()

    # 构建期兜底：ninja/cmake --build 往往跑在没 source env.sh 的 shell 里，ccache
    # 那时读不到 CCACHE_DIR（会落 ~/.cache/ccache，连 depend_mode 等策略也丢）。
    # ccache 的取值顺序是 env 优先、conf 兜底——把 cache_dir 钉进 conf 后，
    # 构建期无论 shell 环境如何都落到统一缓存并读到同一份策略。
    set(_ps_ccache_conf "$ENV{CCACHE_DIR}/ccache.conf")
    if(EXISTS "${_ps_ccache_conf}")
        file(READ "${_ps_ccache_conf}" _ps_conf)
        if(NOT _ps_conf MATCHES "(^|\n)[ \t]*cache_dir[ \t]*=")
            file(APPEND "${_ps_ccache_conf}"
                "\n# 由 cmake configure 追加：构建 shell 未加载 env.sh 时仍落到统一缓存。\n"
                "cache_dir = $ENV{CCACHE_DIR}\n")
            message(STATUS "[dev-env] 已在 ccache.conf 钉入 cache_dir=$ENV{CCACHE_DIR}（构建期免 env）")
        endif()
    endif()
endfunction()
_ps_check_env_vars()

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

# ---- Git hooks 自动注册（小白零门槛：clone 后直接 configure 也能过）----
# 只要仓库里有 .githooks，就把它注册成 core.hooksPath 并保证 hook 可执行；
# 这是纯本地 git config / chmod 操作，安全幂等，失败才报错。
if(_ps_is_git_worktree AND EXISTS "${CMAKE_SOURCE_DIR}/.githooks")
    set(_ps_hooks_expected "${CMAKE_SOURCE_DIR}/.githooks")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" config --get core.hooksPath
        OUTPUT_VARIABLE _ps_hooks_path
        ERROR_QUIET
        RESULT_VARIABLE _ps_hooks_rc)
    string(STRIP "${_ps_hooks_path}" _ps_hooks_path)
    set(_ps_hooks_ok FALSE)
    if(_ps_hooks_rc EQUAL 0 AND NOT _ps_hooks_path STREQUAL "")
        get_filename_component(_ps_hooks_abs "${_ps_hooks_path}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
        if(_ps_hooks_abs STREQUAL _ps_hooks_expected)
            set(_ps_hooks_ok TRUE)
        endif()
    endif()
    if(NOT _ps_hooks_ok)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" config core.hooksPath .githooks
            RESULT_VARIABLE _ps_hooks_set_rc)
        if(NOT _ps_hooks_set_rc EQUAL 0)
            message(FATAL_ERROR
                "[dev-env] 无法自动注册 Git hooks（git config 返回 ${_ps_hooks_set_rc}）。\n"
                "[dev-env] 修复：手动执行 git config core.hooksPath .githooks")
        endif()
        message(STATUS "[dev-env] 已自动注册 Git hooks：core.hooksPath=.githooks")
        set(_ps_hooks_path ".githooks")
    endif()
    # 可执行位自动修复（clone 工具/压缩包传输可能丢 +x）
    if(UNIX)
        foreach(_ps_hook pre-commit pre-push)
            if(EXISTS "${_ps_hooks_expected}/${_ps_hook}")
                file(CHMOD "${_ps_hooks_expected}/${_ps_hook}"
                    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                                GROUP_READ GROUP_EXECUTE
                                WORLD_READ WORLD_EXECUTE)
            endif()
        endforeach()
    endif()
    message(STATUS "[dev-env] Git hooks 已就位：core.hooksPath=${_ps_hooks_path}")
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
        _ps_deps_root(_ps_root)
        message(FATAL_ERROR
            "[ccache] CCACHE_DIR 未设置；CMake 无法判断缓存落在哪。\n"
            "[ccache] 修复：. \"${_ps_root}/env.sh\"（默认根 $HOME/.ps-deps，"
            "可用 PS_DEPS_ROOT 覆盖），或用 cmake --preset default。")
    endif()
    set(_dir "$ENV{CCACHE_DIR}")
    if(NOT IS_DIRECTORY "${_dir}")
        message(FATAL_ERROR "[ccache] CCACHE_DIR 不存在：${_dir}；修复：tools/ubuntu-setup.sh --install-toolchain")
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

# ---- 层 6：--check 全量校验，未通过自动 --install 修复后复查 ----
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND EXISTS "/etc/debian_version")
    _ps_setup(--check)
    if(NOT _ps_setup_rc EQUAL 0)
        message(STATUS "[dev-env] --check 未通过，自动修复（--install）后复查...")
        _ps_setup(--install)
        if(NOT _ps_setup_rc EQUAL 0)
            message(FATAL_ERROR
                "[dev-env] 自动修复失败（--install 返回 ${_ps_setup_rc}），输出尾部：\n${_ps_setup_out}")
        endif()
        _ps_setup(--check)
        if(NOT _ps_setup_rc EQUAL 0)
            message(FATAL_ERROR
                "[dev-env] tools/ubuntu-setup.sh --check 修复后仍未通过（返回 ${_ps_setup_rc}）：\n"
                "${_ps_setup_out}\n"
                "[dev-env] 手动修复：bash tools/ubuntu-setup.sh --install")
        endif()
        message(STATUS "[dev-env] --check 失败项已自动修复并复查通过")
    else()
        message(STATUS "[dev-env] tools/ubuntu-setup.sh --check 通过")
    endif()
else()
    message(STATUS "[dev-env] 非 Debian/Ubuntu Linux，跳过 tools/ubuntu-setup.sh --check")
    _ps_check_git_hooks_native()
endif()
