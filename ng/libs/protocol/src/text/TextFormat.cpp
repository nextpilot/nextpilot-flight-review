// text 段的格式解析：把一段 scanf 风格的 format 编译成"字面 / 转换符 / 字符集"序列，
// 再按字节把元素读出来。这里只管字节与文本，不碰帧结构、不碰通道——帧流程仍在 StreamDecoder。
#include "protocol/TextFormat.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace nextpilot::plotscope::protocol
{

// ---------------------------------------------------------------- text：扩展 scanf 扫描（SPEC §10）
// 一个段 = 一个 format；format 里最多一个取值转换符（其余只能是字面 / %* 丢弃）。


bool textCompileFormat(const std::string &f, std::vector<TextFmtPart> &out, std::string &err)
{
    for (size_t i = 0; i < f.size(); ++i)
    {
        TextFmtPart part;
        if (f[i] != '%')
        {
            part.kind = 0;
            std::string lit;
            while (i < f.size() && f[i] != '%')
                lit.push_back(f[i++]);
            --i;
            part.lit = lit;
            out.push_back(part);
            continue;
        }
        if (i + 1 < f.size() && f[i + 1] == '%')
        {
            part.kind = 0;
            part.lit = "%";
            out.push_back(part);
            ++i;
            continue;
        }
        part.kind = 1;
        ++i;
        if (i < f.size() && f[i] == '*')
        {
            part.discard = true;
            ++i;
        }
        std::string w;
        while (i < f.size() && std::isdigit(static_cast<unsigned char>(f[i])))
            w.push_back(f[i++]);
        part.width = w.empty() ? 0 : std::atoi(w.c_str());
        if (i >= f.size())
        {
            err = "格式串以 % 结尾";
            return false;
        }
        if (f[i] == '[')
        {
            part.kind = 2;
            ++i;
            if (i < f.size() && f[i] == '^')
            {
                part.negate = true;
                ++i;
            }
            while (i < f.size() && f[i] != ']')
                part.set.push_back(f[i++]);
            if (i >= f.size())
            {
                err = "字符集缺少 ]";
                return false;
            }
            out.push_back(part);
            continue;
        }
        static const std::string convs = "duixXobfegcs";
        if (!convs.empty() && convs.find(f[i]) == std::string::npos && f[i] != 't' && f[i] != 'T')
        {
            err = std::string("未知转换符 %") + f[i];
            return false;
        }
        part.conv = f[i];
        out.push_back(part);
    }
    return true;
}

// 段开始/元素之间吃掉一个分隔符（blank 用空白字符类）
size_t textEatDelimiter(const uint8_t *p, size_t pos, size_t n, const CompiledSeg &seg)
{
    if (seg.blankDelim)
    {
        if (pos < n && isBlankByte(static_cast<char>(p[pos])))
            return pos + 1;
        return pos;
    }
    if (seg.delimiters.empty())
    {
        if (pos < n && p[pos] == ',')
            return pos + 1;
        return pos;
    }
    for (const std::string &d : seg.delimiters)
        if (d.size() && pos + d.size() <= n &&
            std::memcmp(p + pos, d.data(), d.size()) == 0)
            return pos + d.size();
    return pos;
}

// 读一个元素：按 format 的步走，产出一个原始字符串值（最多一个）
bool textScanElement(const uint8_t *p, size_t n, size_t &pos, const std::vector<TextFmtPart> &parts,
                 const CompiledSeg &seg, const std::string &stopLit, std::string &val, char &conv,
                 bool &hasVal, std::string &err, bool &needMore)
{
    // 下一段的起始字面（NMEA 的 "*"）也算本段的右边界，否则 %s 会一路吃到行尾
    auto atStopLit = [&](size_t at) {
        return !stopLit.empty() && at + stopLit.size() <= n &&
               std::memcmp(p + at, stopLit.data(), stopLit.size()) == 0;
    };
    auto isDelimAt = [&](size_t at) {
        if (seg.blankDelim)
            return at < n && isBlankByte(static_cast<char>(p[at]));
        if (seg.delimiters.empty())
            return at < n && p[at] == ',';
        for (const std::string &d : seg.delimiters)
            if (d.size() && at + d.size() <= n && std::memcmp(p + at, d.data(), d.size()) == 0)
                return true;
        return false;
    };
    for (const TextFmtPart &part : parts)
    {
        if (part.kind == 0)
        {
            // 字面：允许前面有空白，然后逐字匹配
            while (pos < n && isBlankByte(static_cast<char>(p[pos])))
                ++pos;
            if (pos + part.lit.size() > n)
            {
                needMore = true;
                err = "字面 \"" + part.lit + "\" 尚未到齐";
                return false;
            }
            for (size_t k = 0; k < part.lit.size(); ++k)
                if (static_cast<char>(p[pos + k]) != part.lit[k])
                {
                    err = "字面 \"" + part.lit + "\" 不匹配";
                    return false;
                }
            pos += part.lit.size();
            continue;
        }
        if (part.kind == 2)
        {
            // 字符集：不跳空白
            const size_t start = pos;
            while (pos < n)
            {
                const char c = static_cast<char>(p[pos]);
                const bool in = part.set.find(c) != std::string::npos;
                if (part.negate ? in : !in)
                    break;
                if (part.width && static_cast<int>(pos - start) >= part.width)
                    break;
                ++pos;
            }
            const std::string tok(reinterpret_cast<const char *>(p + start), pos - start);
            if (!part.discard)
            {
                if (hasVal)
                {
                    err = "一段只能取一个值";
                    return false;
                }
                val = tok;
                conv = 's';
                hasVal = true;
            }
            continue;
        }
        // 转换符
        if (part.conv != 'c')
            while (pos < n && isBlankByte(static_cast<char>(p[pos])))
                ++pos;
        size_t start = pos;
        if (part.conv == 'c')
        {
            const int w = part.width > 0 ? part.width : 1;
            if (pos + static_cast<size_t>(w) > n)
            {
                needMore = true;
                err = "%c 剩余字符不足";
                return false;
            }
            pos += static_cast<size_t>(w);
        }
        else if (part.conv == 's')
        {
            while (pos < n)
            {
                if (isDelimAt(pos) || isBlankByte(static_cast<char>(p[pos])) || atStopLit(pos))
                    break;
                if (part.width && static_cast<int>(pos - start) >= part.width)
                    break;
                ++pos;
            }
        }
        else if (part.conv == 't' || part.conv == 'T')
        {
            while (pos < n && !isDelimAt(pos) && !atStopLit(pos))
                ++pos;
        }
        else
        {
            // 数字：按转换符的字符集扫，遇分隔符/空白停
            const char c = part.conv;
            auto digitOk = [c](char ch) {
                switch (c)
                {
                case 'x':
                case 'X':
                    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
                case 'o':
                    return ch >= '0' && ch <= '7';
                case 'b':
                    return ch == '0' || ch == '1';
                case 'f':
                case 'e':
                case 'g':
                    return std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' || ch == 'e' ||
                           ch == 'E' || ch == '+' || ch == '-';
                default:
                    return std::isdigit(static_cast<unsigned char>(ch)) != 0;
                }
            };
            if (pos < n && (p[pos] == '-' || p[pos] == '+'))
                ++pos;
            if (c == 'x' || c == 'X')
            {
                if (pos + 1 < n && p[pos] == '0' && (p[pos + 1] == 'x' || p[pos + 1] == 'X'))
                    pos += 2;
            }
            while (pos < n && !atStopLit(pos) && digitOk(static_cast<char>(p[pos])))
            {
                if (part.width && static_cast<int>(pos - start) >= part.width)
                    break;
                ++pos;
            }
        }
        const std::string tok(reinterpret_cast<const char *>(p + start), pos - start);
        if (part.discard)
            continue;
        if (hasVal)
        {
            err = "一段只能取一个值";
            return false;
        }
        val = tok;
        conv = part.conv;
        hasVal = true;
    }
    return true;
}

bool isBlankByte(char c)
{
    return c == ' ' || c == '\t';
}

} // namespace nextpilot::plotscope::protocol
