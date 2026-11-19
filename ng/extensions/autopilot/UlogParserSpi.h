#pragma once

#include "ps_parser_spi.h"

#include <QString>

/*
 * ULog 解析器（SPI 视图）。
 *
 * 架构定位（AGENTS.md §3 P0-1）：
 *   ng 曾有两条数据通路并存——
 *     A：data.load → IMessageParser::load()，驱动自己读文件、自己建 Dataset；
 *     B：Source → IngestPipeline → parser SPI → Dataset，生产零调用。
 *   处置是**归并到 B，不新建第三条路**。本文件是 B 侧的一环：
 *   把 ULog 的格式解析能力以标准 SPI 解析器的身份暴露出去，让宿主能
 *     · 同一份解析器既读文件、也读流（喂字节的是调用者）；
 *     · 同一份字节同时喂给多个解析器（扇出）；
 *     · 读字节的同时旁路到录制器（raw/mcap 多路录制）。
 *
 * 格式知识逐字保留：协议解析（格式表 / 订阅展平 / D 段字段解码 /
 * 参数 / 事件 / dropout）复用旧 extension 的实现，未重新发明。
 *
 * 与旧路径的关系：**只有一份格式解析实现**。
 *   旧的 parseUlogStream(path, ds, err) 与 SPI 路径共用同一套协议解析代码，
 *   区别只在"读字节"与"批去哪"由谁决定——见 UlogStreamParser.cpp 的 Emitter。
 */

namespace nextpilot::plotscope::ext::ulog
{

// 解析器元数据与 vtable（供 NG_REGISTER_PARSER 使用）。
const PsParserInfo &parserInfo();
const PsParserVtable *parserVtable();

} // namespace nextpilot::plotscope::ext::ulog
