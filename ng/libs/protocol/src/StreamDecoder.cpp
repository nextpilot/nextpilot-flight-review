#include "protocol/StreamDecoder.h"

#include "protocol/CompiledProgram.h"
#include "protocol/DigestEngine.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <cmath>
#include <map>
#include <regex>

namespace nextpilot::plotscope::protocol
{

namespace
{

constexpr size_t kNoPos = static_cast<size_t>(-1);

bool isSignedKind(ValueType t)
{
    return t == ValueType::I8 || t == ValueType::I16 || t == ValueType::I32 || t == ValueType::I64;
}

// 按端序从 elem 字节里组装整数（float 也走它，取到的是 bit pattern）。
uint64_t readIntAt(const uint8_t *p, uint32_t size, bool bigEndian)
{
    uint64_t v = 0;
    if (bigEndian)
        for (uint32_t i = 0; i < size; ++i)
            v = (v << 8) | p[i];
    else
        for (uint32_t i = size; i > 0; --i)
            v = (v << 8) | p[i - 1];
    return v;
}

bool isBlankByte(char c)
{
    return c == ' ' || c == '\t';
}

std::string trimAscii(const std::string &s)
{
    size_t b = 0, e = s.size();
    while (b < e && isBlankByte(s[b]))
        ++b;
    while (e > b && isBlankByte(s[e - 1]))
        --e;
    return s.substr(b, e - b);
}

// 在 [from, to) 里找最早出现的一个候选定界符；matchLen 返回命中长度。
// 候选已在编译期按长度降序排过 → 天然最长匹配（\r\n 先于 \n）。
size_t findDelimiter(const uint8_t *p, size_t from, size_t to, const std::vector<std::string> &cands,
                     size_t &matchLen)
{
    for (size_t i = from; i < to; ++i)
    {
        for (const std::string &c : cands)
        {
            if (c.empty() || i + c.size() > to)
                continue;
            if (std::memcmp(p + i, c.data(), c.size()) == 0)
            {
                matchLen = c.size();
                return i;
            }
        }
    }
    return to;
}

bool findBytes(const uint8_t *p, size_t from, size_t to, const std::string &lit, size_t &at)
{
    if (lit.empty() || from >= to || lit.size() > to - from)
        return false;
    for (size_t i = from; i + lit.size() <= to; ++i)
        if (std::memcmp(p + i, lit.data(), lit.size()) == 0)
        {
            at = i;
            return true;
        }
    return false;
}

// check 段没写 width/delimiter 时，十六进制字符数由算法宽度推导（xor8 → 2，crc16 → 4）。
size_t digestHexWidth(const std::string &alg)
{
    const std::string a = normalizeDigestAlg(alg);
    if (a == "xor8" || a == "sum8" || a == "crc8")
        return 2;
    if (a == "crc16" || a == "crc16_modbus" || a == "crc16_x25")
        return 4;
    if (a == "crc24q")
        return 6;
    if (a == "crc32" || a == "crc32c")
        return 8;
    return 0;
}

// pattern 只做校验，不参与切分。正则非法由 validate/compile 拦下，这里只会遇到匹配失败。
bool checkPattern(const std::string &tok, const CompiledSeg &seg, std::string &err)
{
    if (seg.pattern.empty())
        return true;
    try
    {
        const std::regex re(seg.pattern);
        if (!std::regex_match(tok, re))
        {
            err = "不匹配 pattern：" + seg.pattern;
            return false;
        }
    }
    catch (const std::regex_error &e)
    {
        err = std::string("pattern 编译失败：") + e.what();
        return false;
    }
    return true;
}

bool parseNumber(const std::string &rawIn, const CompiledSeg &seg, TypedValue &out, std::string &err)
{
    std::string s = trimAscii(rawIn);
    if (s.empty())
    {
        err = "空字段";
        return false;
    }
    if (!seg.signedFlag && s[0] == '-')
    {
        err = "不允许负号（未声明 signed）";
        return false;
    }

    const bool sexa = !seg.transform.empty() && seg.transform.front().op == "sexagesimal";
    const int splitDigits = sexa ? seg.transform.front().split : 0;

    if (seg.base != 10)
    {
        char *endp = nullptr;
        const long long v = std::strtoll(s.c_str(), &endp, static_cast<int>(seg.base));
        if (endp == s.c_str() || (endp && *endp != '\0'))
        {
            err = "无法按 " + std::to_string(seg.base) + " 进制解析：" + s;
            return false;
        }
        const bool signedType = seg.type == ValueType::I64 || seg.type == ValueType::I32 ||
                                seg.type == ValueType::I16 || seg.type == ValueType::I8;
        out = signedType ? TypedValue::fromI(v, seg.type)
                         : TypedValue::fromU(static_cast<uint64_t>(v), seg.type);
        return true;
    }

    if (seg.asciiType == 0 && (seg.type == ValueType::F64 || seg.type == ValueType::F32))
    {
        if (!seg.sci && (s.find('e') != std::string::npos || s.find('E') != std::string::npos))
        {
            err = "不允许科学计数法（未声明 sci）";
            return false;
        }
        if (sexa)
        {
            // NMEA 记法：DDMM.MMM / DDDMM.MMM，整数部分末两位是分，小数部分是分的小数。
            const size_t dot = s.find('.');
            const std::string intPart = dot == std::string::npos ? s : s.substr(0, dot);
            const std::string frac = dot == std::string::npos ? std::string() : s.substr(dot + 1);
            if (intPart.size() < 3)
            {
                err = "sexagesimal 整数位不足（需要 DDMM）：" + s;
                return false;
            }
            if (splitDigits > 0 && frac.size() != static_cast<size_t>(splitDigits))
            {
                err = "sexagesimal 分的小数位应为 " + std::to_string(splitDigits) + "：" + s;
                return false;
            }
            const std::string degStr = intPart.substr(0, intPart.size() - 2);
            const std::string minStr = intPart.substr(intPart.size() - 2) +
                                       (frac.empty() ? std::string() : "." + frac);
            char *e1 = nullptr;
            char *e2 = nullptr;
            const double deg = std::strtod(degStr.c_str(), &e1);
            const double min = std::strtod(minStr.c_str(), &e2);
            if (!e1 || !e2 || *e1 || *e2)
            {
                err = "sexagesimal 解析失败：" + s;
                return false;
            }
            out = TypedValue::fromF(deg + (deg < 0 ? -min : min) / 60.0, seg.type);
            return true;
        }
        if (seg.hasFixedDigits)
        {
            const size_t dot = s.find('.');
            const int fracLen = (dot == std::string::npos) ? 0 : static_cast<int>(s.size() - dot - 1);
            if (fracLen != static_cast<int>(seg.fixedDigits))
            {
                err = "小数位应为 " + std::to_string(seg.fixedDigits) + "（实际 " + std::to_string(fracLen) + "）：" + s;
                return false;
            }
        }
        char *endp = nullptr;
        const double v = std::strtod(s.c_str(), &endp);
        if (endp == s.c_str() || (endp && *endp != '\0'))
        {
            err = "无法解析为数值：" + s;
            return false;
        }
        out = TypedValue::fromF(v, seg.type);
        return true;
    }

    if (s.find('.') != std::string::npos)
    {
        err = "整型字段不接受小数点：" + s;
        return false;
    }
    char *endp = nullptr;
    const long long v = std::strtoll(s.c_str(), &endp, 10);
    if (endp == s.c_str() || (endp && *endp != '\0'))
    {
        err = "无法解析为整数：" + s;
        return false;
    }
    const bool unsignedType = seg.type == ValueType::U64 || seg.type == ValueType::U32 ||
                              seg.type == ValueType::U16 || seg.type == ValueType::U8;
    out = unsignedType ? TypedValue::fromU(static_cast<uint64_t>(v), seg.type)
                       : TypedValue::fromI(v, seg.type);
    return true;
}

std::vector<std::string> splitTokens(const uint8_t *p, size_t from, size_t to, const CompiledSeg &seg)
{
    std::vector<std::string> out;
    if (from >= to)
    {
        out.emplace_back();
        return out;
    }
    if (seg.blankDelim)
    {
        size_t i = from;
        while (i < to)
        {
            while (i < to && isBlankByte(static_cast<char>(p[i])))
                ++i;
            if (i >= to)
                break;
            size_t e = i;
            while (e < to && !isBlankByte(static_cast<char>(p[e])))
                ++e;
            out.push_back(std::string(reinterpret_cast<const char *>(p + i), e - i));
            i = e;
        }
        if (out.empty())
            out.emplace_back();
        return out;
    }
    size_t i = from;
    for (;;)
    {
        size_t ml = 0;
        const size_t at = findDelimiter(p, i, to, seg.delimiters, ml);
        out.push_back(std::string(reinterpret_cast<const char *>(p + i), at - i));
        if (at >= to)
            break;
        i = at + ml;
        if (i >= to)
        {
            out.emplace_back();
            break;
        }
    }
    return out;
}

// 单次记录解析的结果。
struct RecAttempt
{
    bool ok = false;
    bool needMore = false;
    size_t used = 0;
    DecodeError::Code code = DecodeError::None;
    std::string segment;
    std::string detail;
    std::vector<TypedVec> vals;
    int skipped = 0;
};

// 解析一条记录。p[0] 是帧首（已定位/已是记录开始），n 是记录逻辑尾（不含 terminator）。
// varIdx 非空时按变体段表解析（下标指向 prog.varSegs），用于 ASCII 变体按 msgid 分派。
bool attemptRecord(const CompiledProgram &prog, const uint8_t *p, size_t n, RecAttempt &r,
                   const std::vector<const CompiledSeg *> *segsOverride = nullptr)
{
    const size_t segCount = segsOverride ? segsOverride->size() : prog.segs.size();
    auto segAt = [&](size_t i) -> const CompiledSeg & {
        return segsOverride ? *(*segsOverride)[i] : prog.segs[i];
    };
    if (prog.slotTable.empty())
    {
        r.code = DecodeError::ConfigInvalid;
        r.detail = "没有段定义";
        return false;
    }
    r.vals.assign(prog.slotTable.size(), TypedVec{});
    std::vector<std::pair<size_t, size_t>> spans(prog.slotTable.size(), {kNoPos, kNoPos});

    size_t pos = 0;
    for (size_t si = 0; si < segCount; ++si)
    {
        const CompiledSeg &seg = segAt(si);

        if (seg.isLiteral)
        {
            if (pos + seg.literal.size() > n)
            {
                r.segment = seg.name;
                    r.needMore = true;
                return false;
            }
            if (std::memcmp(p + pos, seg.literal.data(), seg.literal.size()) != 0)
            {
                r.code = DecodeError::SyncMissing;
                r.segment = seg.name;
                r.detail = "期望字面量 " + seg.literal;
                return false;
            }
            spans[seg.slot] = {pos, pos + seg.literal.size()};
            pos += seg.literal.size();
            continue;
        }

        // 字节段：边界由 elemSize × count 推出（位域/变体仍走 readFrame）。
        if (!seg.isAscii)
        {
            const uint32_t elem = seg.elemSize > 0 ? seg.elemSize : 1;
            const uint32_t cnt = seg.countIsConst ? seg.constCount : 1;
            const uint32_t total = elem * cnt;
            if (pos + total > n)
            {
                r.segment = seg.name;
                r.needMore = true;
                return false;
            }
            TypedVec col;
            col.reset(seg.type);
            if (seg.type == ValueType::Text)
            {
                col.push(TypedValue::fromText(
                    std::string(reinterpret_cast<const char *>(p + pos), total)));
            }
            else
            {
                for (uint32_t k = 0; k < cnt; ++k)
                {
                    const uint8_t *q = p + pos + k * elem;
                    TypedValue v;
                    if (seg.type == ValueType::F32)
                    {
                        uint32_t bits = static_cast<uint32_t>(readIntAt(q, 4, seg.bigEndian));
                        float f;
                        std::memcpy(&f, &bits, 4);
                        v = TypedValue::fromF(f, ValueType::F32);
                    }
                    else if (seg.type == ValueType::F64)
                    {
                        uint64_t bits = readIntAt(q, 8, seg.bigEndian);
                        double d;
                        std::memcpy(&d, &bits, 8);
                        v = TypedValue::fromF(d, ValueType::F64);
                    }
                    else
                    {
                        const uint64_t raw = readIntAt(q, elem, seg.bigEndian);
                        v = isSignedKind(seg.type)
                                ? TypedValue::fromRawInt(static_cast<int64_t>(raw), seg.type)
                                : TypedValue::fromU(raw, seg.type);
                    }
                    if (seg.hasExpect && seg.expectIsConst)
                    {
                        const uint64_t want = seg.expectConst.isSigned()
                                                  ? static_cast<uint64_t>(seg.expectConst.asI64())
                                                  : seg.expectConst.asU64();
                        const uint64_t got = (seg.type == ValueType::F32 || seg.type == ValueType::F64)
                                                 ? static_cast<uint64_t>(v.asDouble())
                                                 : (v.isSigned() ? static_cast<uint64_t>(v.asI64())
                                                                 : v.asU64());
                        if (got != want)
                        {
                            r.code = DecodeError::SyncMissing;
                            r.segment = seg.name;
                            char buf[128];
                            std::snprintf(buf, sizeof(buf), "期望 0x%llX，实际 0x%llX",
                                          static_cast<unsigned long long>(want),
                                          static_cast<unsigned long long>(got));
                            r.detail = buf;
                            return false;
                        }
                    }
                    col.push(v);
                }
            }
            r.vals[seg.slot] = col;
            spans[seg.slot] = {pos, pos + total};
            pos += total;
            continue;
        }

        // 本段之后的第一个 literal（repeat 段的右边界）
        std::string nextLit;
        for (size_t j = si + 1; j < segCount; ++j)
        {
            if (segAt(j).isLiteral && !segAt(j).literal.empty())
            {
                nextLit = segAt(j).literal;
                break;
            }
        }

        if (seg.repeatAll)
        {
            size_t stop = n;
            if (!nextLit.empty())
            {
                size_t at = 0;
                if (!findBytes(p, pos, n, nextLit, at))
                {
                    // 等右边界时又见到帧首字面量 → 旧候选作废
                    if (prog.hasStartAnchor && nextLit != prog.startAnchor)
                    {
                        size_t anchorAt = 0;
                        if (findBytes(p, pos, n, prog.startAnchor, anchorAt))
                        {
                            r.code = DecodeError::RecordAborted;
                            r.segment = seg.name;
                            r.detail = "等 " + nextLit + " 时遇到新帧首 " + prog.startAnchor;
                            r.used = anchorAt;
                            return false;
                        }
                    }
                    r.segment = seg.name;
                    r.needMore = true;
                    return false;
                }
                stop = at;
            }
            const std::vector<std::string> toks = splitTokens(p, pos, stop, seg);
            if (!seg.repeatAll && seg.countIsConst && seg.constCount > 0 && toks.size() > seg.constCount)
            {
                r.code = DecodeError::TooManyFields;
                r.segment = seg.name;
                r.detail = "声明 " + std::to_string(seg.constCount) + " 个元素，实际切出 " +
                           std::to_string(toks.size()) + " 个";
                return false;
            }
            TypedVec col;
            col.reset(seg.type);
            for (const std::string &tok : toks)
            {
                if (tok.empty())
                {
                    if (seg.emptyPolicy == "skip")
                        continue;
                    if (seg.emptyPolicy == "raw")
                    {
                        col.push(TypedValue::fromText(std::string()));
                        continue;
                    }
                    if (seg.emptyPolicy == "nan" && seg.asciiType == 0)
                    {
                        col.push(TypedValue::fromF(std::numeric_limits<double>::quiet_NaN(), seg.type));
                        continue;
                    }
                    r.code = DecodeError::FieldInvalid;
                    r.segment = seg.name;
                    r.detail = "空字段（可用 empty:nan/skip）";
                    return false;
                }
                std::string perr;
                if (!checkPattern(tok, seg, perr))
                {
                    if (seg.onMismatch == 2)
                        continue;
                    if (seg.onMismatch == 1)
                    {
                        r.skipped++;
                        col.push(TypedValue::fromF(std::numeric_limits<double>::quiet_NaN(), ValueType::F64));
                        continue;
                    }
                    r.code = DecodeError::FieldInvalid;
                    r.segment = seg.name;
                    r.detail = perr + "（原文 " + tok + "）";
                    return false;
                }
                if (seg.asciiType == 1)
                {
                    col.push(TypedValue::fromText(tok));
                    continue;
                }
                TypedValue v;
                if (!parseNumber(tok, seg, v, perr))
                {
                    if (seg.onMismatch == 2)
                        continue;
                    if (seg.onMismatch == 1)
                    {
                        r.skipped++;
                        col.push(TypedValue::fromF(std::numeric_limits<double>::quiet_NaN(), ValueType::F64));
                        continue;
                    }
                    r.code = (seg.base == 16) ? DecodeError::CheckNotHex : DecodeError::FieldInvalid;
                    r.segment = seg.name;
                    r.detail = perr + "（原文 " + tok + "）";
                    return false;
                }
                col.push(v);
            }
            r.vals[seg.slot] = col;
            spans[seg.slot] = {pos, stop};
            pos = stop;
            continue;
        }

        // 单元素段
        size_t tokenStart = pos;
        size_t tokenEnd = n;
        size_t nextPos = n;
        if (!seg.key.empty())
        {
            // key=value：定位 "key="，值到下一个 , ; 空白 或记录尾
            const std::string pat = seg.key + "=";
            size_t at = 0;
            if (!findBytes(p, pos, n, pat, at))
            {
                r.segment = seg.name;
                if (prog.hasTerminator)
                {
                    r.code = DecodeError::TooFewFields;
                    r.detail = "记录中没有键 " + seg.key;
                    return false;
                }
                r.needMore = true;
                return false;
            }
            tokenStart = at + pat.size();
            size_t vend = tokenStart;
            while (vend < n && p[vend] != ',' && p[vend] != ';' && !isBlankByte(static_cast<char>(p[vend])))
                ++vend;
            tokenEnd = vend;
            nextPos = vend;
            while (nextPos < n && (p[nextPos] == ',' || p[nextPos] == ';' || isBlankByte(static_cast<char>(p[nextPos]))))
                ++nextPos;
        }
        else if (seg.hasWidth && seg.width > 0)
        {
            if (pos + seg.width > n)
            {
                r.segment = seg.name;
                r.needMore = true;
                return false;
            }
            tokenStart = pos;
            tokenEnd = pos + seg.width;
            nextPos = tokenEnd;
        }
        else if (seg.hasDelimiter)
        {
            bool found = false;
            if (seg.blankDelim)
            {
                while (tokenStart < n && isBlankByte(static_cast<char>(p[tokenStart])))
                    ++tokenStart;
                size_t e = tokenStart;
                while (e < n && !isBlankByte(static_cast<char>(p[e])))
                    ++e;
                tokenEnd = e;
                if (e < n)
                {
                    found = true;
                    nextPos = e;
                    while (nextPos < n && isBlankByte(static_cast<char>(p[nextPos])))
                        ++nextPos;
                }
            }
            else
            {
                size_t ml = 0;
                const size_t at = findDelimiter(p, pos, n, seg.delimiters, ml);
                if (at < n)
                {
                    found = true;
                    tokenEnd = at;
                    nextPos = at + ml;
                }
                else
                {
                    tokenStart = pos;
                }
            }
            if (!found)
            {
                // 没找到分隔符：token 被记录尾或随后的 literal 截断
                size_t stop = n;
                bool haveStop = true;
                if (!nextLit.empty())
                {
                    size_t at2 = 0;
                    if (findBytes(p, pos, n, nextLit, at2))
                        stop = at2;
                    else
                        haveStop = false;
                }
                else if (si + 1 < segCount)
                {
                    haveStop = false;
                }
                if (!haveStop)
                {
                    // 先看是不是新记录已经把旧候选截断（流式协议的正常重置）
                    size_t anchorAt = 0;
                    if (prog.hasStartAnchor && findBytes(p, pos, n, prog.startAnchor, anchorAt))
                    {
                        r.code = DecodeError::RecordAborted;
                        r.segment = seg.name;
                        r.detail = "等记录尾时遇到新帧首 " + prog.startAnchor;
                        r.used = anchorAt;
                        return false;
                    }
                    r.segment = seg.name;
                    if (prog.hasTerminator)
                    {
                        // 记录尾已经由 term 界定，还缺字段 → 字段数不足，不是数据不够
                        r.code = DecodeError::TooFewFields;
                        r.detail = "记录尾之前找不到段 " + seg.name;
                        return false;
                    }
                    r.needMore = true;
                    return false;
                }
                tokenEnd = stop;
                nextPos = stop;
            }
        }
        else if (seg.digestKind != 0)
        {
            // check 段：宽度由算法推导的十六进制字符数决定
            const size_t w = digestHexWidth(seg.digestAlg);
            if (w > 0)
            {
                if (pos + w > n)
                {
                    r.segment = seg.name;
                    r.needMore = true;
                    return false;
                }
                tokenEnd = pos + w;
                nextPos = tokenEnd;
            }
            else
            {
                tokenEnd = n;
                nextPos = n;
            }
        }
        else
        {
            // 没有定界符也不是 check：读到下一个 literal 或记录尾
            size_t stop = n;
            if (!nextLit.empty())
            {
                size_t at = 0;
                if (findBytes(p, pos, n, nextLit, at))
                    stop = at;
                else
                {
                    r.segment = seg.name;
                    r.needMore = true;
                    return false;
                }
            }
            tokenEnd = stop;
            nextPos = stop;
        }

        const std::string tok(reinterpret_cast<const char *>(p + tokenStart), tokenEnd - tokenStart);
        TypedVec col;
        col.reset(seg.type);
        if (tok.empty())
        {
            if (seg.emptyPolicy == "skip")
            {
                r.vals[seg.slot] = col;
                spans[seg.slot] = {tokenStart, tokenEnd};
                pos = nextPos;
                continue;
            }
            if (seg.emptyPolicy == "nan" && seg.asciiType == 0)
            {
                col.push(TypedValue::fromF(std::numeric_limits<double>::quiet_NaN(), seg.type));
                r.vals[seg.slot] = col;
                spans[seg.slot] = {tokenStart, tokenEnd};
                pos = nextPos;
                continue;
            }
            r.code = DecodeError::FieldInvalid;
            r.segment = seg.name;
            r.detail = "空字段（可用 empty:nan/skip）";
            return false;
        }
        std::string perr;
        if (!checkPattern(tok, seg, perr))
        {
            if (seg.onMismatch == 2)
            {
                pos = nextPos;
                continue;
            }
            if (seg.onMismatch == 1)
            {
                r.skipped++;
                col.push(TypedValue::fromF(std::numeric_limits<double>::quiet_NaN(), ValueType::F64));
                pos = nextPos;
                continue;
            }
            r.code = DecodeError::FieldInvalid;
            r.segment = seg.name;
            r.detail = perr + "（原文 " + tok + "）";
            return false;
        }
        if (seg.asciiType == 1)
        {
            col.push(TypedValue::fromText(tok));
        }
        else
        {
            TypedValue v;
            if (!parseNumber(tok, seg, v, perr))
            {
                if (seg.onMismatch == 2)
                {
                    pos = nextPos;
                    continue;
                }
                if (seg.onMismatch == 1)
                {
                    r.skipped++;
                    col.push(TypedValue::fromF(std::numeric_limits<double>::quiet_NaN(), ValueType::F64));
                    pos = nextPos;
                    continue;
                }
                // 值里混进了新帧首 → 旧候选被截断，不是字段本身非法
                size_t anchorAt = 0;
                if (prog.hasStartAnchor && findBytes(p, pos, n, prog.startAnchor, anchorAt))
                {
                    r.code = DecodeError::RecordAborted;
                    r.segment = seg.name;
                    r.detail = "解析段时遇到新帧首 " + prog.startAnchor;
                    r.used = anchorAt;
                    return false;
                }
                if (seg.base == 16 && tok.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
                    r.code = DecodeError::CheckNotHex;
                else
                    r.code = DecodeError::FieldInvalid;
                r.segment = seg.name;
                r.detail = perr + "（原文 " + tok + "）";
                return false;
            }
            col.push(v);
        }
        r.vals[seg.slot] = col;
        spans[seg.slot] = {tokenStart, tokenEnd};
        pos = nextPos;
    }

    // 摘要校验：算的是记录的连续原始字节（含分隔符），不是字段值拼回去。
    for (const CompiledSeg &seg : prog.segs)
    {
        if (seg.digestKind == 0)
            continue;
        size_t from = 0;
        if (seg.digestFrom >= 0)
        {
            const size_t idx = static_cast<size_t>(seg.digestFrom);
            if (idx < spans.size() && spans[idx].second != kNoPos)
                from = spans[idx].second;
        }
        size_t to = n;
        if (seg.digestTo == -3)
        {
            if (seg.slot >= 0 && spans[seg.slot].first != kNoPos)
                to = spans[seg.slot].first;
        }
        else if (seg.digestTo >= 0)
        {
            const size_t idx = static_cast<size_t>(seg.digestTo);
            if (idx < spans.size() && spans[idx].first != kNoPos)
                to = spans[idx].first;
        }
        if (to < from)
        {
            r.code = DecodeError::ConfigInvalid;
            r.segment = seg.name;
            r.detail = "digest 区间反了";
            return false;
        }
        uint64_t computed = 0;
        if (seg.digestKind == 1)
        {
            std::string derr;
            if (!computeDigest(seg.digestAlg, p + from, to - from, computed, derr))
            {
                r.code = DecodeError::ConfigInvalid;
                r.segment = seg.name;
                r.detail = derr;
                return false;
            }
        }
        else
        {
            ScriptDigestInput in;
            in.data = p + from;
            in.len = to - from;
            std::map<std::string, TypedVec> fields;
            for (size_t i = 0; i < r.vals.size() && i < prog.slotTable.size(); ++i)
                if (r.vals[i].size() > 0)
                    fields[prog.slotTable[i].name] = r.vals[i];
            in.fields = &fields;
            std::string derr;
            if (!computeScriptDigest(seg.digestScript, in, computed, derr))
            {
                r.code = DecodeError::FieldInvalid;
                r.segment = seg.name;
                r.detail = derr;
                return false;
            }
        }
        const TypedVec &checkCol = r.vals[seg.slot];
        if (checkCol.size() > 0)
        {
            const TypedValue got = checkCol.at(0);
            uint64_t actual = 0;
            if (got.type == ValueType::F64 || got.type == ValueType::F32)
                actual = static_cast<uint64_t>(got.asDouble());
            else if (got.isSigned())
                actual = static_cast<uint64_t>(got.asI64());
            else
                actual = got.asU64();
            if (actual != computed)
            {
                r.code = DecodeError::ChecksumMismatch;
                r.segment = seg.name;
                char buf[160];
                std::snprintf(buf, sizeof(buf), "期望 0x%llX，实际 0x%llX，区间 [%zu,%zu)",
                              static_cast<unsigned long long>(computed),
                              static_cast<unsigned long long>(actual), from, to);
                r.detail = buf;
                return false;
            }
        }
    }

    r.ok = true;
    r.used = pos;
    return true;
}

// 把一条记录的值追加到通道。
void emitRecord(const CompiledProgram &prog, const RecAttempt &r, int64_t t, ProtocolDecodeResult &out)
{
    out.skipped += r.skipped;
    // 顶层段与变体段都要出通道：变体记录里顶层段多半 emit=none，变体段才有值。
    std::vector<const CompiledSeg *> allSegs;
    for (const CompiledSeg &s : prog.segs)
        allSegs.push_back(&s);
    for (const CompiledSeg &s : prog.varSegs)
        allSegs.push_back(&s);
    for (const CompiledSeg *sp : allSegs)
    {
        const CompiledSeg &seg = *sp;
        if (seg.emitMode == 2) // none
            continue;
        if (seg.slot < 0 || static_cast<size_t>(seg.slot) >= r.vals.size())
            continue;
        const TypedVec &col = r.vals[seg.slot];
        if (col.size() == 0)
            continue;
        const int n = col.size();
        for (int i = 0; i < n; ++i)
        {
            std::string name = seg.prefix + seg.name;
            if (n > 1)
                name += "[" + std::to_string(i) + "]";
            const TypedValue v = col.at(i);
            // 通道类型：物理换算的段一律 f64，其余保持原生 typed。
            const bool phys = !seg.identityPhys;
            ProtocolChannel *ch = nullptr;
            for (auto &c : out.channels)
                if (c.name == name)
                {
                    ch = &c;
                    break;
                }
            if (!ch)
            {
                out.channels.push_back(ProtocolChannel{});
                ch = &out.channels.last();
                ch->name = name;
                ch->unit = seg.unit;
                ch->v.reset(phys ? ValueType::F64 : seg.type);
            }
            ch->t.push_back(t);
            if (phys)
            {
                const double raw = v.asDouble();
                ch->v.push(TypedValue::fromF(raw * seg.scale + seg.offset, ValueType::F64));
            }
            else
            {
                ch->v.push(v);
            }
        }
    }
}

} // namespace

StreamDecoder::StreamDecoder(const CompiledProgram &prog) : m_prog(prog)
{
    m_pending.reserve(prog.maxsize + 64);
}

StreamDecoder::~StreamDecoder() = default;

bool StreamDecoder::push(const QByteArray &chunk, ProtocolDecodeResult &out, std::string &err)
{
    return push(reinterpret_cast<const uint8_t *>(chunk.constData()),
                static_cast<size_t>(chunk.size()), out, err);
}

bool StreamDecoder::push(const uint8_t *data, size_t len, ProtocolDecodeResult &out, std::string &err)
{
    if (!data && len > 0)
    {
        err = "输入为空";
        return false;
    }
    m_pending.insert(m_pending.end(), data, data + len);

    const CompiledProgram &prog = m_prog;

    for (;;)
    {
        if (m_pending.empty())
            break;

        const uint8_t *p = m_pending.data();
        const size_t n = m_pending.size();

        // 1) 定位：有帧首字面量就先找它，把前面的垃圾丢掉。
        size_t start = 0;
        if (prog.hasStartAnchor)
        {
            size_t at = 0;
            if (!findBytes(p, 0, n, prog.startAnchor, at))
            {
                // 保留末尾 anchor.size()-1 字节，防止 anchor 被 chunk 边界切断
                const size_t keep = prog.startAnchor.size() > 1 ? prog.startAnchor.size() - 1 : 0;
                const size_t drop = n > keep ? n - keep : 0;
                if (drop > 0)
                {
                    m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<long>(drop));
                    out.skipped += drop;
                }
                break;
            }
            if (at > 0)
            {
                m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<long>(at));
                out.skipped += at;
                continue; // 重新取指针
            }
            start = 0;
        }
        (void)start;

        // 2) 剥帧尾：有 terminator 就找到记录逻辑尾；没有就先整块尝试。
        size_t bodyEnd = n;
        size_t consume = n;
        if (prog.hasTerminator)
        {
            size_t termAt = n;
            size_t termLen = 0;
            for (size_t i = 0; i < n; ++i)
            {
                for (const std::string &t : prog.terminator)
                {
                    if (t.empty() || i + t.size() > n)
                        continue;
                    if (std::memcmp(p + i, t.data(), t.size()) == 0)
                    {
                        termAt = i;
                        termLen = t.size();
                        break;
                    }
                }
                if (termAt != n)
                    break;
            }
            if (termAt == n)
            {
                if (n > prog.maxsize)
                {
                    ++m_overflow;
                    out.errors.push_back({DecodeError::OverflowMaxsize, m_streamOffset, "", "超过 maxsize 仍无 term"});
                    m_pending.clear();
                    m_streamOffset += n;
                    break;
                }
                break; // 等更多数据
            }
            bodyEnd = termAt;
            consume = termAt + termLen;
        }

        // 第二个帧首：一条记录不可能跨过它——流式协议里新 $ 出现即表示旧候选作废。
        size_t nextAnchor = n;
        const bool hasNextAnchor =
            prog.hasStartAnchor && findBytes(p, 1, n, prog.startAnchor, nextAnchor);
        const bool cutByAnchor = hasNextAnchor && nextAnchor < bodyEnd;
        const size_t limit = cutByAnchor ? nextAnchor : bodyEnd;

        // 空行（记录逻辑尾为 0）：直接吃掉 term，不算错误。
        if (prog.hasTerminator && bodyEnd == 0)
        {
            m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<long>(consume));
            m_streamOffset += consume;
            continue;
        }

        RecAttempt r;
        if (attemptRecord(prog, p, limit, r))
        {
            // 变体分派：顶层段解出 selector（如 NMEA msgid）→ 用变体段表重解整条记录。
            if (prog.hasSeries && prog.selectorSlot >= 0 &&
                static_cast<size_t>(prog.selectorSlot) < r.vals.size())
            {
                const TypedVec &sel = r.vals[static_cast<size_t>(prog.selectorSlot)];
                if (sel.size() == 1)
                {
                    const TypedValue sv = sel.at(0);
                    std::string key = (sv.type == ValueType::Text)
                                          ? ("s:" + sv.s)
                                          : ("n:" + std::to_string(sv.isSigned() ? sv.asI64()
                                                                               : static_cast<int64_t>(sv.asU64())));
                    int vi = -1;
                    for (size_t i = 0; i < prog.variants.size() && vi < 0; ++i)
                        for (const std::string &k : prog.variants[i].keys)
                            if (k == key || (sv.type == ValueType::Text && k == sv.s))
                            {
                                vi = static_cast<int>(i);
                                break;
                            }
                    if (vi >= 0)
                    {
                        // pragma: 变体段替换 payload 段，头部/校验段沿用顶层段
                        const CompiledVariant &cv = prog.variants[static_cast<size_t>(vi)];
                        int pIdx = -1;
                        for (size_t i = 0; i < prog.segs.size(); ++i)
                            if (prog.payloadSlot >= 0 && prog.segs[i].slot == prog.payloadSlot)
                            {
                                pIdx = static_cast<int>(i);
                                break;
                            }
                        std::vector<const CompiledSeg *> arr;
                        const size_t cut = (pIdx < 0) ? prog.segs.size() : static_cast<size_t>(pIdx);
                        for (size_t i = 0; i < cut; ++i)
                            arr.push_back(&prog.segs[i]);
                        for (int32_t k : cv.segs)
                            arr.push_back(&prog.varSegs[static_cast<size_t>(k)]);
                        if (pIdx >= 0)
                            for (size_t i = static_cast<size_t>(pIdx) + 1; i < prog.segs.size(); ++i)
                                arr.push_back(&prog.segs[i]);
                        RecAttempt r2;
                        if (attemptRecord(prog, p, limit, r2, &arr))
                            r = std::move(r2);
                    }
                    else if (prog.fallbackReject)
                    {
                        r.code = DecodeError::FieldInvalid;
                        r.segment = "series";
                        r.detail = "selector " + key + " 没有匹配的变体";
                    }
                }
            }
            // 时间轴：声明了 timestamp 就用它算 ticks，否则按记录序号。
            int64_t t = m_recordIndex;
            if (prog.hasTimestamp && prog.timestamp.constant)
            {
                t = static_cast<int64_t>(prog.timestamp.constValue.asDouble());
            }
            else if (prog.hasTimestamp)
            {
                TypedValue tv;
                std::string terr;
                if (runByteCode(prog.timestamp, r.vals, tv, terr) && tv.type != ValueType::Text)
                {
                    int64_t mult = 1;
                    if (prog.timestampUnit == "ms")
                        mult = 1000;
                    else if (prog.timestampUnit == "us")
                        mult = 1000000;
                    else if (prog.timestampUnit == "ns")
                        mult = 1000000000;
                    t = static_cast<int64_t>(std::llround(tv.asDouble() * static_cast<double>(mult)));
                    out.ticksPerSecond = mult;
                }
            }
            m_recordIndex++;
            emitRecord(prog, r, t, out);
            out.ok = true;
            // 有 term：消费到 term 结束；无 term：消费到 check 读满的位置（剩余留给下一条）。
            size_t take = prog.hasTerminator ? consume : r.used;
            if (cutByAnchor && take > limit)
                take = limit;
            if (take == 0)
                take = 1;
            if (take > m_pending.size())
                take = m_pending.size();
            m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<long>(take));
            m_streamOffset += take;
            continue;
        }

        if (r.needMore && prog.hasTerminator && !cutByAnchor)
        {
            // 已剥掉 term 却还缺字段：这条记录不完整，按坏帧丢弃，不占着缓冲等。
            r.needMore = false;
            r.code = DecodeError::FieldInvalid;
            if (r.detail.empty())
                r.detail = "记录不完整（缺字段/缺边界）";
        }

        if (r.needMore)
        {
            if (cutByAnchor)
            {
                // 旧候选在等边界时被新帧首截断：整段丢弃，从新帧首重来。
                ++m_dropped;
                if (out.errors.size() < 128)
                    out.errors.push_back({DecodeError::RecordAborted, m_streamOffset, r.segment,
                                          "等边界时出现新帧首 " + prog.startAnchor});
                m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<long>(nextAnchor));
                m_streamOffset += nextAnchor;
                continue;
            }
            if (m_pending.size() > prog.maxsize)
            {
                ++m_overflow;
                out.errors.push_back({DecodeError::OverflowMaxsize, m_streamOffset, r.segment,
                                      "超过 maxsize 仍不完整"});
                m_pending.clear();
                break;
            }
            break; // 一直等
        }

        // 3) 解析失败：计数 + 丢到可恢复位置
        const bool aborted = cutByAnchor || (r.code == DecodeError::RecordAborted);
        if (aborted)
            ++m_dropped;
        else
            ++m_rejected;
        // 记录被新帧首截断时，具体字段错误都归为"作废"，否则上层无法区分坏帧与正常重置。
        DecodeError::Code code = r.code;
        if (aborted && code != DecodeError::RecordAborted && code != DecodeError::OverflowMaxsize)
            code = DecodeError::RecordAborted;
        if (out.errors.size() < 128)
            out.errors.push_back({code, m_streamOffset, r.segment, r.detail});

        size_t drop = cutByAnchor ? nextAnchor : consume;
        if (r.code == DecodeError::RecordAborted && r.used > 0 && r.used < drop)
            drop = r.used; // 停在新帧首之前
        if (drop == 0)
            drop = 1; // 保证前进
        if (drop > m_pending.size())
            drop = m_pending.size();
        m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<long>(drop));
        m_streamOffset += drop;
    }

    err.clear();
    return true;
}

void StreamDecoder::finish(ProtocolDecodeResult &out)
{
    if (!m_pending.empty())
    {
        out.errors.push_back(
            {DecodeError::IncompleteAtEof, m_streamOffset, "", "EOF 时仍有未闭合候选"});
        ++m_dropped;
        m_pending.clear();
    }
}

quint64 StreamDecoder::dropped() const
{
    return m_dropped;
}

quint64 StreamDecoder::rejected() const
{
    return m_rejected;
}

quint64 StreamDecoder::overflow() const
{
    return m_overflow;
}

} // namespace nextpilot::plotscope::protocol
