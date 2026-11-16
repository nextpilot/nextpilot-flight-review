#include "UlogParser.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <tuple>

namespace {

constexpr uint8_t kMagic[7] = {'U', 'L', 'o', 'g', 0x01, 0x12, 0x35};

enum class Prim { I8, U8, I16, U16, I32, U32, I64, U64, F32, F64, Bool, Char };

size_t primSize(Prim p)
{
    switch (p)
    {
    case Prim::I8: case Prim::U8: case Prim::Bool: case Prim::Char: return 1;
    case Prim::I16: case Prim::U16: return 2;
    case Prim::I32: case Prim::U32: case Prim::F32: return 4;
    case Prim::I64: case Prim::U64: case Prim::F64: return 8;
    }
    return 0;
}

bool primFromName(const std::string &s, Prim &p)
{
    static const std::map<std::string, Prim> map{
        {"int8_t", Prim::I8}, {"uint8_t", Prim::U8}, {"int16_t", Prim::I16},
        {"uint16_t", Prim::U16}, {"int32_t", Prim::I32}, {"uint32_t", Prim::U32},
        {"int64_t", Prim::I64}, {"uint64_t", Prim::U64}, {"float", Prim::F32},
        {"double", Prim::F64}, {"bool", Prim::Bool}, {"char", Prim::Char},
    };
    auto it = map.find(s);
    if (it == map.end())
        return false;
    p = it->second;
    return true;
}

bool readPrim(const uint8_t *b, size_t len, size_t off, Prim p, double &out)
{
    if (off + primSize(p) > len)
        return false;
    switch (p)
    {
    case Prim::I8: out = static_cast<double>(static_cast<int8_t>(b[off])); return true;
    case Prim::U8: case Prim::Char: out = static_cast<double>(b[off]); return true;
    case Prim::Bool: out = b[off] != 0 ? 1.0 : 0.0; return true;
    case Prim::I16: out = static_cast<double>(static_cast<int16_t>(b[off] | (b[off + 1] << 8))); return true;
    case Prim::U16: out = static_cast<double>(b[off] | (b[off + 1] << 8)); return true;
    case Prim::I32:
    {
        const int32_t v = static_cast<int32_t>(static_cast<uint32_t>(b[off]) |
            (uint32_t(b[off + 1]) << 8) | (uint32_t(b[off + 2]) << 16) | (uint32_t(b[off + 3]) << 24));
        out = static_cast<double>(v);
        return true;
    }
    case Prim::U32:
        out = static_cast<double>(uint32_t(b[off]) | (uint32_t(b[off + 1]) << 8) |
                                  (uint32_t(b[off + 2]) << 16) | (uint32_t(b[off + 3]) << 24));
        return true;
    case Prim::F32:
    {
        float v = 0;
        std::memcpy(&v, b + off, 4);
        out = static_cast<double>(v);
        return true;
    }
    case Prim::I64:
    {
        int64_t v = 0;
        std::memcpy(&v, b + off, 8);
        out = static_cast<double>(v);
        return true;
    }
    case Prim::U64:
    {
        uint64_t v = 0;
        std::memcpy(&v, b + off, 8);
        out = static_cast<double>(v);
        return true;
    }
    case Prim::F64:
    {
        double v = 0;
        std::memcpy(&v, b + off, 8);
        out = v;
        return true;
    }
    }
    return false;
}

struct Base
{
    bool isPrim = false;
    Prim prim = Prim::U8;
    std::string msg;
};

struct Field
{
    std::string name;
    Base base;
    size_t array = 0;
};

struct Leaf
{
    std::string path;
    size_t off = 0;
    bool hasPrim = false;
    Prim prim = Prim::U8;
    bool keepValue = true;
};

struct Subscription
{
    std::string topic;
    uint8_t multiId = 0;
    std::vector<Leaf> leaves;
    std::optional<size_t> tsOff;
    std::vector<double> t;
    std::vector<std::vector<double>> vals;
};

std::string trim(const std::string &s)
{
    const size_t lb = s.find_first_not_of(" \t\r\n");
    const size_t rb = s.find_last_not_of(" \t\r\n");
    return lb == std::string::npos ? std::string() : s.substr(lb, rb - lb + 1);
}

bool splitFormat(const std::string &text, std::string &name, std::vector<Field> &fields)
{
    const size_t colon = text.find(':');
    if (colon == std::string::npos)
        return false;
    name = trim(text.substr(0, colon));
    if (name.empty())
        return false;
    fields.clear();
    size_t start = colon + 1;
    while (start <= text.size())
    {
        const size_t semi = text.find(';', start);
        const std::string raw = trim(text.substr(start, semi == std::string::npos ? std::string::npos : semi - start));
        if (!raw.empty())
        {
            size_t space = raw.find(' ');
            if (space == std::string::npos)
                return false;
            std::string ty = trim(raw.substr(0, space));
            std::string fname = trim(raw.substr(space + 1));
            if (fname.empty())
                return false;
            size_t array = 0;
            const size_t lb = ty.find('[');
            if (lb != std::string::npos)
            {
                const size_t rb = ty.find(']', lb);
                if (rb == std::string::npos)
                    return false;
                array = static_cast<size_t>(std::stoul(ty.substr(lb + 1, rb - lb - 1)));
                ty = ty.substr(0, lb);
            }
            if (array == 0)
            {
                const size_t flb = fname.find('[');
                if (flb != std::string::npos)
                {
                    const size_t frb = fname.find(']', flb);
                    if (frb == std::string::npos)
                        return false;
                    array = static_cast<size_t>(std::stoul(fname.substr(flb + 1, frb - flb - 1)));
                    fname = fname.substr(0, flb);
                }
            }
            Base base;
            Prim p;
            if (primFromName(ty, p))
            {
                base.isPrim = true;
                base.prim = p;
            }
            else
            {
                base.msg = ty;
            }
            fields.push_back({fname, base, array});
        }
        if (semi == std::string::npos)
            break;
        start = semi + 1;
    }
    return true;
}

bool flatten(const std::map<std::string, std::vector<Field>> &formats, const std::string &name,
             size_t off, const std::string &prefix, std::vector<Leaf> &out, size_t depth,
             size_t &consumed)
{
    if (depth > 16)
        return false;
    auto it = formats.find(name);
    if (it == formats.end())
        return false;
    size_t cur = off;
    for (const Field &f : it->second)
    {
        const std::string path = prefix.empty() ? f.name : prefix + "." + f.name;
        const size_t repeated = std::max<size_t>(f.array, 1);
        const bool skip = f.name == "timestamp" || f.name.rfind("_padding", 0) == 0;
        if (f.base.isPrim && f.base.prim == Prim::Char && f.array > 0)
        {
            Leaf l;
            l.path = path;
            l.off = cur;
            l.hasPrim = false;
            l.keepValue = false;
            out.push_back(std::move(l));
            cur += f.array;
        }
        else if (f.base.isPrim)
        {
            for (size_t k = 0; k < repeated; ++k)
            {
                Leaf l;
                l.path = repeated > 1 ? path + "." + (k < 10 ? "0" : "") + std::to_string(k) : path;
                l.off = cur;
                l.hasPrim = true;
                l.prim = f.base.prim;
                l.keepValue = !skip;
                out.push_back(std::move(l));
                cur += primSize(f.base.prim);
            }
        }
        else
        {
            for (size_t k = 0; k < repeated; ++k)
            {
                const std::string sub = repeated > 1 ? path + "." + (k < 10 ? "0" : "") + std::to_string(k) : path;
                size_t child = 0;
                if (!flatten(formats, f.base.msg, cur, sub, out, depth + 1, child))
                    return false;
                cur += child;
            }
        }
    }
    consumed = cur - off;
    return true;
}

bool u16At(const std::vector<uint8_t> &b, size_t off, uint16_t &v)
{
    if (off + 2 > b.size())
        return false;
    v = static_cast<uint16_t>(b[off] | (b[off + 1] << 8));
    return true;
}

bool decodeParam(const uint8_t *p, size_t len, std::string &key, std::string &value)
{
    if (len < 3)
        return false;
    const uint8_t keyLen = p[2];
    if (3 + keyLen + 1 > len)
        return false;
    key.assign(reinterpret_cast<const char *>(p + 3), keyLen);
    if (key.empty())
        return false;
    const char ty = static_cast<char>(p[3 + keyLen]);
    const uint8_t *d = p + 3 + keyLen + 1;
    const size_t dlen = len - (3 + keyLen + 1);
    auto str = [](auto v) { return std::to_string(v); };
    switch (ty)
    {
    case 'b': if (dlen < 1) return false; value = std::to_string(static_cast<int8_t>(d[0])); return true;
    case 'B': if (dlen < 1) return false; value = std::to_string(d[0]); return true;
    case 'h': if (dlen < 2) return false; value = std::to_string(static_cast<int16_t>(d[0] | (d[1] << 8))); return true;
    case 'H': if (dlen < 2) return false; value = std::to_string(d[0] | (d[1] << 8)); return true;
    case 'i': if (dlen < 4) return false; value = str(static_cast<int32_t>(uint32_t(d[0]) | (uint32_t(d[1]) << 8) | (uint32_t(d[2]) << 16) | (uint32_t(d[3]) << 24))); return true;
    case 'I': if (dlen < 4) return false; value = str(uint32_t(d[0]) | (uint32_t(d[1]) << 8) | (uint32_t(d[2]) << 16) | (uint32_t(d[3]) << 24)); return true;
    case 'q': case 'Q':
    {
        if (dlen < 8) return false;
        uint64_t v = 0;
        std::memcpy(&v, d, 8);
        value = std::to_string(ty == 'q' ? static_cast<int64_t>(v) : v);
        return true;
    }
    case 'f': if (dlen < 4) return false; { float v = 0; std::memcpy(&v, d, 4); value = std::to_string(v); return true; }
    case 'd': if (dlen < 8) return false; { double v = 0; std::memcpy(&v, d, 8); value = std::to_string(v); return true; }
    default: return false;
    }
}

std::string readCString(const uint8_t *p, size_t len)
{
    size_t n = 0;
    while (n < len && p[n] != 0)
        ++n;
    return std::string(reinterpret_cast<const char *>(p), n);
}

} // namespace

bool psParseUlog(const char *path, UlogLog &log, std::string &err)
{
    std::vector<uint8_t> buf;
    if (!psReadFile(path, buf, err))
        return false;
    if (buf.size() < 16)
    {
        err = "文件太短，不是 ULog";
        return false;
    }
    if (std::memcmp(buf.data(), kMagic, 7) != 0)
    {
        err = "magic 不匹配（不是 ULog v1）";
        return false;
    }
    if (buf[7] != 1)
    {
        err = "不支持的 ULog 版本 v" + std::to_string(buf[7]);
        return false;
    }

    std::map<std::string, std::vector<Field>> formats;
    std::vector<Subscription> subs;
    std::map<uint16_t, size_t> byId;
    std::map<std::pair<std::string, uint8_t>, size_t> byTopic;
    std::vector<std::pair<std::string, std::string>> params;
    size_t pos = 16;

    while (pos + 3 <= buf.size())
    {
        uint16_t size = 0;
        if (!u16At(buf, pos, size))
            break;
        const uint8_t ty = buf[pos + 2];
        const size_t body = pos + 3;
        if (body + size > buf.size())
            break;
        const uint8_t *p = buf.data() + body;

        if (ty == 'F')
        {
            std::string text(reinterpret_cast<const char *>(p), size);
            std::string name;
            std::vector<Field> fields;
            if (splitFormat(text, name, fields))
                formats[name] = std::move(fields);
        }
        else if (ty == 'A')
        {
            if (size >= 3)
            {
                const uint8_t multiId = p[0];
                const uint16_t msgId = static_cast<uint16_t>(p[1] | (p[2] << 8));
                const std::string name = readCString(p + 3, size - 3);
                auto fit = formats.find(name);
                if (fit == formats.end())
                {
                    pos = body + size;
                    continue;
                }
                std::vector<Leaf> leaves;
                size_t consumed = 0;
                if (!flatten(formats, name, 0, "", leaves, 0, consumed))
                {
                    pos = body + size;
                    continue;
                }
                std::optional<size_t> tsOff;
                for (const Leaf &l : leaves)
                    if (l.path == "timestamp" && l.hasPrim && l.prim == Prim::U64)
                    {
                        tsOff = l.off;
                        break;
                    }
                if (!tsOff)
                {
                    pos = body + size;
                    continue;
                }
                if (byTopic.count({name, multiId}))
                {
                    pos = body + size;
                    continue;
                }
                size_t numeric = 0;
                for (const Leaf &l : leaves)
                    if (l.hasPrim && l.keepValue)
                        ++numeric;
                const size_t idx = subs.size();
                Subscription s;
                s.topic = name;
                s.multiId = multiId;
                s.leaves = std::move(leaves);
                s.tsOff = tsOff;
                s.vals.resize(numeric);
                byTopic[{name, multiId}] = idx;
                byId[msgId] = idx;
                subs.push_back(std::move(s));
            }
        }
        else if (ty == 'D')
        {
            uint16_t msgId = 0;
            if (u16At(std::vector<uint8_t>(p, p + size), 0, msgId))
            {
                auto bit = byId.find(msgId);
                if (bit != byId.end())
                {
                    Subscription &s = subs[bit->second];
                    double ts = 0;
                    if (s.tsOff && readPrim(p + 2, size - 2, *s.tsOff, Prim::U64, ts))
                    {
                        s.t.push_back(ts / 1e6);
                        size_t k = 0;
                        for (const Leaf &l : s.leaves)
                        {
                            if (!l.hasPrim || !l.keepValue)
                                continue;
                            double v = 0;
                            if (!readPrim(p + 2, size - 2, l.off, l.prim, v))
                                v = std::numeric_limits<double>::quiet_NaN();
                            if (k < s.vals.size())
                                s.vals[k].push_back(v);
                            ++k;
                        }
                    }
                }
            }
        }
        else if (ty == 'P' || ty == 'Q')
        {
            std::string key, value;
            if (decodeParam(p, size, key, value))
            {
                bool found = false;
                for (auto &kv : params)
                    if (kv.first == key)
                    {
                        kv.second = value;
                        found = true;
                        break;
                    }
                if (!found)
                    params.emplace_back(key, value);
            }
        }
        else if (ty == 'R')
        {
            uint16_t msgId = 0;
            if (size >= 2 && u16At(std::vector<uint8_t>(p, p + size), 0, msgId))
                byId.erase(msgId);
        }
        pos = body + size;
    }

    std::map<std::string, uint8_t> multiTopics;
    for (const Subscription &s : subs)
        multiTopics[s.topic] = std::max(multiTopics[s.topic], s.multiId);

    for (auto &s : subs)
    {
        if (s.t.empty())
            continue;
        const std::string topic = multiTopics[s.topic] > 0
            ? s.topic + "." + (s.multiId < 10 ? "0" : "") + std::to_string(s.multiId)
            : s.topic;
        size_t k = 0;
        for (const Leaf &l : s.leaves)
        {
            if (!l.hasPrim || !l.keepValue)
                continue;
            if (k >= s.vals.size())
                break;
            PsChannelData c;
            c.name = topic + "." + l.path;
            c.t = s.t;
            c.v = std::move(s.vals[k]);
            log.channels.push_back(std::move(c));
            ++k;
        }
    }
    if (log.channels.empty())
    {
        err = "没有可绘制的数值通道（可能只有参数/文本）";
        return false;
    }
    log.params = std::move(params);
    return true;
}
