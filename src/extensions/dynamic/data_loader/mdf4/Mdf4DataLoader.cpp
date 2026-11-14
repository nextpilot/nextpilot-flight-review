#include "ps_extension_api.h"
#include "LegacyFileInput.hpp"
#include "PsLoaderCommon.h"

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

struct Channel
{
    std::string name;
    std::string unit;
    uint32_t byteOffset = 0;
    uint32_t bitCount = 0;
    uint8_t type = 0;       // 0 unsigned / 2 signed / 4 float
    uint8_t channelType = 0;
    int64_t conversion = 0;
};

double readValue(const uint8_t *rec, const Channel &ch)
{
    const uint8_t *p = rec + ch.byteOffset;
    const uint32_t n = ch.bitCount;
    if (ch.type == 4)
    {
        if (n == 32) { float f; std::memcpy(&f, p, 4); return f; }
        if (n == 64) { double d; std::memcpy(&d, p, 8); return d; }
    }
    if (ch.type == 2)
    {
        switch (n)
        {
        case 8: return static_cast<int8_t>(*p);
        case 16: { int16_t v; std::memcpy(&v, p, 2); return v; }
        case 32: { int32_t v; std::memcpy(&v, p, 4); return v; }
        case 64: { int64_t v; std::memcpy(&v, p, 8); return static_cast<double>(v); }
        default: break;
        }
    }
    switch (n)
    {
    case 8: return *p;
    case 16: { uint16_t v; std::memcpy(&v, p, 2); return v; }
    case 32: { uint32_t v; std::memcpy(&v, p, 4); return v; }
    case 64: { uint64_t v; std::memcpy(&v, p, 8); return static_cast<double>(v); }
    default: break;
    }
    return 0;
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
            if (!readBlock(file, dtOff, dt) || dt.id != "##DT")
            {
                cgOff = nextCg;
                continue;
            }
            const uint64_t cycles = cg.body.size() >= 16 ? u64(cg.body.data() + 8) : 0;

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
                ch.bitCount = cn.body.size() >= 12 ? u32(cn.body.data() + 8) : 0;
                ch.name = readText(file, cn.links[2]);
                ch.unit = readText(file, cn.links[5]);
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
            if (!recordSize || dt.body.size() < cycles * recordSize)
            {
                cgOff = nextCg;
                continue;
            }

            for (size_t i = 0; i < cycles; ++i)
            {
                const uint8_t *rec = dt.body.data() + i * recordSize + recordIdSize;
                const double t = hasMaster ? readValue(rec, master) : static_cast<double>(i);
                for (const Channel &ch : chans)
                {
                    acc[ch.name].push_back({t, readValue(rec, ch)});
                    if (!ch.unit.empty())
                        units[ch.name] = ch.unit;
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
