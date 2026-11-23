#include "ApbinParser.h"

#include "ParserHub.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nextpilot::plotscope::ext::apbin
{
namespace
{

// ---- DataFlash 常量（与 main ApbinParser.cpp 同值）----
constexpr uint8_t kHead1 = 0xA3;
constexpr uint8_t kHead2 = 0x95;
constexpr uint8_t kFmtId = 128;
constexpr size_t kFmtLen = 89; // FMT 帧固定长度（含 3 字节头）

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

// FMT 字段类型字符 → 字节宽度（与 main codeSize 一致）
size_t codeSize(uint8_t c)
{
    switch (c)
    {
    case 'h': case 'H': case 'c': case 'C': return 2;
    case 'i': case 'I': case 'f': case 'e': case 'E': case 'L': return 4;
    case 'M': return 1;
    case 'd': case 'q': case 'Q': return 8;
    case 'n': return 4;
    case 'N': return 16;
    case 'Z': return 64;
    case 'a': return 64;
    default: return 0;
    }
}

bool isStringCode(uint8_t c) { return c == 'n' || c == 'N' || c == 'Z' || c == 'a'; }

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

bool parseFormat(const std::vector<uint8_t> &buf, size_t pos, Format &fmt)
{
    if (pos + kFmtLen > buf.size())
        return false;
    fmt.id = buf[pos + 3];
    fmt.length = buf[pos + 4];
    fmt.name = trimStr(&buf[pos + 5], 4); // 名字最长 4 字节，常带尾空格
    if (fmt.name.empty())
        return false;
    for (char ch : fmt.name)
        if (ch < 0x21 || ch > 0x7e)
            return false;

    const std::string codes = trimStr(&buf[pos + 9], 16);
    if (codes.empty() || fmt.length < 3)
        return false;
    const std::string labels = trimStr(&buf[pos + 25], 64);

    std::vector<std::string> labelList;
    size_t start = 0;
    while (start <= labels.size())
    {
        const size_t comma = labels.find(',', start);
        labelList.push_back(
            labels.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }

    size_t off = 3; // 消息体从 3 字节头之后开始
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

bool readNum(const uint8_t *pkt, size_t pktLen, const Field &f, double &out)
{
    const size_t end = f.off + f.size;
    if (end > pktLen)
        return false;
    const uint8_t *s = pkt + f.off;
    switch (f.code)
    {
    case 'b': out = static_cast<double>(static_cast<int8_t>(s[0])); return true;
    case 'B': case 'M': out = static_cast<double>(s[0]); return true;
    case 'h': case 'c': out = static_cast<double>(static_cast<int16_t>(s[0] | (s[1] << 8))); return true;
    case 'H': case 'C': out = static_cast<double>(s[0] | (s[1] << 8)); return true;
    case 'i': case 'e': case 'L':
        out = static_cast<double>(static_cast<int32_t>(static_cast<uint32_t>(s[0]) |
                                                       (uint32_t(s[1]) << 8) |
                                                       (uint32_t(s[2]) << 16) |
                                                       (uint32_t(s[3]) << 24)));
        return true;
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

std::string readString(const uint8_t *pkt, size_t pktLen, const Field &f)
{
    const size_t end = std::min(f.off + f.size, pktLen);
    if (f.off >= end)
        return {};
    return trimStr(pkt + f.off, end - f.off);
}

int findField(const Format &fmt, const std::string &name)
{
    for (size_t i = 0; i < fmt.fields.size(); ++i)
        if (fmt.fields[i].name == name)
            return static_cast<int>(i);
    return -1;
}

// 第一遍：扫出全部 FMT 定义（用于后续按长度步进）
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

// 第二遍：FMTU（单位/倍率 + 实例列）、UNIT（单位名）、MULT（倍率）
void collectUnits(const std::vector<uint8_t> &buf,
                  std::map<uint8_t, Format> &formats,
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
        const uint8_t *pkt = buf.data() + pos;

        if (fmt.name == "FMTU" && fmt.length >= 44)
        {
            const uint8_t target = pkt[11];
            FmtUnits u;
            std::memcpy(u.units.data(), pkt + 12, 16);
            std::memcpy(u.mults.data(), pkt + 28, 16);
            fmtu[target] = u;
            // FMTU 的 units 里 '#' 标记实例列
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
            if (iId >= 0 && iLabel >= 0 && readNum(pkt, fmt.length, fmt.fields[iId], idv))
                unitNames[static_cast<uint8_t>(idv)] = readString(pkt, fmt.length, fmt.fields[iLabel]);
        }
        else if (fmt.name == "MULT")
        {
            const int iId = findField(fmt, "Id");
            const int iMult = findField(fmt, "Mult");
            double idv = 0, mult = 0;
            if (iId >= 0 && iMult >= 0 && readNum(pkt, fmt.length, fmt.fields[iId], idv) &&
                readNum(pkt, fmt.length, fmt.fields[iMult], mult))
                mults[static_cast<uint8_t>(idv)] = mult;
        }
        pos += fmt.length;
    }
}

struct Table
{
    std::string name;
    std::vector<int64_t> timeNs;
    std::vector<std::string> colNames;
    std::vector<std::vector<double>> cols;
    std::vector<std::string> colUnits;
    std::vector<const char *> unitPtrs; // 指向 colUnits，构造 batch 用
};

struct Ctx
{
    std::vector<uint8_t> buf;
};

void *create() { return new Ctx(); }
void destroy(void *p) { delete static_cast<Ctx *>(p); }

int32_t sniff(void *, const uint8_t *data, uint32_t size)
{
    if (!data || size < 3)
        return 0;
    return (data[0] == kHead1 && data[1] == kHead2) ? 1 : 0;
}

int32_t feed(void *p, const uint8_t *data, uint32_t size, const PsParserSink *, PsError *)
{
    auto *c = static_cast<Ctx *>(p);
    if (!c || (!data && size))
        return -1;
    if (data && size)
        c->buf.insert(c->buf.end(), data, data + size);
    return 0; // 随机访问格式：只在 finish 出批
}

void emitTables(const std::vector<Table> &tables, const PsParserSink &sink)
{
    for (const Table &t : tables)
    {
        if (t.timeNs.empty() || t.cols.empty())
            continue;
        std::vector<PsFrameColumn> defs(t.cols.size());
        for (size_t c = 0; c < t.cols.size(); ++c)
        {
            defs[c].name = t.colNames[c].c_str();
            defs[c].unit = t.unitPtrs[c];
            defs[c].type = PS_VALUE_F64;
            defs[c].values = t.cols[c].data();
            defs[c].nulls = nullptr;
        }
        PsFrameBatch batch{};
        batch.table = t.name.c_str();
        batch.time_ns = t.timeNs.data();
        batch.rows = static_cast<uint32_t>(t.timeNs.size());
        batch.columns = static_cast<uint32_t>(t.cols.size());
        batch.column_data = defs.data();
        if (sink.frame_batch && sink.frame_batch(sink.ctx, &batch) != 0)
            return; // 宿主拒收：停止后续
    }
}

int32_t finish(void *p, const PsParserSink *sink, PsError *err)
{
    auto *c = static_cast<Ctx *>(p);
    if (!c)
        return -1;
    err->code = 0;
    err->message[0] = '\0';

    const std::vector<uint8_t> &buf = c->buf;
    if (buf.size() < 3 || buf[0] != kHead1 || buf[1] != kHead2)
    {
        err->code = 1;
        std::snprintf(err->message, sizeof(err->message),
                      "不是 ArduPilot DataFlash .bin（缺少 A3 95 头）");
        return -1;
    }

    auto formats = collectFormats(buf);
    if (formats.empty())
    {
        err->code = 2;
        std::snprintf(err->message, sizeof(err->message), "没有找到 FMT 格式定义");
        return -1;
    }
    std::map<uint8_t, FmtUnits> fmtu;
    std::map<uint8_t, std::string> unitNames;
    std::map<uint8_t, double> mults;
    collectUnits(buf, formats, fmtu, unitNames, mults);

    // 按 (格式, 实例号) 收表；每个字段一列
    std::map<std::pair<uint8_t, uint8_t>, Table> tables;
    std::map<std::pair<uint8_t, uint8_t>, std::map<size_t, size_t>> colIndex; // 字段序号 → 列序号
    bool haveT0 = false;
    double t0 = 0;

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
        const uint8_t *pkt = buf.data() + pos;

        const int ti = findField(fmt, "TimeUS");
        if (ti < 0)
        {
            pos += fmt.length; // 无时间轴的消息（FMTU/UNIT/MULT/MSG）不成表
            continue;
        }
        double tUs = 0;
        if (!readNum(pkt, fmt.length, fmt.fields[ti], tUs))
        {
            pos += fmt.length;
            continue;
        }
        if (!haveT0)
        {
            t0 = tUs;
            haveT0 = true;
        }
        uint8_t instance = 0;
        if (fmt.instanceIdx)
        {
            double inst = 0;
            if (readNum(pkt, fmt.length, fmt.fields[*fmt.instanceIdx], inst))
                instance = static_cast<uint8_t>(inst);
        }
        const auto key = std::make_pair(fmt.id, instance);
        Table &tbl = tables[key];
        if (tbl.name.empty())
        {
            tbl.name = fmt.name;
            if (instance > 0)
                tbl.name += "_" + std::to_string(static_cast<unsigned>(instance));
        }
        // DataFlash TimeUS 原生微秒；相对首帧后 ×1000 转内部纳秒轴。
        tbl.timeNs.push_back(std::llround((tUs - t0) * 1000.0));

        for (size_t fi = 0; fi < fmt.fields.size(); ++fi)
        {
            const Field &f = fmt.fields[fi];
            if (static_cast<int>(fi) == ti)
                continue;
            if (fmt.instanceIdx && *fmt.instanceIdx == fi)
                continue;
            if (f.name.empty() || isStringCode(f.code))
                continue;
            double v = 0;
            if (!readNum(pkt, fmt.length, f, v))
                continue;
            auto &idxMap = colIndex[key];
            auto ci = idxMap.find(fi);
            if (ci == idxMap.end())
            {
                // 新列：补齐已有行（同一格式早期帧可能缺该字段）
                const size_t row = idxMap.size();
                tbl.colNames.push_back(f.name);
                tbl.cols.emplace_back();
                tbl.cols.back().assign(tbl.timeNs.size() - 1,
                                       std::numeric_limits<double>::quiet_NaN());
                std::string unit;
                if (fmtu.count(fmt.id) && fi < fmtu[fmt.id].units.size())
                {
                    const char uc = fmtu[fmt.id].units[fi];
                    if (uc != 0 && uc != '-' && uc != '#' &&
                        unitNames.count(static_cast<uint8_t>(uc)))
                        unit = unitNames[static_cast<uint8_t>(uc)];
                }
                tbl.colUnits.push_back(unit);
                ci = idxMap.emplace(fi, row).first;
            }
            // 倍率
            if (fmtu.count(fmt.id) && fi < fmtu[fmt.id].mults.size())
            {
                const char mc = fmtu[fmt.id].mults[fi];
                auto mit = mults.find(static_cast<uint8_t>(mc));
                if (mit != mults.end())
                    v *= mit->second;
            }
            auto &col = tbl.cols[ci->second];
            col.resize(tbl.timeNs.size() - 1, std::numeric_limits<double>::quiet_NaN());
            col.push_back(v);
        }
        pos += fmt.length;
    }

    std::vector<Table> out;
    for (auto &kv : tables)
    {
        Table &t = kv.second;
        if (t.timeNs.empty() || t.cols.empty())
            continue; // 只有时间没有数值字段的格式（如 MSG）不成表
        t.unitPtrs.clear();
        for (const std::string &u : t.colUnits)
            t.unitPtrs.push_back(u.empty() ? nullptr : u.c_str());
        out.push_back(std::move(t));
    }
    std::sort(out.begin(), out.end(), [](const Table &a, const Table &b) { return a.name < b.name; });

    if (out.empty())
    {
        err->code = 3;
        std::snprintf(err->message, sizeof(err->message), "DataFlash 里没有可成表的数值消息");
        return -1;
    }
    emitTables(out, *sink);
    return 0;
}

void reset(void *p)
{
    auto *c = static_cast<Ctx *>(p);
    if (c)
        c->buf.clear();
}

const PsParserInfo *info(void *) { return &parserInfo(); }
uint64_t capabilities(void *) { return 0; }

const PsParserVtable kVtable = {
    PS_PARSER_ABI_VERSION,
    sizeof(PsParserVtable),
    create,
    destroy,
    sniff,
    feed,
    finish,
    reset,
    info,
    capabilities,
    nullptr, // configure：DataFlash 无解析参数
};

} // namespace

const PsParserInfo &parserInfo()
{
    static const PsParserInfo kInfo = {
        PS_PARSER_ABI_VERSION,
        "apbin",
        "ArduPilot DataFlash（.bin）",
        ".bin,.log",
        1, // can_sniff（A3 95 头）
        0, // streaming
        {0, 0},
    };
    return kInfo;
}

const PsParserVtable *parserVtable() { return &kVtable; }

// 注意限定命名空间：类内直接写 parserInfo() 会解析到成员自身 → 无限递归（静态初始化阶段崩）。
const PsParserInfo &ApbinParserSpi::parserInfo() { return apbin::parserInfo(); }
const PsParserVtable *ApbinParserSpi::parserVtable() { return apbin::parserVtable(); }

} // namespace nextpilot::plotscope::ext::apbin

PS_REGISTER_PARSER(::nextpilot::plotscope::ext::apbin::ApbinParserSpi)
