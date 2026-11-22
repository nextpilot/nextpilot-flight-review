#pragma once

#include "ps_parser_spi.h"

namespace nextpilot::plotscope::ext
{

// 一个最小的 SPI 解析器实现，示范"解析器如何写"。
//
// 它演示了 SPI 的全部要点：
//   - 解析器只认字节：feed() 收到什么就解析什么，不问字节从哪来；
//   - 解析器不写存储：结果通过 sink.frame_batch() 吐给调用者；
//   - 解析器不认识宿主：没有 Dataset、没有 repo、没有 Qt（除 ABI 头）；
//   - 解析器可独立单测：喂一段字节，断言吐出的批。
//
// 只需把"产出端"从"自己建 Dataset"改成"通过 sink 吐批"。
//
// 这里用一种极简的二进制格式做验证载体：
//   魔数 "PSPX" | u32 行数 | 重复 { i64 time_us, f64 value }
class PspxParser final
{
public:
    static const PsParserInfo &parserInfo();
    static const PsParserVtable *parserVtable();

    // 供单测使用：直接解析一段字节到批（不经过 vtable）。
    static bool parseBuffer(const uint8_t *data, uint32_t size, PsParserSink *sink,
                            PsError *err);
};

} // namespace nextpilot::plotscope::ext
