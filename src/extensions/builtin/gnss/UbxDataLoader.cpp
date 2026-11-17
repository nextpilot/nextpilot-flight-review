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
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

struct Column
{
    std::string unit;
    std::vector<double> v;
};

// 一种 UBX 报文 = 一张表；同一报文的所有字段共享一条时间轴。
struct Table
{
    std::vector<double> t;
    std::map<std::string, Column> cols;
};

bool isHeaderField(const QString &name)
{
    return name == QStringLiteral("classId") || name == QStringLiteral("messageId") ||
           name == QStringLiteral("length") || name == QStringLiteral("payload") ||
           name == QStringLiteral("checksum");
}

bool parseFile(const std::string &path, std::vector<PsChannelData> &channels, std::string &err)
{
    UbxMessageParser parser;
    std::vector<uint8_t> buf;
    if (!psReadFile(path.c_str(), buf, err))
        return false;
    std::map<std::string, Table> tables;
    size_t pos = 0;
    while (pos + 8 <= buf.size())
    {
        psLoaderProgress(10 + pos * 90 / std::max<size_t>(1, buf.size()), 100);
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
        if (parser.parse(frame, decoded) && decoded.checksumOk)
        {
            const std::string tableName = "ubx." + decoded.frameName.toStdString();
            Table &table = tables[tableName];
            table.t.push_back(decoded.timestamp);
            for (const PsDecodedField &field : decoded.fields)
            {
                if (isHeaderField(field.name))
                    continue;
                bool ok = false;
                const double value = field.value.toDouble(&ok);
                if (!ok)
                    continue;
                Column &col = table.cols[field.name.toStdString()];
                col.unit = field.unit.toStdString();
                while (col.v.size() + 1 < table.t.size())
                    col.v.push_back(std::numeric_limits<double>::quiet_NaN());
                col.v.push_back(value);
            }
            pos += 8 + len;
        }
        else
        {
            ++pos;  // 坏 CRC：丢弃当前帧，从 payload 内重新搜帧头
        }
    }

    psLoaderProgress(100, 100);
    if (tables.empty())
    {
        err = "UBX 里没有描述库可解字段";
        return false;
    }

    double t0 = 0;
    bool got = false;
    for (const auto &kv : tables)
        if (!kv.second.t.empty())
        {
            t0 = got ? std::min(t0, kv.second.t.front()) : kv.second.t.front();
            got = true;
        }

    channels.clear();
    for (auto &kv : tables)
    {
        const std::string &tableName = kv.first;
        Table &table = kv.second;
        for (double &t : table.t)
            t -= t0;
        auto axis = std::make_shared<const std::vector<double>>(table.t);
        for (auto &ck : table.cols)
        {
            Column &col = ck.second;
            col.v.resize(table.t.size(), std::numeric_limits<double>::quiet_NaN());
            PsChannelData pc;
            pc.name = tableName + "." + ck.first;
            pc.table = tableName;
            pc.axis = axis;
            pc.unit = col.unit;
            pc.v = std::move(col.v);
            pc.value_type = PS_VALUE_F64;
            channels.push_back(std::move(pc));
        }
    }
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
    psLoaderSetHost(host);
    std::vector<PsChannelData> channels;
    std::string perr;
    if (!parseFile(path, channels, perr))
    {
        psWriteErr(err, errCap, perr);
        return 1;
    }
    PsDataset *ds = psBuildDataset(channels, perr, host);
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
