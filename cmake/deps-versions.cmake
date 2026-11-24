include_guard(GLOBAL)

# 唯一版本源是仓库根的 versions.json。本文件只做 JSON → CMake 变量的适配，
# 不放任何版本常量；升级依赖改 versions.json，不改这里。
#
# versions.json 两个顶层段：
#   toolchains —— ubuntu-setup.sh 负责获取的工具（gcc/cmake/ninja/ccache/python/rust/qt），
#                 只由脚本体检，CMake 不消费，因此这里不解析。
#   packages   —— 构建期由 FetchContent 拉源码的三方库，CMake 只解析这一段。
set(_ps_versions_json "${CMAKE_SOURCE_DIR}/versions.json")
if(NOT EXISTS "${_ps_versions_json}")
    message(FATAL_ERROR "缺少 versions.json：${_ps_versions_json}")
endif()
file(READ "${_ps_versions_json}" _ps_versions_raw)

# 先探一个字段做语法体检，坏 JSON 在这里就给可读报错，而不是等到拼 URL 时才炸
string(JSON _ps_probe ERROR_VARIABLE _ps_json_err GET "${_ps_versions_raw}" packages mirror)
if(_ps_json_err)
    message(FATAL_ERROR "versions.json 解析失败（${_ps_versions_json}）：${_ps_json_err}")
endif()

string(JSON PS_MIRROR_BASE GET "${_ps_versions_raw}" packages mirror)

string(JSON PS_VER_ZLIB GET "${_ps_versions_raw}" packages zlib version)
string(JSON PS_SHA_ZLIB GET "${_ps_versions_raw}" packages zlib sha256)
string(JSON PS_VER_ZSTD GET "${_ps_versions_raw}" packages zstd version)
string(JSON PS_SHA_ZSTD GET "${_ps_versions_raw}" packages zstd sha256)
string(JSON PS_VER_LZ4 GET "${_ps_versions_raw}" packages lz4 version)
string(JSON PS_SHA_LZ4 GET "${_ps_versions_raw}" packages lz4 sha256)
string(JSON PS_VER_NANOARROW GET "${_ps_versions_raw}" packages nanoarrow version)
string(JSON PS_SHA_NANOARROW GET "${_ps_versions_raw}" packages nanoarrow sha256)
string(JSON PS_TAG_NANOARROW GET "${_ps_versions_raw}" packages nanoarrow tag)

set(PS_DEPS_MANIFEST "${_ps_versions_json}")
