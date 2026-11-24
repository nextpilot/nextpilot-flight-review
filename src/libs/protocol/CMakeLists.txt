find_package(Qt6 REQUIRED COMPONENTS Core)

# 协议逻辑库：顺序段协议的解码内核 + 表达式系统 + 校验。
#
# 纯逻辑，无 QWidget、无 Rust、无 PsHostApi——产出一中立的 channel→(t,v)，
# 由上层决定去向：SPI 解析器转成 PsFrameBatch 进摄取管道，编辑器转成可读文本。
# 老代码的解码内核（约 2400 行）原样复用，只把产出端从"自建 Dataset"改成中立结构。
add_library(ps_protocol STATIC
    src/ProtocolDocument.cpp
    src/ProtocolSpec.cpp
    src/core/SharedSpec.cpp
    src/bin/BinSpec.cpp
    src/text/TextFormat.cpp
    src/CompiledProgram.cpp
    src/ProtocolTemplateStore.cpp
    src/SequentialCodec.cpp
    src/DigestEngine.cpp
    src/StreamDecoder.cpp
    src/ScriptEngine.cpp
    include/protocol/ProtocolDocument.h
    include/protocol/ProtocolSpec.h
    include/protocol/SequentialCodec.h
    include/protocol/CompiledProgram.h
    include/protocol/ProtocolTemplateStore.h
    include/protocol/DigestEngine.h
    include/protocol/StreamDecoder.h
    include/protocol/TextFormat.h
    include/protocol/ScriptEngine.h
)

# 脚本后端：仓库内 src/3rdparty/lua-5.4（源码编库，目标 lua::lua），不走系统包。
# 目标由 ng 顶层 add_subdirectory 提供；路径、平台差异（m/libdl）都在 lua 目标自己身上，
# 这里只管链接，不再出现任何 ../../../deps 之类写死的路径。
if(NOT TARGET lua)
    message(FATAL_ERROR "缺少 lua::lua 目标：ng/CMakeLists.txt 需要先引入 3rdparty/lua-5.4")
endif()
target_link_libraries(ps_protocol PUBLIC lua::lua)
target_compile_definitions(ps_protocol PUBLIC PS_PROTOCOL_LUA=1)
target_include_directories(ps_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ps_protocol PUBLIC Qt6::Core)
set_target_properties(ps_protocol PROPERTIES AUTOMOC ON CXX_STANDARD 17)
