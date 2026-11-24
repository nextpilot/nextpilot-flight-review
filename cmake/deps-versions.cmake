include_guard(GLOBAL)

# 唯一版本源是仓库根的 versions.json。本文件只做 JSON → CMake 变量的适配，
# 不放任何版本常量；升级依赖改 versions.json，不改这里。
set(_ps_versions_json "${CMAKE_SOURCE_DIR}/versions.json")
if(NOT EXISTS "${_ps_versions_json}")
    message(FATAL_ERROR "缺少 versions.json：${_ps_versions_json}")
endif()
file(READ "${_ps_versions_json}" _ps_versions_raw)

# 先探一个字段做语法体检，坏 JSON 在这里就给可读报错，而不是等到拼 URL 时才炸
string(JSON _ps_probe ERROR_VARIABLE _ps_json_err GET "${_ps_versions_raw}" mirror)
if(_ps_json_err)
    message(FATAL_ERROR "versions.json 解析失败（${_ps_versions_json}）：${_ps_json_err}")
endif()

string(JSON PS_MIRROR_BASE GET "${_ps_versions_raw}" mirror)
string(JSON PS_VER_QT GET "${_ps_versions_raw}" qt version)
string(JSON PS_QT_ABI GET "${_ps_versions_raw}" qt abi)

string(JSON PS_TOOL_GCC_MIN GET "${_ps_versions_raw}" toolchain gcc min)
string(JSON PS_TOOL_CMAKE_MIN GET "${_ps_versions_raw}" toolchain cmake min)
string(JSON PS_TOOL_NINJA_MIN GET "${_ps_versions_raw}" toolchain ninja min)
string(JSON PS_TOOL_RUST_PIN GET "${_ps_versions_raw}" toolchain rust pin)
string(JSON PS_TOOL_CCACHE_MIN GET "${_ps_versions_raw}" toolchain ccache min)
string(JSON PS_TOOL_PYTHON_MIN GET "${_ps_versions_raw}" toolchain python min)

string(JSON PS_VER_ZLIB GET "${_ps_versions_raw}" cpp zlib version)
string(JSON PS_SHA_ZLIB GET "${_ps_versions_raw}" cpp zlib sha256)
string(JSON PS_VER_ZSTD GET "${_ps_versions_raw}" cpp zstd version)
string(JSON PS_SHA_ZSTD GET "${_ps_versions_raw}" cpp zstd sha256)
string(JSON PS_VER_LZ4 GET "${_ps_versions_raw}" cpp lz4 version)
string(JSON PS_SHA_LZ4 GET "${_ps_versions_raw}" cpp lz4 sha256)
string(JSON PS_VER_NANOARROW GET "${_ps_versions_raw}" cpp nanoarrow version)
string(JSON PS_SHA_NANOARROW GET "${_ps_versions_raw}" cpp nanoarrow sha256)
string(JSON PS_TAG_NANOARROW GET "${_ps_versions_raw}" cpp nanoarrow archive_tag)

set(PS_DEPS_MANIFEST "${_ps_versions_json}")
