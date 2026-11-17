#include "ps_extension_api.h"
#include "LegacyFileInput.hpp"
#include "NmeaMessageParser.h"
#include "PsLoaderCommon.h"

#include <QByteArray>
#include <QString>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace {

struct Column
{
    std::string unit;
    std::vector<double> v;
};

// 一种 NMEA 语句 = 一张表；同一语句的所有字段共享一条时间轴。
struct Table
{
    std::vector<double> t;
    std::map<std::string, Column> cols;
};

std::string lowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool parseFile(const std::string &path, std::vector<PsChannelData> &channels, std::string &err)
{
    std::ifstream in(path);
    if (!in)
    {
        err = "无法打开 NMEA 文件：" + path;
        return false;
    }
    NmeaMessageParser parser;
    std::map<std::string, Table> tables;
    double lastT = 0, dayOffset = 0;
    bool first = true;
    std::error_code sizeEc;
    const auto fileSize = static_cast<uint64_t>(std::filesystem::file_size(path, sizeEc));
    std::string line;
    while (std::getline(in, line))
    {
        const std::streampos at = in.tellg();
        if (at >= 0)
            psLoaderProgress(10 + static_cast<uint64_t>(at) * 90 / std::max<uint64_t>(1, fileSize), 100);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.size() < 7 || line[0] != '$')
            continue;

        PsDecodedFrame decoded;
        QByteArray frame(line.data(), static_cast<int>(line.size()));
        if (!parser.parse(frame, decoded) || !decoded.checksumOk)
            continue;

        QString sentence;
        for (const PsDecodedField &field : decoded.fields)
            if (field.name == QStringLiteral("sentence"))
            {
                sentence = field.value.toString();
                break;
            }
        if (sentence.isEmpty())
            continue;
        const std::string tableName = "nmea." + lowerCopy(sentence.toStdString());

        double t = decoded.timestamp;
        if (first)
        {
            lastT = t;
            first = false;
        }
        else
        {
            if (t + dayOffset + 43200 < lastT)
                dayOffset += 86400;
            if (t + dayOffset < lastT)
                t += 86400;
            lastT = t + dayOffset;
        }
        const double stamp = t + dayOffset;

        Table &table = tables[tableName];
        table.t.push_back(stamp);
        for (const PsDecodedField &field : decoded.fields)
        {
            if (field.name == QStringLiteral("talker") || field.name == QStringLiteral("sentence") ||
                field.name == QStringLiteral("checksum") || field.name == QStringLiteral("time") ||
                field.name == QStringLiteral("timestamp") || field.name == QStringLiteral("utc") ||
                field.name == QStringLiteral("date"))
                continue;
            bool ok = false;
            const double value = field.value.toDouble(&ok);
            if (!ok || !std::isfinite(value))
                continue;
            Column &col = table.cols[field.name.toStdString()];
            col.unit = field.unit.toStdString();
            while (col.v.size() + 1 < table.t.size())
                col.v.push_back(std::numeric_limits<double>::quiet_NaN());
            col.v.push_back(value);
        }
    }
    psLoaderProgress(100, 100);
    if (tables.empty())
    {
        err = "NMEA 里没有可解析字段";
        return false;
    }

    double t0 = 0;
    bool gotT0 = false;
    for (const auto &kv : tables)
        if (!kv.second.t.empty())
        {
            t0 = gotT0 ? std::min(t0, kv.second.t.front()) : kv.second.t.front();
            gotT0 = true;
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
        err = "NMEA 里没有可解析字段";
        return false;
    }
    return true;
}

int32_t nmeaLoad(const char *path, const char *, const PsHostApi *host, void **dataset, char *err,
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

const PsLoaderVtable kNmeaLoader = {
    PS_EXTENSION_ABI_VERSION,
    "data_loader_nmea",
    "NMEA · GNSS",
    "NMEA (*.nmea *.txt);;全部 (*)",
    ".nmea,.txt",
    "NMEA · GNSS",
    0,
    0,
    {0, 0, 0, 0, 0, 0},
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nmeaLoad,
};

const PsDataSourceVtable *nmeaInputVtable()
{
    static const PsDataSourceVtable vt = ps::VtableBuilder::make<LegacyFileInput<&kNmeaLoader>>(R"({"id":"data_loader_nmea","name":"NMEA","version":"1.0.0","sdk":"1.0","kind":"file"})");
    return &vt;
}

const PsExtensionDescriptor kNmeaExtension = {
    PS_EXTENSION_ABI_VERSION,
    R"({"id":"data_loader_nmea","name":"NMEA","version":"1.0.0","sdk":"1.0","family":"data_source","kind":"file","entry":"ps_extension_entry_data_loader_nmea"})",
    PS_FAMILY_DATA_SOURCE,
    PS_CAP_FILE | PS_CAP_CACHE,
    &kNmeaLoader,
    nullptr,
    nullptr,
    nmeaInputVtable(),
};

} // namespace

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_nmea(void)
{
    return &kNmeaExtension;
}
