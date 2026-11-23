#pragma once

#include "ps_parser_spi.h"

/*
 * ArduPilot DataFlash（.bin）解析器（SPI 视图）。
 *
 * 格式知识来自 main 的 autopilot/datasources/apbin/ApbinParser.cpp
 * （main 里这个 loader 的注释就写着「ArduPilot DataFlash：FMT 自描述 + 定长消息」，
 *  即 DataFlash 与 apbin 是同一格式，main 没有单独的 dataflash 模块）。
 *
 * 字节结构：A3 95 <msgId> 头 + 定长消息体。
 *   - FMT(128) 固定 89 字节：Type/Length/Name/Fields(codes)/Labels
 *   - FMTU / UNIT / MULT 提供单位与倍率
 *   - 其余消息按 TypeUS/TimeUS 字段对齐成时间轴
 *
 * 出口口径（本仓「一表一批」）：
 *   每个 (格式, 实例号) 出一张表，表名 = FMT 名（实例号>0 时追加 _<n>），
 *   列 = 该格式的数值字段（TimeUS 与实例字段当轴/维度，不作列；字符串字段跳过），
 *   时间轴 = 相对**全文件首帧**的纳秒（保留各消息间的原始相对偏移）。
 *
 * 随机访问格式：streaming=0，feed() 只累积字节，finish() 一次性解析。
 */
namespace nextpilot::plotscope::ext::apbin
{

const PsParserInfo &parserInfo();
const PsParserVtable *parserVtable();

// PS_REGISTER_PARSER 用的静态自注册类。
class ApbinParserSpi
{
public:
    static const PsParserInfo &parserInfo();
    static const PsParserVtable *parserVtable();
};

} // namespace nextpilot::plotscope::ext::apbin
