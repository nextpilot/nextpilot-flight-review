find_package(Qt6 REQUIRED COMPONENTS Core)

# 协议逻辑库：顺序段协议的解码内核 + 表达式系统 + 校验。
#
# 纯逻辑，无 QWidget、无 Rust、无 PsHostApi——产出一中立的 channel→(t,v)，
# 由上层决定去向：SPI 解析器转成 PsFrameBatch 进摄取管道，编辑器转成可读文本。
# 老代码的解码内核（约 2400 行）原样复用，只把产出端从"自建 Dataset"改成中立结构。
add_library(ng_protocol STATIC
    src/ProtocolDocument.cpp
    src/ProtocolSpec.cpp
    src/SequentialCodec.cpp
    include/protocol/ProtocolDocument.h
    include/protocol/ProtocolSpec.h
    include/protocol/SequentialCodec.h
)
target_include_directories(ng_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ng_protocol PUBLIC Qt6::Core)
set_target_properties(ng_protocol PROPERTIES AUTOMOC ON CXX_STANDARD 17)
