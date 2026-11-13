#include "ApbinParser.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <optional>
#include <tuple>

namespace {

constexpr uint8_t kHead1 = 0xA3;
constexpr uint8_t kHead2 = 0x95;
constexpr uint8_t kFmtId = 128;
constexpr size_t kFmtLen = 89;

struct Field
{
    std::string name;
    uint8_t code = 0;
    size_t off = 0;
    size_t size = 0;
};

struct Format
{
    uint8_t id = 0;
    std::string name;
    size_t length = 0;
    std::vector<Field> fields;
    std::optional<size_t> instanceIdx;
};

struct FmtUnits
{
    std::array<char, 16> units{};
    std::array<char, 16> mults{};
};

std::string trimStr(const uint8_t *p, size_t n)
{
    size_t end = 0;
    while (end < n && p[end] != 0)
        ++end;
    std::string s(reinterpret_cast<const char *>(p), end);
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    return s;
}

size_t codeSize(uint8_t c)
{
    switch (c)
    {
    case 'h': case 'H': case 'c': case 'C': return 2;
    case 'i': case 'I': case 'f': case 'e': case 'E': case 'L': case 'M': return 4;
    case 'd': case 'q': case 'Q': return 8;
    case 'n': return 4;
    case 'N': return 16;
    case 'Z': return 64;
    case 'a': return 64;
    default: return 0;
    }
}

bool isStringCode(uint8_t c)
{
    return c == 'n' || c == 'N' || c == 'Z' || c == 'a';
}

bool parseFormat(const std::vector<uint8_t> &buf, size_t pos, Format &fmt)
{
    if (pos + kFmtLen > buf.size())
        return false;
    fmt.id = buf[pos + 3];
    fmt.length = buf[pos + 4];
    fmt.name = trimStr(&buf[pos + 5], 4);
    if (fmt.name.empty())
        return false;
    for (char ch : fmt.name)
    {
        if (ch < 0x21 || ch > 0x7e)
            return false;
    }
    const std::string codes = trimStr(&buf[pos + 9], 16);
    if (codes.empty() || fmt.length < 3)
        return false;
    const std::string labels = trimStr(&buf[pos + 25], 64);

    std::vector<std::string> labelList;
    size_t start = 0;
    while (start <= labels.size())
    {
        const size_t comma = labels.find(',', start);
        labelList.push_back(labels.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    size_t off = 3;
    for (size_t i = 0; i < codes.size(); ++i)
    {
        const uint8_t code = static_cast<uint8_t>(codes[i]);
        const size_t sz = codeSize(code);
        if (sz == 0)
            return false;
        std::string label = i < labelList.size() ? labelList[i] : std::string();
        const size_t lb = label.find_first_not_of(" \t");
        const size_t rb = label.find_last_not_of(" \t");
        label = lb == std::string::npos ? std::string() : label.substr(lb, rb - lb + 1);
        fmt.fields.push_back({label, code, off, sz});
        off += sz;
    }
    fmt.instanceIdx.reset();
    return true;
}

bool readNum(const std::vector<uint8_t> &pkt, const Field &f, double &out)
{
    const size_t end = f.off + f.size;
    if (end > pkt.size())
        return false;
    const uint8_t *s = pkt.data() + f.off;
    switch (f.code)
    {
    case 'b': out = static_cast<double>(static_cast<int8_t>(s[0])); return true;
    case 'B': case 'M': out = static_cast<double>(s[0]); return true;
    case 'h': case 'c':
    {
        const int16_t v = static_cast<int16_t>(s[0] | (s[1] << 8));
        out = static_cast<double>(v);
        return true;
    }
    case 'H': case 'C':
        out = static_cast<double>(s[0] | (s[1] << 8));
        return true;
    case 'i': case 'e': case 'L':
    {
        const int32_t v = static_cast<int32_t>(static_cast<uint32_t>(s[0]) | (uint32_t(s[1]) << 8) |
                                              (uint32_t(s[2]) << 16) | (uint32_t(s[3]) << 24));
        out = static_cast<double>(v);
        return true;
    }
    case 'I': case 'E':
        out = static_cast<double>(uint32_t(s[0]) | (uint32_t(s[1]) << 8) |
                                  (uint32_t(s[2]) << 16) | (uint32_t(s[3]) << 24));
        return true;
    case 'f':
    {
        float v = 0;
        std::memcpy(&v, s, sizeof(v));
        out = static_cast<double>(v);
        return true;
    }
    case 'd':
    {
        double v = 0;
        std::memcpy(&v, s, sizeof(v));
        out = v;
        return true;
    }
    case 'q':
    {
        int64_t v = 0;
        std::memcpy(&v, s, sizeof(v));
        out = static_cast<double>(v);
        return true;
    }
    case 'Q':
    {
        uint64_t v = 0;
        std::memcpy(&v, s, sizeof(v));
        out = static_cast<double>(v);
        return true;
    }
    default: return false;
    }
}

std::string readString(const std::vector<uint8_t> &pkt, const Field &f)
{
    const size_t end = std::min(f.off + f.size, pkt.size());
    if (f.off >= end)
        return {};
    return trimStr(pkt.data() + f.off, end - f.off);
}

int findField(const Format &fmt, const std::string &name)
{
    for (size_t i = 0; i < fmt.fields.size(); ++i)
        if (fmt.fields[i].name == name)
            return static_cast<int>(i);
    return -1;
}

std::map<uint8_t, Format> collectFormats(const std::vector<uint8_t> &buf)
{
    std::map<uint8_t, Format> formats;
    size_t pos = 0;
    while (pos + 3 <= buf.size())
    {
        if (buf[pos] != kHead1 || buf[pos + 1] != kHead2)
        {
            ++pos;
            continue;
        }
        const uint8_t id = buf[pos + 2];
        if (id == kFmtId)
        {
            Format f;
            if (pos + kFmtLen <= buf.size() && parseFormat(buf, pos, f))
                formats[f.id] = f;
            pos += kFmtLen;
            continue;
        }
        auto it = formats.find(id);
        if (it != formats.end() && it->second.length >= 3)
            pos += it->second.length;
        else
            ++pos;
    }
    return formats;
}

void collectUnits(const std::vector<uint8_t> &buf, std::map<uint8_t, Format> &formats,
                  std::map<uint8_t, FmtUnits> &fmtu,
                  std::map<uint8_t, std::string> &unitNames,
                  std::map<uint8_t, double> &mults)
{
    size_t pos = 0;
    while (pos + 3 <= buf.size())
    {
        if (buf[pos] != kHead1 || buf[pos + 1] != kHead2)
        {
            ++pos;
            continue;
        }
        const uint8_t id = buf[pos + 2];
        if (id == kFmtId)
        {
            pos += kFmtLen;
            continue;
        }
        auto it = formats.find(id);
        if (it == formats.end())
        {
            ++pos;
            continue;
        }
        const Format &fmt = it->second;
        if (pos + fmt.length > buf.size())
            break;
        const std::vector<uint8_t> pkt(buf.begin() + pos, buf.begin() + pos + fmt.length);
        if (fmt.name == "FMTU" && pkt.size() >= 44)
        {
            const uint8_t target = pkt[11];
            FmtUnits u;
            std::memcpy(u.units.data(), pkt.data() + 12, 16);
            std::memcpy(u.mults.data(), pkt.data() + 28, 16);
            fmtu[target] = u;
            for (size_t i = 0; i < u.units.size(); ++i)
            {
                if (u.units[i] == '#')
                {
                    auto targetIt = formats.find(target);
                    if (targetIt != formats.end())
                        targetIt->second.instanceIdx = i;
                    break;
                }
            }
        }
        else if (fmt.name == "UNIT")
        {
            const int iId = findField(fmt, "Id");
            const int iLabel = findField(fmt, "Label");
            double idv = 0;
            if (iId >= 0 && iLabel >= 0 && readNum(pkt, fmt.fields[iId], idv))
                unitNames[static_cast<uint8_t>(idv)] = readString(pkt, fmt.fields[iLabel]);
        }
        else if (fmt.name == "MULT")
        {
            const int iId = findField(fmt, "Id");
            const int iMult = findField(fmt, "Mult");
            double idv = 0, mult = 0;
            if (iId >= 0 && iMult >= 0 && readNum(pkt, fmt.fields[iId], idv) &&
                readNum(pkt, fmt.fields[iMult], mult))
                mults[static_cast<uint8_t>(idv)] = mult;
        }
        pos += fmt.length;
    }
}

struct Builder
{
    bool hasInstance = false;
    std::vector<double> t;
    std::vector<double> v;
};

} // namespace

bool psParseApbin(const char *path, ApbinLog &log, std::string &err)
{
    std::vector<uint8_t> buf;
    if (!psReadFile(path, buf, err))
        return false;
    if (buf.size() < 3 || buf[0] != kHead1 || buf[1] != kHead2)
    {
        err = "不是 ArduPilot DataFlash .bin（缺少 A3 95 头）";
        return false;
    }
    auto formats = collectFormats(buf);
    if (formats.empty())
    {
        err = "没有找到 FMT 格式定义";
        return false;
    }
    std::map<uint8_t, FmtUnits> fmtu;
    std::map<uint8_t, std::string> unitNames;
    std::map<uint8_t, double> mults;
    collectUnits(buf, formats, fmtu, unitNames, mults);

    std::map<std::tuple<uint8_t, uint8_t, size_t>, Builder> builders;
    std::vector<std::pair<std::string, std::string>> params;
    size_t pos = 0;
    while (pos + 3 <= buf.size())
    {
        if (buf[pos] != kHead1 || buf[pos + 1] != kHead2)
        {
            ++pos;
            continue;
        }
        const uint8_t id = buf[pos + 2];
        if (id == kFmtId)
        {
            pos += kFmtLen;
            continue;
        }
        auto it = formats.find(id);
        if (it == formats.end())
        {
            ++pos;
            continue;
        }
        const Format &fmt = it->second;
        if (pos + fmt.length > buf.size())
            break;
        const std::vector<uint8_t> pkt(buf.begin() + pos, buf.begin() + pos + fmt.length);

        if (fmt.name == "PARM")
        {
            const int iName = findField(fmt, "Name");
            const int iValue = findField(fmt, "Value");
            double value = 0;
            if (iName >= 0 && iValue >= 0 && readNum(pkt, fmt.fields[iValue], value))
            {
                const std::string name = readString(pkt, fmt.fields[iName]);
                if (!name.empty())
                    params.emplace_back(name, std::to_string(value));
            }
            pos += fmt.length;
            continue;
        }
        if (fmt.name == "MSG")
        {
            pos += fmt.length;
            continue;
        }
        const int ti = findField(fmt, "TimeUS");
        if (ti < 0)
        {
            pos += fmt.length;
            continue;
        }
        double tUs = 0;
        if (!readNum(pkt, fmt.fields[ti], tUs))
        {
            pos += fmt.length;
            continue;
        }
        const double t = tUs / 1e6;
        uint8_t instance = 0;
        if (fmt.instanceIdx)
        {
            double inst = 0;
            if (readNum(pkt, fmt.fields[*fmt.instanceIdx], inst))
                instance = static_cast<uint8_t>(inst);
        }

        for (size_t fi = 0; fi < fmt.fields.size(); ++fi)
        {
            const Field &f = fmt.fields[fi];
            if (static_cast<int>(fi) == ti || (fmt.instanceIdx && *fmt.instanceIdx == fi))
                continue;
            if (f.name.empty() || isStringCode(f.code))
                continue;
            double v = 0;
            if (!readNum(pkt, f, v))
                continue;
            Builder &b = builders[{fmt.id, instance, fi}];
            b.hasInstance = fmt.instanceIdx.has_value();
            b.t.push_back(t);
            b.v.push_back(v);
        }
        pos += fmt.length;
    }

    for (const auto &kv : builders)
    {
        const uint8_t msgId = std::get<0>(kv.first);
        const uint8_t instance = std::get<1>(kv.first);
        const size_t fi = std::get<2>(kv.first);
        const auto fmtIt = formats.find(msgId);
        if (fmtIt == formats.end() || fi >= fmtIt->second.fields.size())
            continue;
        const Format &fmt = fmtIt->second;
        const Field &f = fmt.fields[fi];
        PsChannelData c;
        c.name = fmt.name;
        c.name += kv.second.hasInstance ? ("." + std::string("#") + std::to_string(instance) + ".") : ".";
        c.name += f.name;
        c.t = kv.second.t;
        c.v = kv.second.v;

        if (fmtu.count(msgId) && fi < fmtu[msgId].units.size())
        {
            const char uc = fmtu[msgId].units[fi];
            if (uc != 0 && uc != '-' && uc != '#' && unitNames.count(static_cast<uint8_t>(uc)))
                c.unit = unitNames[static_cast<uint8_t>(uc)];
            if (fi < fmtu[msgId].mults.size())
            {
                const char mc = fmtu[msgId].mults[fi];
                auto mit = mults.find(static_cast<uint8_t>(mc));
                if (mit != mults.end())
                    for (double &v : c.v)
                        v *= mit->second;
            }
        }
        log.channels.push_back(std::move(c));
    }
    std::sort(log.channels.begin(), log.channels.end(),
              [](const PsChannelData &a, const PsChannelData &b) { return a.name < b.name; });
    log.params = std::move(params);
    return true;
}
