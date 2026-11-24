include_guard(GLOBAL)

# 唯一版本源是仓库根的 versions.json。本文件只做 JSON → CMake 变量的适配，
# 不放任何版本常量；升级依赖改 versions.json，不改这里。
#
# versions.json 两个顶层段：
#   toolchains —— ubuntu-setup.sh 负责获取的工具，只由脚本体检，CMake 不消费。
#   packages   —— 构建期由 FetchContent 拉源码的三方库，CMake 只解析这一段。
#                 每个包自带 mirrors 数组（元素含 url + sha256），换源/加源只改 JSON。
set(_ps_versions_json "${CMAKE_SOURCE_DIR}/versions.json")
if(NOT EXISTS "${_ps_versions_json}")
    message(FATAL_ERROR "缺少 versions.json：${_ps_versions_json}")
endif()
file(READ "${_ps_versions_json}" _ps_versions_raw)

# 先探一个字段做语法体检，坏 JSON 在这里就给可读报错，而不是等到拼 URL 时才炸
string(JSON _ps_probe ERROR_VARIABLE _ps_json_err GET "${_ps_versions_raw}" packages zlib version)
if(_ps_json_err)
    message(FATAL_ERROR "versions.json 解析失败（${_ps_versions_json}）：${_ps_json_err}")
endif()

# 版本/tag 供 find_package 版本探测与日志使用
string(JSON PS_VER_ZLIB GET "${_ps_versions_raw}" packages zlib version)
string(JSON PS_VER_ZSTD GET "${_ps_versions_raw}" packages zstd version)
string(JSON PS_VER_LZ4 GET "${_ps_versions_raw}" packages lz4 version)
string(JSON PS_VER_NANOARROW GET "${_ps_versions_raw}" packages nanoarrow version)
string(JSON PS_TAG_NANOARROW GET "${_ps_versions_raw}" packages nanoarrow tag)

# 从 packages.<pkg>.mirrors 逐个试镜像，选第一个"能下载且哈希匹配"的。
#
# 为什么不直接用 FetchContent 的多 URL：CMake 的 URL_HASH 只接受单个值，
# 而 gitee 归档 zip 与 GitHub tar.gz 内容不同、哈希不同，无法共用一个 FetchContent。
# 这里用 for 循环手动探测：file(DOWNLOAD) 到缓存 → file(SHA256) 比对，
# 不匹配就试下一个（file(DOWNLOAD) 的 EXPECTED_HASH 参数是致命错误，不能用）。
# 选中的文件留在缓存目录，交给 FetchContent 以本地路径消费，零重复下载、仍校验哈希。
#
# 参数：_ps_pick_mirror <pkg> <out_file_var> <out_hash_var> <out_url_var>
function(_ps_pick_mirror pkg out_file out_hash out_url)
    string(JSON _n LENGTH "${_ps_versions_raw}" packages ${pkg} mirrors)
    if(_n EQUAL 0)
        return()
    endif()
    set(_cache_dir "${CMAKE_BINARY_DIR}/ps-deps-download-cache")
    file(MAKE_DIRECTORY "${_cache_dir}")
    math(EXPR _last "${_n} - 1")
    foreach(_i RANGE ${_last})
        string(JSON _u GET "${_ps_versions_raw}" packages ${pkg} mirrors ${_i} url)
        string(JSON _h GET "${_ps_versions_raw}" packages ${pkg} mirrors ${_i} sha256)
        # 保留归档扩展名：ExternalProject 靠文件名后缀判断如何解压（.zip / .tar.gz）
        set(_ext "")
        if(_u MATCHES "\\.(tar\\.gz|tgz|tar\\.xz|tar\\.bz2|zip|tar)(\\?|$)")
            set(_ext "${CMAKE_MATCH_1}")
        endif()
        if(_ext)
            set(_dst "${_cache_dir}/${pkg}-${_i}.${_ext}")
        else()
            set(_dst "${_cache_dir}/${pkg}-${_i}")
        endif()
        # 缓存命中：本地文件已存在且哈希匹配则跳过网络
        set(_ok FALSE)
        if(EXISTS "${_dst}")
            file(SHA256 "${_dst}" _actual)
            if(_actual STREQUAL _h)
                set(_ok TRUE)
            endif()
        endif()
        if(NOT _ok)
            file(DOWNLOAD "${_u}" "${_dst}" STATUS _st TLS_VERIFY ON)
            list(GET _st 0 _code)
            if(NOT _code EQUAL 0)
                message(STATUS "[deps] ${pkg} 镜像 ${_i} 不可达，试下一个（${_u}）")
                continue()
            endif()
            file(SHA256 "${_dst}" _actual)
            if(NOT _actual STREQUAL _h)
                message(STATUS "[deps] ${pkg} 镜像 ${_i} 哈希不符，试下一个（${_u}）")
                continue()
            endif()
        endif()
        set(${out_file} "${_dst}" PARENT_SCOPE)
        set(${out_hash} "${_h}" PARENT_SCOPE)
        set(${out_url} "${_u}" PARENT_SCOPE)
        return()
    endforeach()
    message(FATAL_ERROR "${pkg}: 所有镜像都不可用或哈希不匹配（versions.json → packages.${pkg}.mirrors）")
endfunction()

set(PS_DEPS_MANIFEST "${_ps_versions_json}")
