#pragma once

#include "protocol/CompiledProgram.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nextpilot::plotscope::protocol
{

// 一段 format 编译后的线性序列：kind 0=字面 1=转换符 2=字符集。
struct TextFmtPart
{
    int kind = 0;
    std::string lit;   // kind 0：要逐字匹配的字面
    char conv = 0;     // kind 1：转换符（d u i x X o b f e g c s t T）
    int width = 0;     // %5d / %3c / %3s 的宽度，0 表示不定宽
    bool discard = false; // %* 读了就丢
    std::string set;   // kind 2：%[...] 的字符集
    bool negate = false;  // %[^...]
};

// 把 scanf 风格的 format 编译成 TextFmtPart 序列；语法错误写进 err。
bool textCompileFormat(const std::string &f, std::vector<TextFmtPart> &out, std::string &err);

// 段开始/元素之间吃掉一个分隔符（blank 走空白字符类，默认逗号）。
size_t textEatDelimiter(const uint8_t *p, size_t pos, size_t n, const CompiledSeg &seg);

// 读一个元素：走完整个 format，最多产出一个取值（其余必须是字面或 %*）。
// stopLit 是下一段的起始字面，用它当 %s 的右边界，避免一路吃到行尾。
bool textScanElement(const uint8_t *p, size_t n, size_t &pos, const std::vector<TextFmtPart> &parts,
                     const CompiledSeg &seg, const std::string &stopLit, std::string &val, char &conv,
                     bool &hasVal, std::string &err, bool &needMore);

bool isBlankByte(char c);

} // namespace nextpilot::plotscope::protocol
