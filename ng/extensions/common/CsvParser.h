#pragma once

#include "ps_parser_spi.h"

namespace nextpilot::plotscope::ext
{

// CSV 解析器（SPI 实现）。
//
// 格式知识（老 CsvExtension 的规则，平移过来）：
//   - 逗号分隔；第一行是**表头**，给出列名；
//   - 第一列是**时间**（单位：秒，浮点），其余列是数值（f64）；
//   - 空行跳过；时间列取不到值按 0 处理。
//
// 与旧实现的差别只在"产出端"：
//   旧：自己开文件、自己建 Dataset；
//   新：只把字节变成**批**，由 sink 吐给宿主（见 AGENTS.md §3 P0-1）。
//
// 于是同一份 CSV 解析实现，既能读文件也能读流；同一份字节还能同时喂多个解析器。
class CsvParser final
{
public:
    static const PsParserInfo &parserInfo();
    static const PsParserVtable *parserVtable();

    // 供单测使用：直接解析一段字节到批（不经过 vtable）。
    static bool parseBuffer(const uint8_t *data, uint32_t size, PsParserSink *sink,
                            PsError *err);
};

} // namespace nextpilot::plotscope::ext
