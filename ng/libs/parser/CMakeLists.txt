# ng_parser：消息解析契约与 C ABI（顺序协议 / 格式解析共用）。
# IMessageParser / ParserRegistry / IByteSource / DecodedFrame + ps_*_spi.h（纯 C ABI，不改结构）。
# 依赖 foundation(Result) + uischema(ParamSpec) + registry(IToolContribution 基类)，headless 可用。
add_library(ng_parser STATIC
    src/IByteSource.cpp
    src/ParserRegistry.cpp
    src/IMessageParser.cpp
    include/DecodedFrame.h
    include/IByteSource.h
    include/ParserRegistry.h
    include/IMessageParser.h
    include/ps_parser_spi.h
    include/ps_message_parser_spi.h
)
target_include_directories(ng_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ng_parser PUBLIC Qt6::Core ng_foundation ng_uischema ng_registry ng_intent)
set_target_properties(ng_parser PROPERTIES CXX_STANDARD 17)
