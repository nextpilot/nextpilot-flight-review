#include "MdfParser.h"

#include "ParserHub.h"

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

/*
 * MDF 4.x → PsFrameBatch。
 *
 * 解析骨架逐段对齐 main 的 Mdf4DataLoader.cpp（同一套块偏移与字段位置），
 * 差别只在出口：main 把每个通道拍平成一条曲线（key = "组.通道"），
 * 这里按 CG（通道组）出**一张表、多列**——正是 ng 数据集的口径。
 *
 * 时间轴：CG 里的 master 通道（channelType==2）经 CC 转换得到秒，
 * 落库时转 UTC 微秒；没有 master 时退回记录序号（与 main 一致）。
 */
namespace nextpilot::plotscope::ext::mdf
{
namespace
{

struct Block
{
    std::string id;
    uint64_t size = 0;
    uint64_t linkCount = 0;
    std::vector<uint64_t> links;
    std::vector<uint8_t> body;
};

uint64_t u64(const uint8_t *p)
{
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}

uint32_t u32(const uint8_t *p)
{
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

uint16_t u16(const uint8_t *p)
{
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

bool readBlock(const std::vector<uint8_t> &file, uint64_t off, Block &b)
{
    if (off + 24 > file.size())
        return false;
    b.id.assign(reinterpret_cast<const char *>(file.data() + off), 4);
    b.size = u64(file.data() + off + 8);
    b.linkCount = u64(file.data() + off + 16);
    if (b.size < 24 + b.linkCount * 8 || off + b.size > file.size())
        return false;
    b.links.clear();
    for (uint64_t i = 0; i < b.linkCount; ++i)
        b.links.push_back(u64(file.data() + off + 24 + i * 8));
    const size_t bodyStart = static_cast<size_t>(24 + b.linkCount * 8);
    b.body.assign(file.begin() + off + bodyStart, file.begin() + off + b.size);
    return true;
}

std::string readText(const std::vector<uint8_t> &file, uint64_t off)
{
    Block b;
    if (!off || !readBlock(file, off, b) || b.id != "##TX")
        return {};
    std::string s;
    for (uint8_t c : b.body)
    {
        if (!c)
            break;
        s.push_back(static_cast<char>(c));
    }
    return s;
}

struct Conversion
{
    uint8_t type = 0;
    std::vector<double> params;

    double apply(double x) const
    {
        switch (type)
        {
        case 0: // linear: a * x + b
            if (params.size() >= 2)
                return params[0] * x + params[1];
            break;
        case 1: // rational
            if (params.size() >= 6)
            {
                const double den = params[3] * x * x + params[4] * x + params[5];
                if (std::fabs(den) > 1e-300)
                    return (params[0] * x * x + params[1] * x + params[2]) / den;
                return 0;
            }
            break;
        case 2: // polynomial
            if (params.size() >= 3)
                return params[0] * x * x + params[1] * x + params[2];
            break;
        default:
            break;
        }
        return x;
    }
};

bool readConversion(const std::vector<uint8_t> &file, uint64_t off, Conversion &conversion)
{
    Block cc;
    if (!off || !readBlock(file, off, cc) || cc.id != "##CC" || cc.body.size() < 24)
        return false;
    conversion.type = cc.body[0];
    const uint16_t valueCount = static_cast<uint16_t>(cc.body[6]) |
                                (static_cast<uint16_t>(cc.body[7]) << 8);
    if (cc.body.size() < 24 + static_cast<size_t>(valueCount) * 8)
        return false;
    for (uint16_t i = 0; i < valueCount; ++i)
    {
        double v = 0;
        std::memcpy(&v, cc.body.data() + 24 + i * 8, sizeof(v));
        conversion.params.push_back(v);
    }
    return true;
}

struct Channel
{
    std::string name;
    std::string unit;
    uint32_t byteOffset = 0;
    uint32_t bitOffset = 0;
    uint32_t bitCount = 0;
    uint8_t type = 0; // 0 unsigned / 2 signed / 4 float
    uint8_t channelType = 0;
    Conversion conversion;
};

bool inflatePayload(const std::vector<uint8_t> &in, std::vector<uint8_t> &out, size_t expected)
{
    if (in.empty())
        return false;
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK)
        return false;
    zs.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(in.data()));
    zs.avail_in = static_cast<uInt>(in.size());
    out.clear();
    out.resize(std::max<size_t>(expected, 1u << 20));
    int rc = Z_OK;
    while (rc == Z_OK)
    {
        if (zs.total_out >= out.size())
            out.resize(out.size() * 2);
        zs.next_out = reinterpret_cast<Bytef *>(out.data() + zs.total_out);
        zs.avail_out = static_cast<uInt>(out.size() - zs.total_out);
        rc = inflate(&zs, Z_NO_FLUSH);
    }
    const size_t produced = zs.total_out;
    inflateEnd(&zs);
    if (rc != Z_STREAM_END)
        return false;
    out.resize(produced);
    return true;
}

// ##DZ：body[4]=zipType，body[8..12]=param（列数），body[12..20]=原始大小，
// body[20..28]=压缩大小，压缩流从 body+28 起。zipType==1 表示按列转置过，需还原。
bool unzipBlock(const Block &dz, std::vector<uint8_t> &out)
{
    if (dz.body.size() < 28)
        return false;
    const uint8_t zipType = dz.body[4];
    const uint32_t param = u32(dz.body.data() + 8);
    const uint64_t originalSize = u64(dz.body.data() + 12);
    const uint64_t zipSize = u64(dz.body.data() + 20);
    if (originalSize > (64ull << 20) || zipSize > dz.body.size() - 28)
        return false;
    const std::vector<uint8_t> compressed(dz.body.begin() + 28, dz.body.begin() + 28 + zipSize);
    if (!inflatePayload(compressed, out, static_cast<size_t>(originalSize)))
        return false;
    if (zipType == 1 && param > 0)
    {
        const size_t cols = param;
        const size_t lines = out.size() / cols;
        if (cols == 0 || lines * cols != out.size())
            return false;
        std::vector<uint8_t> transposed(out.size());
        for (size_t r = 0; r < lines; ++r)
            for (size_t c = 0; c < cols; ++c)
                transposed[r * cols + c] = out[c * lines + r];
        out.swap(transposed);
    }
    return true;
}

double readValue(const uint8_t *rec, const Channel &ch)
{
    const uint8_t *p = rec + ch.byteOffset;
    const uint32_t n = ch.bitCount;
    if (n == 0 || n > 64)
        return 0;
    if (ch.type == 4)
    {
        if (ch.bitOffset != 0)
            return 0;
        if (n == 32)
        {
            float f;
            std::memcpy(&f, p, 4);
            return f;
        }
        if (n == 64)
        {
            double d;
            std::memcpy(&d, p, 8);
            return d;
        }
        return 0;
    }

    uint64_t raw = 0;
    const uint32_t bytes = (ch.bitOffset + n + 7) / 8;
    if (bytes > 8)
        return 0;
    std::memcpy(&raw, p, bytes);
    raw >>= ch.bitOffset;
    if (n < 64)
        raw &= (uint64_t(1) << n) - 1;

    if (ch.type == 2)
    {
        if (n < 64 && (raw & (uint64_t(1) << (n - 1))))
            raw |= ~((uint64_t(1) << n) - 1);
        return static_cast<double>(static_cast<int64_t>(raw));
    }
    return static_cast<double>(raw);
}

struct GroupData
{
    uint16_t recordId = 0;
    uint64_t cycles = 0;
    std::string name;   // ##CG 组名（可空）
    std::string table;  // 最终表名（组名空时按 dg/cg 序号兜底，全局唯一）
    std::vector<Channel> channels; // 不含 master
    Channel master;
    bool hasMaster = false;
    uint32_t maxByte = 0;

    uint64_t written = 0;
    std::vector<int64_t> timeUs;
    std::vector<std::vector<double>> values; // values[列][行]

    // 预分配：MDF 的 cycles 就是行数上界，先按它起量，避免逐行 realloc。
    void reserve()
    {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(cycles, 50'000'000));
        timeUs.reserve(n);
        values.assign(channels.size(), {});
        for (auto &v : values)
            v.reserve(n);
    }
};

// 待发的批：每批最多 kChunk 行（大文件也不一次性拷完）。
constexpr size_t kChunk = 16384;

bool emitGroup(GroupData &g, const PsParserSink *sink, std::string &err)
{
    if (g.timeUs.empty() || g.channels.empty())
        return true;

    // 列名去重（MDF 允许同名通道）。
    std::map<std::string, int> seen;
    std::vector<std::string> names;
    for (const Channel &ch : g.channels)
    {
        const int count = ++seen[ch.name];
        names.push_back(count == 1 ? ch.name : ch.name + "#" + std::to_string(count));
    }
    const std::string &table = g.table;
    std::vector<PsFrameColumn> cols(g.channels.size());
    for (size_t start = 0; start < g.timeUs.size(); start += kChunk)
    {
        const size_t rows = std::min(kChunk, g.timeUs.size() - start);
        for (size_t c = 0; c < g.channels.size(); ++c)
        {
            PsFrameColumn &col = cols[c];
            col.name = names[c].c_str();
            col.unit = g.channels[c].unit.empty() ? nullptr : g.channels[c].unit.c_str();
            col.type = PS_VALUE_F64;
            col.values = g.values[c].data() + start;
            col.nulls = nullptr;
        }
        PsFrameBatch batch{};
        batch.table = table.c_str();
        batch.time_us = g.timeUs.data() + start;
        batch.rows = static_cast<uint32_t>(rows);
        batch.columns = static_cast<uint32_t>(cols.size());
        batch.column_data = cols.data();
        if (sink && sink->frame_batch && sink->frame_batch(sink->ctx, &batch) != 0)
        {
            err = "宿主拒绝了一批数据";
            return false;
        }
    }
    return true;
}

// 把一个数据块（##DT 原文或 ##DZ 解压结果）按 record 分给组。
// 返回 false 表示块结构坏了（跳过该块，不算致命）。
void consumeBlock(const std::vector<uint8_t> &body, uint8_t recordIdSize,
                  std::vector<std::unique_ptr<GroupData>> &groups)
{
    size_t pos = 0;
    while (pos < body.size())
    {
        const uint8_t *rec = body.data() + pos;
        uint64_t id = 0;
        if (recordIdSize)
        {
            if (pos + recordIdSize > body.size())
                break;
            switch (recordIdSize)
            {
            case 1: id = *rec; break;
            case 2: id = u16(rec); break;
            case 4: id = u32(rec); break;
            case 8: id = u64(rec); break;
            default: return;
            }
        }

        GroupData *g = nullptr;
        if (recordIdSize)
        {
            for (auto &candidate : groups)
            {
                if (candidate->recordId == id)
                {
                    g = candidate.get();
                    break;
                }
            }
        }
        else if (groups.size() == 1)
            g = groups.front().get();
        if (!g)
            break; // 未知 record id：数据块对齐已断，停止（与 main 一致）

        const size_t recordSize = static_cast<size_t>(recordIdSize) + g->maxByte;
        if (!recordSize || pos + recordSize > body.size())
            break;
        if (g->written >= g->cycles)
        {
            pos += recordSize;
            continue;
        }

        const double t = g->hasMaster
                             ? g->master.conversion.apply(readValue(rec + recordIdSize, g->master))
                             : static_cast<double>(g->written);
        g->timeUs.push_back(static_cast<int64_t>(std::llround(t * 1e6)));
        for (size_t k = 0; k < g->channels.size(); ++k)
        {
            const Channel &ch = g->channels[k];
            g->values[k].push_back(ch.conversion.apply(readValue(rec + recordIdSize, ch)));
        }
        ++g->written;
        pos += recordSize;
    }
}

bool parseFile(const std::vector<uint8_t> &file, const PsParserSink *sink, std::string &err)
{
    if (file.size() < 64 || std::memcmp(file.data(), "MDF     ", 8) != 0 ||
        std::memcmp(file.data() + 8, "4.", 2) != 0)
    {
        err = "不是 MDF 4.x 文件";
        return false;
    }

    Block hd;
    if (!readBlock(file, 64, hd) || hd.id != "##HD" || hd.links.size() < 1 || !hd.links[0])
    {
        err = "MDF4 HD 块缺失";
        return false;
    }

    // 表名去重：不同 DG/CG 可能同名（很多生成器不写组名），撞名会让两张表混进一个数据集。
    // 兜底名一律用下划线（不用点）：Dataset::channel("表.列") 按第一个点拆分，表名含点会解析错。
    std::map<std::string, int> tableCount;

    const uint64_t firstDg = hd.links[0];
    for (uint64_t dgOff = firstDg, dgIndex = 0; dgOff; ++dgIndex)
    {
        Block dg;
        if (!readBlock(file, dgOff, dg) || dg.id != "##DG" || dg.links.size() < 3)
            break;
        const uint64_t nextDg = dg.links[0];
        const uint64_t firstCg = dg.links[1];
        const uint64_t firstDt = dg.links[2];
        const uint64_t firstDl = dg.links.size() > 3 ? dg.links[3] : 0;
        const uint8_t recordIdSize = dg.body.empty() ? 0 : dg.body[0];

        // ① 通道组（CG）→ 通道（CN）
        std::vector<std::unique_ptr<GroupData>> groups;
        for (uint64_t cgOff = firstCg, cgIndex = 0; cgOff; ++cgIndex)
        {
            Block cg;
            if (!readBlock(file, cgOff, cg) || cg.id != "##CG" || cg.links.size() < 2)
                break;
            const uint64_t nextCg = cg.links[0];
            const uint64_t firstCn = cg.links[1];

            auto g = std::make_unique<GroupData>();
            g->recordId = cg.body.size() >= 2 ? u16(cg.body.data()) : 0;
            g->cycles = cg.body.size() >= 16 ? u64(cg.body.data() + 8) : 0;
            g->name = cg.links.size() > 3 ? readText(file, cg.links[3]) : std::string();
            const std::string base = !g->name.empty()
                                         ? g->name
                                         : "mdf4_dg" + std::to_string(dgIndex) + "_cg" +
                                               std::to_string(cgIndex);
            const int count = ++tableCount[base];
            g->table = count == 1 ? base : base + "#" + std::to_string(count);

            for (uint64_t cnOff = firstCn; cnOff; )
            {
                Block cn;
                if (!readBlock(file, cnOff, cn) || cn.id != "##CN" || cn.links.size() < 5)
                    break;
                const uint64_t nextCn = cn.links[0];
                Channel ch;
                ch.channelType = cn.body.size() > 0 ? cn.body[0] : 0;
                ch.type = cn.body.size() > 2 ? cn.body[2] : 0;
                const uint32_t bitOffset = cn.body.size() > 3 ? cn.body[3] : 0;
                ch.byteOffset = cn.body.size() >= 8 ? u32(cn.body.data() + 4) : 0;
                ch.byteOffset += bitOffset / 8;
                ch.bitOffset = bitOffset % 8;
                ch.bitCount = cn.body.size() >= 12 ? u32(cn.body.data() + 8) : 0;
                ch.name = readText(file, cn.links[2]);
                if (cn.links.size() > 6)
                    ch.unit = readText(file, cn.links[6]);
                if (cn.links.size() > 4)
                    readConversion(file, cn.links[4], ch.conversion);
                if (ch.name.empty())
                    ch.name = "ch" + std::to_string(g->channels.size() + 1);
                if (ch.bitCount)
                    g->maxByte = std::max(g->maxByte,
                                          ch.byteOffset + (ch.bitOffset + ch.bitCount + 7) / 8);
                if (ch.channelType == 2 && !g->hasMaster)
                {
                    g->master = ch;
                    g->hasMaster = true;
                }
                else
                    g->channels.push_back(ch);
                cnOff = nextCn;
            }

            if (!g->channels.empty() && g->cycles)
            {
                g->reserve();
                groups.push_back(std::move(g));
            }
            cgOff = nextCg;
        }

        // ② 数据：##DL 链优先，否则单个 ##DT
        auto consume = [&](uint64_t dataOff) {
            if (!dataOff || groups.empty())
                return;
            Block data;
            if (!readBlock(file, dataOff, data) || (data.id != "##DT" && data.id != "##DZ"))
                return;
            if (data.id == "##DT")
            {
                consumeBlock(data.body, recordIdSize, groups);
                return;
            }
            std::vector<uint8_t> inflated;
            if (unzipBlock(data, inflated))
                consumeBlock(inflated, recordIdSize, groups);
        };

        if (firstDl)
        {
            for (uint64_t dlOff = firstDl; dlOff; )
            {
                Block dl;
                if (!readBlock(file, dlOff, dl) || dl.id != "##DL" || dl.links.empty())
                    break;
                for (size_t i = 1; i < dl.links.size(); ++i)
                    consume(dl.links[i]);
                dlOff = dl.links[0];
            }
        }
        else
            consume(firstDt);

        // ③ 发批
        for (auto &g : groups)
        {
            if (!emitGroup(*g, sink, err))
                return false;
        }

        dgOff = nextDg;
    }

    return true;
}

// ---- SPI 包装 ----

struct SpiCtx
{
    std::vector<uint8_t> bytes;
    std::string error;
};

void *create()
{
    return new SpiCtx();
}

void destroy(void *ctx)
{
    delete static_cast<SpiCtx *>(ctx);
}

int32_t sniff(void *, const uint8_t *data, uint32_t size)
{
    if (!data || size < 10)
        return 0;
    return (std::memcmp(data, "MDF     ", 8) == 0 && std::memcmp(data + 8, "4.", 2) == 0) ? 1 : 0;
}

int32_t feed(void *ctx, const uint8_t *data, uint32_t size, const PsParserSink *, PsError *err)
{
    auto *c = static_cast<SpiCtx *>(ctx);
    if (!c)
    {
        if (err) { err->code = 1; std::snprintf(err->message, sizeof(err->message), "解析器实例为空"); }
        return -1;
    }
    if (data && size)
        c->bytes.insert(c->bytes.end(), data, data + size);
    return 0;
}

int32_t finish(void *ctx, const PsParserSink *sink, PsError *err)
{
    auto *c = static_cast<SpiCtx *>(ctx);
    if (!c)
        return -1;
    std::string msg;
    const bool ok = parseFile(c->bytes, sink, msg);
    if (!ok)
    {
        if (err)
        {
            err->code = 2;
            std::snprintf(err->message, sizeof(err->message), "%s", msg.c_str());
        }
        return -1;
    }
    return 0;
}

void reset(void *ctx)
{
    auto *c = static_cast<SpiCtx *>(ctx);
    if (c)
    {
        c->bytes.clear();
        c->error.clear();
    }
}

const PsParserInfo *info(void *)
{
    return &parserInfo();
}

uint64_t capabilities(void *)
{
    return 0;
}

static const PsParserVtable kVtable = {
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
    nullptr, // configure：MDF 没有解析参数
};

} // namespace

const PsParserInfo &parserInfo()
{
    static const PsParserInfo kInfo = {
        PS_PARSER_ABI_VERSION,
        "mdf4",
        "MDF 4.x（ASAM MDF）",
        ".mf4,.mdf4,.mdf,.dat",
        1, // can_sniff
        0, // streaming：整文件随机访问，宿主喂满后 finish
        {0, 0},
    };
    return kInfo;
}

const PsParserVtable *parserVtable()
{
    return &kVtable;
}

const PsParserInfo &MdfParserSpi::parserInfo()
{
    return mdf::parserInfo();
}

const PsParserVtable *MdfParserSpi::parserVtable()
{
    return mdf::parserVtable();
}

} // namespace nextpilot::plotscope::ext::mdf

// 静态自注册：与外部插件走同一张 ParserHub（宿主按后缀/探测即可选到 mdf4）。
PS_REGISTER_PARSER(::nextpilot::plotscope::ext::mdf::MdfParserSpi)
