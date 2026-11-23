#pragma once

#include "ps_parser_spi.h"

/*
 * Vector BLF（.blf）解析器（SPI 视图）。
 *
 * 格式知识来自 main 的 automotive/datasources/blf/BlfParser.cpp：
 *   "LOGG" 文件头 → 一串 "LOBJ" 对象；对象头 32 字节（签名/size/type/…），
 *   CAN 报文对象类型 1（CAN_MESSAGE）/86（CAN_MESSAGE2）/100（CAN_FD_MESSAGE）；
 *   另外有 type 10 的容器对象（method 0 明文 / method 2 zlib 压缩），
 *   压缩容器解出来内部还是 "LOBJ" 序列。
 * 时间戳：对象头 flags bit1 = 纳秒，否则 10 µs 为单位。
 *
 * 出口口径与 main 同源、形状改成本仓的「一表一批」：
 *   main 把所有帧拍平成 CANbus.ID.bN / .dlc / .period 三条曲线，
 *   这里按 **CAN ID 出一张表**（表名 CAN<bus>_<IDHEX>），列为变化过的字节
 *   （b0..b7）、dlc、period（周期 ms），时间轴 = 相对首帧的纳秒。
 *   只保留变化字节、跳过全常量帧 —— 与 main psCanFramesToChannels 一致。
 *
 * 随机访问格式：streaming=0，feed() 只累积字节，finish() 一次性解析。
 */
namespace nextpilot::plotscope::ext::blf
{

const PsParserInfo &parserInfo();
const PsParserVtable *parserVtable();

// PS_REGISTER_PARSER 用的静态自注册类。
class BlfParserSpi
{
public:
    static const PsParserInfo &parserInfo();
    static const PsParserVtable *parserVtable();
};

} // namespace nextpilot::plotscope::ext::blf
