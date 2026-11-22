#pragma once

#include "ps_parser_spi.h"

#include <QByteArray>

namespace nextpilot::plotscope::ext
{

// 自定义顺序协议解析器（SPI 实现）。
//
// 把 ng/libs/protocol 的解码内核接进统一摄取管道：
//   - configure(template_json) 接收用户选的 .psproto 模板（v2 参数槽，不读全局）；
//   - feed 缓冲全部字节（帧不定长，无法边喂边解）；
//   - finish 一次解码 → 每条通道一批 PsFrameBatch 吐给 sink。
//
// 与老 CustomProtocolLoader 的差别只在产出端：
//   老：自建 Dataset（PsHostApi / psBuildDataset）；
//   新：吐 PsFrameBatch 给宿主统一入仓——于是能录制回放、能扇出、能多源并存。
// 解码算法（段/变体/校验/表达式/时间戳）与编辑器试算完全共用同一份内核。
class CustomProtocolParser final
{
public:
    static const PsParserInfo &parserInfo();
    static const PsParserVtable *parserVtable();

    // 供单测：直接喂模板 + 字节 → sink（不经过注册表）。
    static bool parseBuffer(const QByteArray &templateJson, const uint8_t *data, uint32_t size,
                            PsParserSink *sink, PsError *err);
};

} // namespace nextpilot::plotscope::ext
