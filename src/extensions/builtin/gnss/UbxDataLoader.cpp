#include "ps_extension_api.h"
#include "LegacyFileInput.hpp"
#include "PsLoaderCommon.h"
#include "UbxMessageParser.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

struct Channel
{
    std::string name;
    std::string unit;
    std::vector<double> t;
    std::vector<double> v;
};

void add(std::map<std::string, Channel> &acc, const std::string &name, const std::string &unit,
         double t, double v)
{
    Channel &c = acc[name];
    c.name = name;
    c.unit = unit;
    c.t.push_back(t);
    c.v.push_back(v);
}

bool isHeaderField(const QString &name)
{
    return name == QStringLiteral("classId") || name == QStringLiteral("messageId") ||
           name == QStringLiteral("length") || name == QStringLiteral("payload") ||
           name == QStringLiteral("checksum");
}

bool parseFile(const std::string &path, std::vector<Channel> &channels, std::string &err)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        err = "无法打开 UBX 文件：" + path;
        return false;
    }

    UbxMessageParser parser;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::map<std::string, Channel> acc;
    size_t pos = 0;
    while (pos + 8 <= buf.size())
    {
        if (buf[pos] != 0xB5 || buf[pos + 1] != 0x62)
        {
            ++pos;
            continue;
        }
        const uint32_t len = static_cast<uint32_t>(buf[pos + 4]) |
                             (static_cast<uint32_t>(buf[pos + 5]) << 8);
        if (pos + 8 + len > buf.size())
            break;

        const QByteArray frame(reinterpret_cast<const char *>(buf.data() + pos),
                               static_cast<int>(8 + len));
        PsDecodedFrame decoded;
        if (parser.parse(frame, decoded))
        {
            const std::string prefix = "ubx." + decoded.frameName.toStdString();
            for (const PsDecodedField &field : decoded.fields)
            {
                if (isHeaderField(field.name))
                    continue;
                bool ok = false;
                const double value = field.value.toDouble(&ok);
                if (!ok)
                    continue;
                add(acc, prefix + "." + field.name.toStdString(), field.unit.toStdString(),
                    decoded.timestamp, value);
            }
        }
        pos += 8 + len;
    }

    channels.clear();
    for (auto &kv : acc)
        channels.push_back(std::move(kv.second));
    if (channels.empty())
    {
        err = "UBX 里没有描述库可解字段";
        return false;
    }
    return true;
}

int32_t ubxLoad(const char *path, const char *, const PsHostApi *host, void **dataset, char *err,
                uint32_t errCap)
{
    std::vector<Channel> channels;
    std::string perr;
    if (!parseFile(path, channels, perr))
    {
        psWriteErr(err, errCap, perr);
        return 1;
    }
    double t0 = 0;
    bool got = false;
    for (const auto &c : channels)
        if (!c.t.empty())
        {
            t0 = got ? std::min(t0, c.t.front()) : c.t.front();
            got = true;
        }
    std::vector<PsChannelData> data;
    data.reserve(channels.size());
    for (auto &c : channels)
    {
        PsChannelData pc;
        pc.name = c.name;
        pc.unit = c.unit;
        pc.t = std::move(c.t);
        pc.v = std::move(c.v);
        for (double &t : pc.t)
            t -= t0;
        data.push_back(std::move(pc));
    }
    PsDataset *ds = psBuildDataset(data, perr, host);
    if (!ds)
    {
        psWriteErr(err, errCap, perr);
        return 2;
    }
    *dataset = ds;
    return 0;
}

const PsLoaderVtable kUbxLoader = {
    PS_EXTENSION_ABI_VERSION,
    "data_loader_ubx",
    "UBX · GNSS",
    "u-blox UBX (*.ubx);;全部 (*)",
    ".ubx",
    "UBX · GNSS",
    0,
    0,
    {0, 0, 0, 0, 0, 0},
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    ubxLoad,
};

const PsDataSourceVtable *ubxInputVtable()
{
    static const PsDataSourceVtable vt = ps::VtableBuilder::make<LegacyFileInput<&kUbxLoader>>(R"({"id":"data_loader_ubx","name":"UBX","version":"1.0.0","sdk":"1.0","kind":"file"})");
    return &vt;
}

const PsExtensionDescriptor kUbxExtension = {
    PS_EXTENSION_ABI_VERSION,
    R"({"id":"data_loader_ubx","name":"UBX","version":"1.0.0","sdk":"1.0","family":"data_source","kind":"file","entry":"ps_extension_entry_data_loader_ubx"})",
    PS_FAMILY_DATA_SOURCE,
    PS_CAP_FILE | PS_CAP_CACHE,
    &kUbxLoader,
    nullptr,
    nullptr,
    ubxInputVtable(),
};

} // namespace

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_ubx(void)
{
    return &kUbxExtension;
}
