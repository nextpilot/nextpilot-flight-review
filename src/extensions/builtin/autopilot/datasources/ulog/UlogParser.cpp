#include "UlogParser.h"
#include "PsLoaderCommon.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <tuple>

namespace {

constexpr uint8_t kMagic[7] = {'U', 'L', 'o', 'g', 0x01, 0x12, 0x35};

enum class Prim { I8, U8, I16, U16, I32, U32, I64, U64, F32, F64, Bool, Char };

uint32_t psTypeFromPrim(Prim p)
{
    switch (p)
    {
    case Prim::I8: return PS_VALUE_I8;
    case Prim::U8: return PS_VALUE_U8;
    case Prim::I16: return PS_VALUE_I16;
    case Prim::U16: return PS_VALUE_U16;
    case Prim::I32: return PS_VALUE_I32;
    case Prim::U32: return PS_VALUE_U32;
    case Prim::I64: return PS_VALUE_I64;
    case Prim::U64: return PS_VALUE_U64;
    case Prim::F32: return PS_VALUE_F32;
    case Prim::F64: return PS_VALUE_F64;
    case Prim::Bool: return PS_VALUE_BOOL;
    default: return PS_VALUE_TEXT;
    }
}

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

bool parseSize(const std::string &s, size_t &out)
{
    if (s.empty())
        return false;
    size_t v = 0;
    for (char c : s)
    {
        if (c < '0' || c > '9')
            return false;
        const size_t d = static_cast<size_t>(c - '0');
        if (v > (std::numeric_limits<size_t>::max() - d) / 10)
            return false;
        v = v * 10 + d;
    }
    out = v;
    return true;
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
                if (!parseSize(ty.substr(lb + 1, rb - lb - 1), array))
                    return false;
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
                    if (!parseSize(fname.substr(flb + 1, frb - flb - 1), array))
                        return false;
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

std::string infoValue(const std::string &key, const uint8_t *p, size_t n)
{
    if ((key.find("ver") != std::string::npos || key.find("revision") != std::string::npos ||
         key.find("count") != std::string::npos || key.find("bits") != std::string::npos) && n >= 4)
    {
        uint32_t v = 0;
        std::memcpy(&v, p, 4);
        return std::to_string(v);
    }
    return readCString(p, n);
}

void addInfo(std::vector<std::pair<std::string, std::string>> &out, const uint8_t *p, size_t n)
{
    if (n < 1) return;
    const size_t kl = p[0];
    if (n < 1 + kl) return;
    const std::string key = readCString(p + 1, kl);
    out.emplace_back(key, infoValue(key, p + 1 + kl, n - 1 - kl));
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

    UlogLog meta;
    {
        uint64_t start = 0;
        std::memcpy(&start, buf.data() + 8, 8);
        meta.facts.emplace_back("start_timestamp_us", std::to_string(start));
        meta.facts.emplace_back("file_version", std::to_string(buf[7]));
        log.startTimestampUs = start;
        log.fileVersion = buf[7];
    }
    size_t pos = 16;
    double lastTimestamp = 0.0;

    while (pos + 3 <= buf.size())
    {
        psLoaderProgress(10 + pos * 90 / std::max<size_t>(1, buf.size()), 100);
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
            if (splitFormat(text, name, fields)) {
                formats[name] = std::move(fields);
                log.formats.push_back(name);
            }
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
                    log.missingFormats.push_back(name);
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
                log.subscriptions.push_back({msgId, name, multiId});
                subs.push_back(std::move(s));
            }
        }
        else if (ty == 'D')
        {
            if (size >= 2)
            {
                const uint16_t msgId = static_cast<uint16_t>(p[0] | (p[1] << 8));
                auto bit = byId.find(msgId);
                if (bit != byId.end())
                {
                    Subscription &s = subs[bit->second];
                    double ts = 0;
                    if (s.tsOff && readPrim(p + 2, size - 2, *s.tsOff, Prim::U64, ts))
                    {
                        s.t.push_back(ts / 1e6);
                        lastTimestamp = ts / 1e6;
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
                auto &target = (ty == 'Q') ? meta.defaultParams : params;
                bool found = false;
                for (auto &kv : target)
                    if (kv.first == key)
                    {
                        kv.second = value;
                        found = true;
                        break;
                    }
                if (!found)
                    target.emplace_back(key, value);
                if (ty == 'P' && lastTimestamp > 0)
                    meta.changedParams.emplace_back(lastTimestamp, std::make_pair(key, value));
            }
        }
        else if (ty == 'I')
        {
            addInfo(meta.facts, p, size);
        }
        else if (ty == 'M')
        {
            if (size >= 1) addInfo(meta.facts, p + 1, size - 1);
        }
        else if (ty == 'L' && size >= 9)
        {
            UlogEvent m;
            m.level = p[0];
            uint64_t ts = 0;
            std::memcpy(&ts, p + 1, 8);
            m.t = static_cast<double>(ts) / 1e6;
            lastTimestamp = m.t;
            m.text = readCString(p + 9, size - 9);
            meta.events.push_back(std::move(m));
        }
        else if (ty == 'C' && size >= 11)
        {
            UlogEvent m;
            m.level = p[0];
            std::memcpy(&m.tag, p + 1, 2);
            uint64_t ts = 0;
            std::memcpy(&ts, p + 3, 8);
            m.t = static_cast<double>(ts) / 1e6;
            lastTimestamp = m.t;
            m.text = readCString(p + 11, size - 11);
            m.tagged = true;
            meta.events.push_back(std::move(m));
        }
        else if (ty == 'O' && size >= 10)
        {
            uint64_t ts = 0;
            uint16_t dur = 0;
            std::memcpy(&ts, p, 8);
            std::memcpy(&dur, p + 8, 2);
            meta.dropouts.emplace_back(static_cast<double>(ts) / 1e6, dur);
        }
        else if (ty == 'B' && size >= 16)
        {
            for (int i = 0; i < 8; ++i) { log.compatFlags[i] = p[i]; log.incompatFlags[i] = p[8 + i]; }
            if (size >= 40)
                for (int i = 0; i < 3; ++i) {
                    uint64_t off = 0;
                    std::memcpy(&off, p + 16 + i * 8, 8);
                    if (off) log.appendedOffsets.push_back(off);
                }
            char txt[64];
            std::snprintf(txt, sizeof(txt), "compat=%02x%02x%02x%02x%02x%02x%02x%02x",
                          p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7]);
            meta.facts.emplace_back("flags.compat", txt);
            std::snprintf(txt, sizeof(txt), "incompat=%02x%02x%02x%02x%02x%02x%02x%02x",
                          p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15]);
            meta.facts.emplace_back("flags.incompat", txt);
        }
        else if (ty == 'S')
        {
            log.hasSync = true;
            ++log.syncCount;
        }
        else if (ty == 'R')
        {
            if (size >= 2)
            {
                const uint16_t msgId = static_cast<uint16_t>(p[0] | (p[1] << 8));
                byId.erase(msgId);
            }
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
        auto axis = std::make_shared<const std::vector<double>>(std::move(s.t));
        size_t k = 0;
        for (const Leaf &l : s.leaves)
        {
            if (!l.hasPrim || !l.keepValue)
                continue;
            if (k >= s.vals.size())
                break;
            PsChannelData c;
            c.name = topic + "." + l.path;
            c.table = topic;
            c.value_type = psTypeFromPrim(l.prim);
            c.axis = axis;
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
    log.defaultParams = std::move(meta.defaultParams);
    log.facts = std::move(meta.facts);
    log.events = std::move(meta.events);
    log.dropouts = std::move(meta.dropouts);
    log.changedParams = std::move(meta.changedParams);
    psLoaderProgress(100, 100);
    return true;
}
