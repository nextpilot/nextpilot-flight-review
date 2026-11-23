#include "BlfParser.h"

#include "ParserHub.h"

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace nextpilot::plotscope::ext::blf
{
namespace
{

// ---- BLF 常量（与 main BlfParser.cpp 同值）----
constexpr uint32_t kCanMessage = 1;
constexpr uint32_t kLogContainer = 10;
constexpr uint32_t kCanMessage2 = 86;
constexpr uint32_t kCanFdMessage = 100;
constexpr uint32_t kCanMsgExt = 0x80000000u;
constexpr size_t kMinHeader = 144; // LOGG 文件头最小长度

struct Frame
{
    double t = 0;
    uint8_t bus = 1;
    uint32_t id = 0;
    bool ext = false;
    bool fd = false;
    bool rx = true;
    std::vector<uint8_t> data;
};

uint16_t u16(const std::vector<uint8_t> &b, size_t o)
{
    return static_cast<uint16_t>(b[o] | (b[o + 1] << 8));
}

uint32_t u32(const std::vector<uint8_t> &b, size_t o)
{
    return static_cast<uint32_t>(b[o]) | (uint32_t(b[o + 1]) << 8) |
           (uint32_t(b[o + 2]) << 16) | (uint32_t(b[o + 3]) << 24);
}

uint64_t u64(const std::vector<uint8_t> &b, size_t o)
{
    uint64_t v = 0;
    for (size_t i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(b[o + i]) << (8 * i);
    return v;
}

bool isLobj(const std::vector<uint8_t> &b, size_t o)
{
    return o + 4 <= b.size() && b[o] == 'L' && b[o + 1] == 'O' && b[o + 2] == 'B' && b[o + 3] == 'J';
}

// 对象之间可能有对齐填充/垃圾字节：在 [from, from+16) 里找下一个 LOBJ。
size_t nextObject(const std::vector<uint8_t> &b, size_t from)
{
    const size_t end = std::min(from + 16, b.size());
    for (size_t i = from; i + 4 <= end; ++i)
        if (isLobj(b, i))
            return i;
    return from;
}

// zlib 解压（容器 method=2）。返回是否完整解完。
bool inflateData(const uint8_t *data, size_t len, std::vector<uint8_t> &out)
{
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK)
        return false;
    zs.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(data));
    zs.avail_in = static_cast<uInt>(len);
    out.clear();
    size_t produced = 0;
    int ret = Z_OK;
    while (ret == Z_OK || ret == Z_BUF_ERROR)
    {
        const size_t chunk = 1 << 16;
        out.resize(out.size() + chunk);
        zs.next_out = out.data() + produced;
        zs.avail_out = static_cast<uInt>(chunk);
        ret = inflate(&zs, Z_NO_FLUSH);
        produced += chunk - zs.avail_out;
        if (ret == Z_STREAM_END)
            break;
        if (ret != Z_OK && ret != Z_BUF_ERROR)
            break;
        if (zs.avail_in == 0 && zs.avail_out != 0)
            break;
    }
    inflateEnd(&zs);
    out.resize(produced);
    return ret == Z_STREAM_END;
}

// 一个 CAN 报文对象 → Frame。对象头 pos..pos+hdrSize，报文体在 pos+hdrSize。
void parseOne(const std::vector<uint8_t> &buf, size_t pos, std::vector<Frame> &frames)
{
    const size_t hdrSize = u16(buf, pos + 4);
    const size_t objSize = u32(buf, pos + 8);
    const uint32_t objType = u32(buf, pos + 12);
    if (hdrSize < 32 || pos + hdrSize > buf.size() || pos + objSize > buf.size())
        return;
    const uint32_t flags = u32(buf, pos + 16);
    const uint64_t tsRaw = u64(buf, pos + 24);
    const double t = (flags & 0x2) ? static_cast<double>(tsRaw) * 1e-9
                                   : static_cast<double>(tsRaw) * 1e-5;
    const size_t body = pos + hdrSize;
    if (body + 8 > buf.size())
        return;
    const uint8_t bus = static_cast<uint8_t>(u16(buf, body));
    const uint8_t msgFlags = buf[body + 2];
    const uint8_t dlc = buf[body + 3];
    const uint32_t idRaw = u32(buf, body + 4);

    bool fd = false;
    size_t dataLen = 0, dataStart = 0;
    if (objType == kCanFdMessage)
    {
        fd = true;
        dataLen = (body + 24 < buf.size()) ? std::min<size_t>(buf[body + 24], 64) : 0;
        dataStart = body + 32;
    }
    else
    {
        dataLen = std::min<size_t>(dlc, 8);
        dataStart = body + 8;
    }
    Frame f;
    f.t = t;
    f.bus = bus;
    f.id = idRaw & 0x1FFFFFFFu;
    f.ext = (idRaw & kCanMsgExt) != 0;
    f.fd = fd;
    f.rx = (msgFlags & 0x1) != 0;
    for (size_t i = 0; i < dataLen && dataStart + i < buf.size(); ++i)
        f.data.push_back(buf[dataStart + i]);
    frames.push_back(std::move(f));
}

// 处理容器解出来的明文流（可能跨容器拼接，所以按 pending 里找 LOBJ）。
void drainObjects(std::vector<uint8_t> &pending, std::vector<Frame> &frames)
{
    size_t pos = 0;
    while (pos + 16 <= pending.size())
    {
        if (!isLobj(pending, pos))
        {
            ++pos;
            continue;
        }
        const size_t objSize = u32(pending, pos + 8);
        if (objSize < 16 || pos + objSize > pending.size())
            break;
        const uint32_t ty = u32(pending, pos + 12);
        if (ty == kCanMessage || ty == kCanMessage2 || ty == kCanFdMessage)
            parseOne(pending, pos, frames);
        pos = nextObject(pending, pos + objSize);
    }
    if (pos > 0)
        pending.erase(pending.begin(), pending.begin() + static_cast<long>(pos));
}

std::string idText(const Frame &f)
{
    char buf[16];
    if (f.ext)
        std::snprintf(buf, sizeof(buf), "%08X", static_cast<unsigned>(f.id));
    else
        std::snprintf(buf, sizeof(buf), "%03X", static_cast<unsigned>(f.id));
    return buf;
}

struct ColumnSet
{
    std::vector<int64_t> timeNs;
    std::map<size_t, std::vector<double>> bytes; // 字节位置 → 值列（缺字节置 NaN）
    std::vector<double> period;                 // ms，首行为 NaN
    std::vector<double> dlc;
};

/* 帧 → 表：每个 (bus, id) 一张表，只保留变化过的字节（与 main psCanFramesToChannels 同判据）。 */
void emitTables(const std::vector<Frame> &frames, const PsParserSink &sink, PsError &err)
{
    if (frames.empty())
    {
        err.code = 2;
        std::snprintf(err.message, sizeof(err.message), "BLF 里没找到 CAN 报文");
        return;
    }
    const double t0 = frames[0].t;
    std::map<std::pair<uint8_t, std::string>, std::vector<size_t>> groups;
    for (size_t i = 0; i < frames.size(); ++i)
        groups[{frames[i].bus, idText(frames[i])}].push_back(i);

    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (const auto &kv : groups)
    {
        const std::uint8_t bus = kv.first.first;
        const std::string &id = kv.first.second;
        const std::vector<size_t> &idx = kv.second;

        size_t maxLen = 0;
        for (size_t i : idx)
            maxLen = std::max(maxLen, frames[i].data.size());

        // 哪些字节在整段记录里变过
        std::vector<size_t> varies;
        for (size_t b = 0; b < maxLen; ++b)
        {
            int seen = -1;
            bool has = false;
            bool changed = false;
            for (size_t i : idx)
            {
                if (b >= frames[i].data.size())
                    continue;
                has = true;
                if (seen < 0)
                    seen = frames[i].data[b];
                else if (seen != static_cast<int>(frames[i].data[b]))
                {
                    changed = true;
                    break;
                }
            }
            if (has && changed)
                varies.push_back(b);
        }
        if (varies.empty())
            continue; // 全常量帧：main 也跳过

        ColumnSet cs;
        for (size_t k = 0; k < idx.size(); ++k)
        {
            const Frame &f = frames[idx[k]];
            cs.timeNs.push_back(std::llround((f.t - t0) * 1e9));
            cs.dlc.push_back(static_cast<double>(f.data.size()));
            cs.period.push_back(k == 0 ? nan
                                       : (frames[idx[k]].t - frames[idx[k - 1]].t) * 1000.0);
            for (size_t b : varies)
            {
                auto &col = cs.bytes[b];
                col.push_back(b < f.data.size() ? static_cast<double>(f.data[b]) : nan);
            }
        }

        const uint32_t rows = static_cast<uint32_t>(idx.size());
        const uint32_t cols = static_cast<uint32_t>(varies.size()) + 2;
        char tableName[64];
        std::snprintf(tableName, sizeof(tableName), "CAN%u_%s", static_cast<unsigned>(bus),
                      id.c_str());

        // 列名与值数组（指针只在回调期间有效）
        std::vector<std::string> names;
        std::vector<const void *> values;
        std::vector<const char *> units;
        for (size_t b : varies)
        {
            char nm[16];
            std::snprintf(nm, sizeof(nm), "b%u", static_cast<unsigned>(b));
            names.push_back(nm);
            values.push_back(cs.bytes[b].data());
            units.push_back(nullptr);
        }
        names.push_back("dlc");
        values.push_back(cs.dlc.data());
        units.push_back(nullptr);
        names.push_back("period");
        values.push_back(cs.period.data());
        units.push_back("ms");

        std::vector<PsFrameColumn> colDefs(cols);
        for (uint32_t c = 0; c < cols; ++c)
        {
            colDefs[c].name = names[c].c_str();
            colDefs[c].unit = units[c];
            colDefs[c].type = PS_VALUE_F64;
            colDefs[c].values = values[c];
            colDefs[c].nulls = nullptr;
        }
        PsFrameBatch batch{};
        batch.table = tableName;
        batch.time_ns = cs.timeNs.data();
        batch.rows = rows;
        batch.columns = cols;
        batch.column_data = colDefs.data();
        if (sink.frame_batch && sink.frame_batch(sink.ctx, &batch) != 0)
            break; // 宿主拒收：停止后续（与 SPI 约定一致）
    }
}

struct Ctx
{
    std::vector<uint8_t> buf;
};

void *create() { return new Ctx(); }

void destroy(void *p) { delete static_cast<Ctx *>(p); }

int32_t sniff(void *, const uint8_t *data, uint32_t size)
{
    if (!data || size < 4)
        return 0;
    return (data[0] == 'L' && data[1] == 'O' && data[2] == 'G' && data[3] == 'G') ? 1 : 0;
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

int32_t finish(void *p, const PsParserSink *sink, PsError *err)
{
    auto *c = static_cast<Ctx *>(p);
    if (!c)
        return -1;
    err->code = 0;
    err->message[0] = '\0';

    const std::vector<uint8_t> &buf = c->buf;
    if (buf.size() < kMinHeader || !(buf[0] == 'L' && buf[1] == 'O' && buf[2] == 'G' && buf[3] == 'G'))
    {
        err->code = 1;
        std::snprintf(err->message, sizeof(err->message), "不是 BLF：文件头没有 LOGG 签名");
        return -1;
    }
    const size_t headerSize = std::max<size_t>(u32(buf, 4), kMinHeader);
    std::vector<Frame> frames;
    std::vector<uint8_t> pending;
    size_t pos = headerSize;
    while (pos + 16 <= buf.size())
    {
        if (!isLobj(buf, pos))
        {
            ++pos;
            continue;
        }
        const size_t objSize = u32(buf, pos + 8);
        const uint32_t objType = u32(buf, pos + 12);
        if (objSize < 16 || pos + objSize > buf.size())
            break;
        const size_t objHdrSize = u16(buf, pos + 4);
        if (objHdrSize > objSize)
            break;
        const size_t body = pos + objHdrSize;

        if (objType == kLogContainer)
        {
            if (body + 16 <= pos + objSize)
            {
                const uint16_t method = u16(buf, body);
                const uint8_t *payload = buf.data() + body + 16;
                const size_t payloadLen = pos + objSize - (body + 16);
                if (method == 0)
                    pending.insert(pending.end(), payload, payload + payloadLen);
                else if (method == 2)
                {
                    std::vector<uint8_t> out;
                    if (inflateData(payload, payloadLen, out))
                        pending.insert(pending.end(), out.begin(), out.end());
                }
                drainObjects(pending, frames);
            }
        }
        else if (objType == kCanMessage || objType == kCanMessage2 || objType == kCanFdMessage)
        {
            parseOne(buf, pos, frames);
        }
        pos = nextObject(buf, pos + objSize);
    }

    PsError emitErr{};
    emitTables(frames, *sink, emitErr);
    if (emitErr.code != 0)
    {
        *err = emitErr;
        return -1;
    }
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
    nullptr, // configure：BLF 无解析参数（DBC 属于出口映射，不在解析器内）
};

} // namespace

const PsParserInfo &parserInfo()
{
    static const PsParserInfo kInfo = {
        PS_PARSER_ABI_VERSION,
        "blf",
        "Vector BLF（.blf）",
        ".blf",
        1, // can_sniff（LOGG 头）
        0, // streaming
        {0, 0},
    };
    return kInfo;
}

const PsParserVtable *parserVtable() { return &kVtable; }

// 注意限定命名空间：类内直接写 parserInfo() 会解析到成员自身 → 无限递归（静态初始化阶段崩）。
const PsParserInfo &BlfParserSpi::parserInfo() { return blf::parserInfo(); }
const PsParserVtable *BlfParserSpi::parserVtable() { return blf::parserVtable(); }

} // namespace nextpilot::plotscope::ext::blf

// 静态自注册：宿主按后缀 .blf / sniff 即可选到 blf。
PS_REGISTER_PARSER(::nextpilot::plotscope::ext::blf::BlfParserSpi)
