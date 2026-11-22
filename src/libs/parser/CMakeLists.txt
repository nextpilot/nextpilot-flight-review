# ps_parser：消息解析契约与 C ABI（顺序协议 / 格式解析共用）。
# IMessageParser / ParserHub / IByteSource / DecodedFrame + ps_*_spi.h（纯 C ABI，不改结构）。
# 依赖 foundation(Result) + uischema(ParamSpec) + registry(IToolContribution 基类)，headless 可用。
add_library(ps_parser STATIC
    src/IByteSource.cpp
    src/ParserHub.cpp
    src/IMessageParser.cpp
    include/DecodedFrame.h
    include/IByteSource.h
    include/ParserHub.h
    include/IMessageParser.h
    include/ps_parser_spi.h
    include/ps_message_parser_spi.h
)
target_include_directories(ps_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ps_parser PUBLIC Qt6::Core ps_foundation ps_uischema ps_registry ps_intent)
set_target_properties(ps_parser PROPERTIES CXX_STANDARD 17)
