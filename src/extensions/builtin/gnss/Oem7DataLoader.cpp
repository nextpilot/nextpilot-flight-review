#include "ps_extension_api.h"
#include "LegacyFileInput.hpp"
#include "Oem7MessageParser.h"
#include "PsLoaderCommon.h"

#include <QByteArray>
#include <QString>
#include <QMetaType>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdint>
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

// 一种 OEM7 日志名 = 一张表；同一日志的字段共享一条时间轴。
struct Table
{
    std::vector<double> t;
    std::map<std::string, Column> cols;
};

bool isHeaderField(const QString &name)
{
    static const QStringList kSkip = {
        QStringLiteral("logName"),      QStringLiteral("port"),
        QStringLiteral("sequence"),     QStringLiteral("idleTime"),
        QStringLiteral("timeStatus"),   QStringLiteral("week"),
        QStringLiteral("milliseconds"), QStringLiteral("receiverStatus"),
        QStringLiteral("reserved"),     QStringLiteral("messageDefinitionCrc"),
        QStringLiteral("messageId"),    QStringLiteral("messageType"),
        QStringLiteral("length"),       QStringLiteral("body"),
        QStringLiteral("checksum")};
    return kSkip.contains(name);
}

QString unitOf(const QString &fname)
{
    const QString lower = fname.toLower();
    if (lower.contains(QStringLiteral("latitude")) || lower.contains(QStringLiteral("longitude")) ||
        lower.contains(QStringLiteral("azimuth")) || lower.contains(QStringLiteral("roll")) ||
        lower.contains(QStringLiteral("pitch")) || lower.contains(QStringLiteral("heading")))
        return QStringLiteral("deg");
    if (lower.contains(QStringLiteral("height")) || lower.contains(QStringLiteral("altitude")) ||
        lower.contains(QStringLiteral("undulation")))
        return QStringLiteral("m");
    if (lower.contains(QStringLiteral("velocity")) || lower.contains(QStringLiteral("speed")))
        return QStringLiteral("m/s");
    return {};
}

void addFrame(std::map<std::string, Table> &tables, const PsDecodedFrame &frame)
{
    if (!frame.checksumOk || frame.timestamp <= 0)
        return;
    QString name = frame.frameName;
    if (name.isEmpty() || name.startsWith(QLatin1Char('#')))
        return;
    name.remove(QLatin1Char('#'));
    const std::string tableName = "oem7." + name.toLower().toStdString();

    Table &table = tables[tableName];
    const double stamp = frame.timestamp;
    table.t.push_back(stamp);
    const size_t row = table.t.size();

    for (const PsDecodedField &field : frame.fields)
    {
        if (field.name.isEmpty() || isHeaderField(field.name) ||
            field.name.contains(QStringLiteral("reserved")))
            continue;
        bool ok = false;
        double value = 0.0;
        if (field.value.typeId() == QMetaType::Bool)
        {
            value = field.value.toBool() ? 1.0 : 0.0;
            ok = true;
        }
        else
        {
            value = field.value.toDouble(&ok);
        }
        if (!ok || !std::isfinite(value))
            continue;

        Column &col = table.cols[field.name.toStdString()];
        if (col.unit.empty())
            col.unit = unitOf(field.name).toStdString();
        // 同一张表某帧缺字段时补 NaN，保证所有列与时间轴等长。
        while (col.v.size() + 1 < row)
            col.v.push_back(std::numeric_limits<double>::quiet_NaN());
        col.v.push_back(value);
    }
}

bool parseBinary(const std::vector<uint8_t> &bytes, const Oem7MessageParser &parser,
                 std::map<std::string, Table> &tables)
{
    size_t i = 0;
    while (i + 3 < bytes.size())
    {
        if (!(bytes[i] == 0xAA && bytes[i + 1] == 0x44))
        {
            ++i;
            continue;
        }
        const uint8_t type = bytes[i + 2];
        if (type != 0x12 && type != 0x13)
        {
            ++i;
            continue;
        }
        const int headerLength = type == 0x12 ? 28 : 12;
        if (i + headerLength + 4 > bytes.size())
            break;
        const uint16_t bodyLength = type == 0x12
            ? static_cast<uint16_t>(bytes[i + 8] | (bytes[i + 9] << 8))
            : bytes[i + 3];
        const size_t total = static_cast<size_t>(headerLength) + bodyLength + 4;
        if (i + total > bytes.size())
            break;

        QByteArray frame(reinterpret_cast<const char *>(bytes.data() + i),
                         static_cast<int>(total));
        PsDecodedFrame decoded;
        if (parser.parse(frame, decoded) && decoded.checksumOk)
        {
            addFrame(tables, decoded);
            i += total;
        }
        else
        {
            // 坏 CRC / 未知结构：丢当前帧，从下一字节重新搜帧头。
            ++i;
        }
    }
    return true;
}

bool parseAscii(const std::vector<uint8_t> &bytes, const Oem7MessageParser &parser,
                std::map<std::string, Table> &tables)
{
    std::string text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    std::size_t begin = 0;
    while (begin < text.size())
    {
        std::size_t end = text.find('\n', begin);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        begin = end + 1;
        if (line.empty() || (line[0] != '#' && line[0] != '%'))
            continue;
        QByteArray frame(line.data(), static_cast<int>(line.size()));
        PsDecodedFrame decoded;
        if (parser.parse(frame, decoded) && decoded.checksumOk)
            addFrame(tables, decoded);
    }
    return true;
}

bool parseFile(const std::string &path, std::vector<PsChannelData> &channels, std::string &err)
{
    std::vector<uint8_t> bytes;
    if (!psReadFile(path.c_str(), bytes, err))
        return false;
    if (bytes.empty())
    {
        err = "OEM7 文件为空";
        return false;
    }

#ifdef PS_GNSS_DB_DIR
    Oem7MessageParser parser(QStringLiteral(PS_GNSS_DB_DIR) + QStringLiteral("/oem7_database.json"));
#else
    Oem7MessageParser parser;
#endif
    std::map<std::string, Table> tables;
    const bool binary = bytes.size() >= 3 && bytes[0] == 0xAA && bytes[1] == 0x44;
    if (binary)
        parseBinary(bytes, parser, tables);
    else
        parseAscii(bytes, parser, tables);

    if (tables.empty())
    {
        err = "OEM7 里没有可解析字段（校验失败或协议库不匹配）";
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
        Table &table = kv.second;
        for (double &t : table.t)
            t -= t0;
        auto axis = std::make_shared<const std::vector<double>>(table.t);
        for (auto &ck : table.cols)
        {
            Column &col = ck.second;
            col.v.resize(table.t.size(), std::numeric_limits<double>::quiet_NaN());
            PsChannelData pc;
            pc.name = kv.first + "." + ck.first;
            pc.table = kv.first;
            pc.axis = axis;
            pc.unit = col.unit;
            pc.v = std::move(col.v);
            pc.value_type = PS_VALUE_F64;
            channels.push_back(std::move(pc));
        }
    }
    if (channels.empty())
    {
        err = "OEM7 里没有可解析字段";
        return false;
    }
    return true;
}

int32_t oem7Load(const char *path, const char *, const PsHostApi *host, void **dataset, char *err,
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

const PsLoaderVtable kOem7Loader = {
    PS_EXTENSION_ABI_VERSION,
    "data_loader_oem7",
    "NovAtel OEM7",
    "NovAtel OEM7 (*.gps *.dat *.oem7 *.log);;全部 (*)",
    ".gps,.dat,.oem7,.log",
    "NovAtel OEM7",
    0,
    0,
    {0, 0, 0, 0, 0, 0},
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    oem7Load,
};

const PsDataSourceVtable *oem7InputVtable()
{
    static const PsDataSourceVtable vt =
        ps::VtableBuilder::make<LegacyFileInput<&kOem7Loader>>(
            R"({"id":"data_loader_oem7","name":"NovAtel OEM7","version":"1.0.0","sdk":"1.0","kind":"file"})");
    return &vt;
}

const PsExtensionDescriptor kOem7Extension = {
    PS_EXTENSION_ABI_VERSION,
    R"({"id":"data_loader_oem7","name":"NovAtel OEM7","version":"1.0.0","sdk":"1.0","family":"data_source","kind":"file","entry":"ps_extension_entry_data_loader_oem7"})",
    PS_FAMILY_DATA_SOURCE,
    PS_CAP_FILE | PS_CAP_CACHE,
    &kOem7Loader,
    nullptr,
    nullptr,
    oem7InputVtable(),
};

} // namespace

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_oem7(void)
{
    return &kOem7Extension;
}
