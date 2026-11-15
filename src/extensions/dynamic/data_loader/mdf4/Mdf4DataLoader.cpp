#include "ps_extension_api.h"
#include "LegacyFileInput.hpp"
#include "PsLoaderCommon.h"

#include <zlib.h>

#include <QFile>
#include <QString>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

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
        case 1: // rational: (P1*x^2 + P2*x + P3) / (P4*x^2 + P5*x + P6)
            if (params.size() >= 6)
            {
                const double den = params[3] * x * x + params[4] * x + params[5];
                if (std::fabs(den) > 1e-300)
                    return (params[0] * x * x + params[1] * x + params[2]) / den;
                return 0;
            }
            break;
        case 2: // polynomial: P1*x^2 + P2*x + P3
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
    uint8_t type = 0;       // 0 unsigned / 2 signed / 4 float
    uint8_t channelType = 0;
    Conversion conversion;
};

bool inflatePayload(const std::vector<uint8_t> &in, std::vector<uint8_t> &out)
{
    if (in.empty())
        return false;
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK)
        return false;
    zs.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(in.data()));
    zs.avail_in = static_cast<uInt>(in.size());
    out.clear();
    out.resize(1u << 20);
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
        if (n == 32) { float f; std::memcpy(&f, p, 4); return f; }
        if (n == 64) { double d; std::memcpy(&d, p, 8); return d; }
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

int32_t loadMdf4(const char *path, const char *, const PsHostApi *host, void **dataset,
                 char *err, uint32_t errCap)
{
    QFile f(QString::fromUtf8(path));
    if (!f.open(QIODevice::ReadOnly))
    {
        psWriteErr(err, errCap, "无法打开 MDF4 文件");
        return 1;
    }
    const QByteArray bytes = f.readAll();
    std::vector<uint8_t> file(bytes.constData(), bytes.constData() + bytes.size());
    if (file.size() < 64 || std::memcmp(file.data(), "MDF     ", 8) != 0 ||
        std::memcmp(file.data() + 8, "4.", 2) != 0)
    {
        psWriteErr(err, errCap, "不是 MDF 4.x 文件");
        return 2;
    }

    Block hd;
    if (!readBlock(file, 64, hd) || hd.id != "##HD" || hd.links.size() < 1)
    {
        psWriteErr(err, errCap, "MDF4 HD 块缺失");
        return 2;
    }
    const uint64_t firstDg = hd.links[0];
    if (!firstDg)
    {
        psWriteErr(err, errCap, "MDF4 没有数据组");
        return 2;
    }

    std::map<std::string, std::vector<std::pair<double, double>>> acc;
    std::map<std::string, std::string> units;
    for (uint64_t dgOff = firstDg; dgOff; )
    {
        Block dg;
        if (!readBlock(file, dgOff, dg) || dg.id != "##DG" || dg.links.size() < 3)
            break;
        const uint64_t nextDg = dg.links[0];
        const uint64_t firstCg = dg.links[1];
        const uint64_t dtOff = dg.links[2];
        uint8_t recordIdSize = dg.body.empty() ? 0 : dg.body[0];

        for (uint64_t cgOff = firstCg; cgOff; )
        {
            Block cg, dt;
            if (!readBlock(file, cgOff, cg) || cg.id != "##CG" || cg.links.size() < 2)
                break;
            const uint64_t nextCg = cg.links[0];
            const uint64_t firstCn = cg.links[1];
            if (!readBlock(file, dtOff, dt) || (dt.id != "##DT" && dt.id != "##DZ"))
            {
                cgOff = nextCg;
                continue;
            }
            std::vector<uint8_t> rawData;
            const std::vector<uint8_t> *dataBody = &dt.body;
            if (dt.id == "##DZ")
            {
                if (!inflatePayload(dt.body, rawData))
                {
                    cgOff = nextCg;
                    continue;
                }
                dataBody = &rawData;
            }
            const uint64_t cycles = cg.body.size() >= 16 ? u64(cg.body.data() + 8) : 0;
            const std::string group = cg.links.size() > 3 ? readText(file, cg.links[3]) : std::string();

            std::vector<Channel> chans;
            Channel master;
            bool hasMaster = false;
            int maxByte = 0;
            for (uint64_t cnOff = firstCn; cnOff; )
            {
                Block cn;
                if (!readBlock(file, cnOff, cn) || cn.id != "##CN" || cn.links.size() < 6)
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
                    ch.name = QStringLiteral("ch%1").arg(chans.size() + 1).toStdString();
                if (ch.bitCount)
                    maxByte = qMax(maxByte, static_cast<int>(ch.byteOffset + (ch.bitCount + 7) / 8));
                if (ch.channelType == 2 && !hasMaster)
                {
                    master = ch;
                    hasMaster = true;
                }
                else
                    chans.push_back(ch);
                cnOff = nextCn;
            }
            if (chans.empty() || !cycles)
            {
                cgOff = nextCg;
                continue;
            }

            const size_t recordSize = static_cast<size_t>(recordIdSize) + maxByte;
            if (!recordSize || dataBody->size() < cycles * recordSize)
            {
                cgOff = nextCg;
                continue;
            }

            for (size_t i = 0; i < cycles; ++i)
            {
                const uint8_t *rec = dataBody->data() + i * recordSize + recordIdSize;
                const double t = hasMaster ? master.conversion.apply(readValue(rec, master))
                                           : static_cast<double>(i);
                for (const Channel &ch : chans)
                {
                    const std::string name = group.empty() ? ch.name : group + "." + ch.name;
                    acc[name].push_back({t, ch.conversion.apply(readValue(rec, ch))});
                    if (!ch.unit.empty())
                        units[name] = ch.unit;
                }
            }
            cgOff = nextCg;
        }
        dgOff = nextDg;
    }
    if (acc.empty())
    {
        psWriteErr(err, errCap, "MDF4 没有可读取的数值通道");
        return 3;
    }

    std::vector<PsChannelData> data;
    for (auto &kv : acc)
    {
        PsChannelData ch;
        ch.name = "mdf4." + kv.first;
        ch.unit = units[kv.first];
        for (const auto &p : kv.second)
        {
            ch.t.push_back(p.first);
            ch.v.push_back(p.second);
        }
        data.push_back(std::move(ch));
    }
    std::string perr;
    PsDataset *ds = psBuildDataset(data, perr, host);
    if (!ds)
    {
        psWriteErr(err, errCap, perr);
        return 4;
    }
    *dataset = ds;
    return 0;
}

const PsLoaderVtable kMdf4Loader = {
    PS_EXTENSION_ABI_VERSION,
    "data_loader_mdf4",
    "MDF4",
    "MDF4 (*.mf4 *.mdf4);;全部 (*)",
    ".mf4,.mdf4",
    "MDF4",
    0,
    0,
    {0, 0, 0, 0, 0, 0},
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    loadMdf4,
};

const PsDataInputVtable *mdf4InputVtable()
{
    static const PsDataInputVtable vt = ps::VtableBuilder::make<LegacyFileInput<&kMdf4Loader>>(R"({"id":"data_loader_mdf4","name":"MDF4","version":"1.0.0","sdk":"1.0","kind":"file"})");
    return &vt;
}

const PsExtensionDescriptor kMdf4Extension = {
    PS_EXTENSION_ABI_VERSION,
    R"({"id":"data_loader_mdf4","name":"MDF4","version":"1.0.0","sdk":"1.0","family":"data_input","kind":"file","entry":"ps_extension_entry_data_loader_mdf4"})",
    PS_FAMILY_DATA_INPUT,
    PS_CAP_FILE | PS_CAP_CACHE,
    &kMdf4Loader,
    nullptr,
    nullptr,
    mdf4InputVtable(),
};

} // namespace

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_mdf4(void)
{
    return &kMdf4Extension;
}
