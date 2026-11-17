#include "ps_extension_api.h"
#include "LegacyFileInput.hpp"
#include "PsLoaderCommon.h"

#include "MavlinkMessageParser.h"

#include <QFile>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

uint64_t readLe64(const QByteArray &bytes)
{
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(static_cast<uint8_t>(bytes.at(i))) << (8 * i);
    return value;
}

struct Table
{
    std::vector<double> t;
    std::map<std::string, std::vector<double>> cols;
    std::map<std::string, std::string> units;
};

void addFrame(std::map<std::string, Table> &tables, const PsDecodedFrame &decoded, double time)
{
    if (!decoded.checksumOk || decoded.frameName.isEmpty())
        return;
    const std::string tableName = "mav." + decoded.frameName.toLower().toStdString();
    Table &table = tables[tableName];
    table.t.push_back(time);
    const size_t row = table.t.size();
    for (const PsDecodedField &field : decoded.fields)
    {
        if (field.offset < 0 || field.name.isEmpty())
            continue;
        bool ok = false;
        const double value = field.value.toDouble(&ok);
        if (!ok || !std::isfinite(value))
            continue;
        auto &col = table.cols[field.name.toStdString()];
        if (table.units.find(field.name.toStdString()) == table.units.end())
            table.units[field.name.toStdString()] = field.unit.toStdString();
        while (col.size() + 1 < row)
            col.push_back(std::numeric_limits<double>::quiet_NaN());
        col.push_back(value);
    }
}

bool loadTlog(const char *path, std::map<std::string, Table> &tables, std::string &err,
              int &frameCount, int &badCount)
{
    QFile file(QString::fromUtf8(path));
    if (!file.open(QIODevice::ReadOnly))
    {
        err = "无法打开 tlog：" + std::string(path);
        return false;
    }

    MavlinkMessageParser parser;
    uint64_t baseUs = 0;
    bool haveBase = false;
    frameCount = 0;
    badCount = 0;

    while (!file.atEnd())
    {
        const QByteArray prefix = file.read(8);
        if (prefix.isEmpty())
            break;
        if (prefix.size() != 8)
        {
            err = "tlog 记录时间戳被截断";
            return false;
        }
        const uint64_t timestampUs = readLe64(prefix);

        const QByteArray magicByte = file.read(1);
        if (magicByte.isEmpty())
            break;
        const uint8_t magic = static_cast<uint8_t>(magicByte.at(0));
        if (magic != 0xFE && magic != 0xFD)
        {
            err = "tlog 记录缺少 MAVLink magic（offset=" +
                  std::to_string(file.pos()) + "，magic=" + std::to_string(magic) + "）";
            return false;
        }

        const QByteArray lengthByte = file.read(1);
        if (lengthByte.size() != 1)
        {
            err = "tlog 帧长度被截断";
            return false;
        }
        const uint8_t payloadLength = static_cast<uint8_t>(lengthByte.at(0));

        // v1: seq/sysid/compid/msgid；v2: incompat/compat/seq/sysid/compid/msgid(3)
        const int headerBytes = magic == 0xFE ? 4 : 8;
        const QByteArray header = file.read(headerBytes);
        if (header.size() != headerBytes)
        {
            err = "tlog MAVLink 头被截断";
            return false;
        }
        const uint8_t incompat = magic == 0xFD ? static_cast<uint8_t>(header.at(0)) : 0;
        const int signatureBytes = (magic == 0xFD && (incompat & 0x01)) ? 13 : 0;

        const QByteArray payload = file.read(payloadLength);
        if (payload.size() != payloadLength)
        {
            err = "tlog MAVLink payload 被截断";
            return false;
        }
        const QByteArray crc = file.read(2);
        if (crc.size() != 2)
        {
            err = "tlog MAVLink CRC 被截断";
            return false;
        }
        QByteArray signature;
        if (signatureBytes)
        {
            signature = file.read(signatureBytes);
            if (signature.size() != signatureBytes)
            {
                err = "tlog MAVLink 签名被截断";
                return false;
            }
        }

        QByteArray frame = magicByte + lengthByte + header + payload + crc + signature;
        PsDecodedFrame decoded;
        if (!parser.parse(frame, decoded))
        {
            ++badCount;
            continue;
        }
        if (!haveBase)
        {
            baseUs = timestampUs;
            haveBase = true;
        }
        addFrame(tables, decoded, static_cast<double>(timestampUs - baseUs) / 1e6);
        ++frameCount;
    }

    return true;
}

int32_t tlogLoad(const char *path, const char *, const PsHostApi *host, void **dataset,
                 char *err, uint32_t errCap)
{
    std::map<std::string, Table> tables;
    int frameCount = 0;
    int badCount = 0;
    std::string perr;
    if (!loadTlog(path, tables, perr, frameCount, badCount))
    {
        psWriteErr(err, errCap, perr);
        return 1;
    }
    if (tables.empty())
    {
        psWriteErr(err, errCap, badCount > 0 ? "tlog 中没有可绘制帧"
                                             : "tlog 中没有 MAVLink 帧");
        return 2;
    }

    std::vector<PsChannelData> channels;
    for (auto &kv : tables)
    {
        Table &table = kv.second;
        auto axis = std::make_shared<const std::vector<double>>(table.t);
        for (auto &ck : table.cols)
        {
            auto &values = ck.second;
            values.resize(table.t.size(), std::numeric_limits<double>::quiet_NaN());
            PsChannelData channel;
            channel.name = kv.first + "." + ck.first;
            channel.table = kv.first;
            channel.axis = axis;
            channel.unit = table.units[ck.first];
            channel.v = std::move(values);
            channel.value_type = PS_VALUE_F64;
            channels.push_back(std::move(channel));
        }
    }

    PsDataset *ds = psBuildDataset(channels, perr, host);
    if (!ds)
    {
        psWriteErr(err, errCap, perr);
        return 3;
    }
    *dataset = ds;
    return 0;
}

const PsLoaderVtable kTlogLoader = {
    PS_EXTENSION_ABI_VERSION,
    "data_loader_tlog",
    "MAVLink tlog",
    "MAVLink tlog (*.tlog);;全部 (*)",
    ".tlog",
    "MAVLink · tlog",
    0,
    0,
    {0, 0, 0, 0, 0, 0},
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    tlogLoad,
};

const PsDataSourceVtable *tlogInputVtable()
{
    static const PsDataSourceVtable vt =
        ps::VtableBuilder::make<LegacyFileInput<&kTlogLoader>>(
            R"({"id":"data_loader_tlog","name":"MAVLink tlog","version":"1.0.0","sdk":"1.0","kind":"file"})");
    return &vt;
}

const PsExtensionDescriptor kTlogExtension = {
    PS_EXTENSION_ABI_VERSION,
    R"({"id":"data_loader_tlog","name":"MAVLink tlog","version":"1.0.0","sdk":"1.0","family":"data_source","kind":"file","entry":"ps_extension_entry_data_loader_tlog"})",
    PS_FAMILY_DATA_SOURCE,
    PS_CAP_FILE | PS_CAP_CACHE,
    &kTlogLoader,
    nullptr,
    nullptr,
    tlogInputVtable(),
};

} // namespace

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_tlog(void)
{
    return &kTlogExtension;
}
