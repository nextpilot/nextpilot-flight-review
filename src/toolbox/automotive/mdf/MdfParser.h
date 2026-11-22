#pragma once

#include "ps_parser_spi.h"

#include <QString>

/*
 * MDF 4.x 解析器（SPI 视图）。
 *
 * 格式知识来自 main 的 Mdf4DataLoader.cpp（525 行自写解析，不依赖第三方 mdf 库）：
 *   ##HD → ##DG → ##CG → ##CN + ##CC/##TX，数据在 ##DT / ##DZ（zlib，type=1 需转置）/ ##DL。
 * 这里只做**重写式的搬迁**：同样按块链表走，但输出改成 SPI 的批（PsFrameBatch），
 * 读字节的职责留给调用者（文件、串口、网络都一样）。
 *
 * 与 ULog 的差别：MDF 是**随机访问**格式（块偏移 + 链表），不是流式协议。
 *   info.streaming = false：宿主喂满整个文件字节后由 finish() 一次解析。
 *   feed() 只累积字节，不产生批。
 */
namespace nextpilot::plotscope::ext::mdf
{

// 解析器元数据与 vtable。
const PsParserInfo &parserInfo();
const PsParserVtable *parserVtable();

// 供 PS_REGISTER_PARSER 的静态自注册类（宏要求 Cls::parserInfo()/parserVtable()）。
class MdfParserSpi
{
public:
    static const PsParserInfo &parserInfo();
    static const PsParserVtable *parserVtable();
};

} // namespace nextpilot::plotscope::ext::mdf
