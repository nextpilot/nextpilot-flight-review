find_package(Qt6 REQUIRED COMPONENTS Core)

# 协议逻辑库：顺序段协议的解码内核 + 表达式系统 + 校验。
#
# 纯逻辑，无 QWidget、无 Rust、无 PsHostApi——产出一中立的 channel→(t,v)，
# 由上层决定去向：SPI 解析器转成 PsFrameBatch 进摄取管道，编辑器转成可读文本。
# 老代码的解码内核（约 2400 行）原样复用，只把产出端从"自建 Dataset"改成中立结构。
add_library(ng_protocol STATIC
    src/ProtocolDocument.cpp
    src/ProtocolSpec.cpp
    src/core/SharedSpec.cpp
    src/bin/BinSpec.cpp
    src/text/TextFormat.cpp
    src/CompiledProgram.cpp
    src/SequentialCodec.cpp
    src/DigestEngine.cpp
    src/StreamDecoder.cpp
    src/ScriptEngine.cpp
    include/protocol/ProtocolDocument.h
    include/protocol/ProtocolSpec.h
    include/protocol/SequentialCodec.h
    include/protocol/CompiledProgram.h
    include/protocol/DigestEngine.h
    include/protocol/StreamDecoder.h
    include/protocol/TextFormat.h
    include/protocol/ScriptEngine.h
)

# 脚本后端：仓库内 deps/lua（Lua 5.4 静态库），不走系统包。
set(NG_LUA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../deps/lua")
if(EXISTS "${NG_LUA_ROOT}/lib/liblua.a")
    target_include_directories(ng_protocol PRIVATE "${NG_LUA_ROOT}/include")
    # libm / libdl 是 POSIX 专有：Windows 上不存在 m.lib，无条件链接会 LNK1181。
    set(_ng_lua_libs "${NG_LUA_ROOT}/lib/liblua.a")
    if(UNIX)
        list(APPEND _ng_lua_libs m ${CMAKE_DL_LIBS})
    endif()
    target_link_libraries(ng_protocol PUBLIC ${_ng_lua_libs})
    target_compile_definitions(ng_protocol PUBLIC NG_PROTOCOL_LUA=1)
endif()
target_include_directories(ng_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ng_protocol PUBLIC Qt6::Core)
set_target_properties(ng_protocol PROPERTIES AUTOMOC ON CXX_STANDARD 17)
