#include "ps_extension_api.h"
#include "LegacyFileInput.hpp"
#include "PsLoaderCommon.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

struct UbxFieldDef
{
    std::string name;
    int offset = -1;
    std::string type;
    double scale = 1.0;
    std::string unit;
};

struct UbxMessageDef
{
    int cls = -1;
    int id = -1;
    std::string name;
    std::string timeField;
    std::vector<UbxFieldDef> fields;
};

using UbxDatabase = std::map<int, UbxMessageDef>;

uint16_t u2(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

int messageKey(int cls, int id) { return ((cls & 0xFF) << 16) | (id & 0xFFFF); }

bool decodeField(const uint8_t *p, uint32_t len, const UbxFieldDef &field, double &out)
{
    if (field.offset < 0 || static_cast<uint32_t>(field.offset) > len)
        return false;
    const uint8_t *q = p + field.offset;
    const uint32_t remain = len - static_cast<uint32_t>(field.offset);
    if (field.type == "u1" && remain >= 1)
        out = q[0];
    else if (field.type == "i1" && remain >= 1)
        out = static_cast<int8_t>(q[0]);
    else if (field.type == "u2" && remain >= 2)
        out = static_cast<uint16_t>(q[0] | (q[1] << 8));
    else if (field.type == "i2" && remain >= 2)
        out = static_cast<int16_t>(q[0] | (q[1] << 8));
    else if (field.type == "u4" && remain >= 4)
        out = static_cast<uint32_t>(q[0]) | (static_cast<uint32_t>(q[1]) << 8) |
              (static_cast<uint32_t>(q[2]) << 16) | (static_cast<uint32_t>(q[3]) << 24);
    else if (field.type == "i4" && remain >= 4)
    {
        const uint32_t raw = static_cast<uint32_t>(q[0]) | (static_cast<uint32_t>(q[1]) << 8) |
                             (static_cast<uint32_t>(q[2]) << 16) | (static_cast<uint32_t>(q[3]) << 24);
        out = static_cast<int32_t>(raw);
    }
    else if (field.type == "f4" && remain >= 4)
    {
        float value = 0;
        std::memcpy(&value, q, sizeof(value));
        out = value;
    }
    else if (field.type == "f8" && remain >= 8)
    {
        double value = 0;
        std::memcpy(&value, q, sizeof(value));
        out = value;
    }
    else
        return false;
    out *= field.scale;
    return true;
}

void add(std::map<std::string, Channel> &acc, const std::string &name, const std::string &unit,
         double t, double v)
{
    Channel &c = acc[name];
    c.name = name;
    c.unit = unit;
    c.t.push_back(t);
    c.v.push_back(v);
}

UbxDatabase loadUbxDatabase()
{
    UbxDatabase db;
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
#ifdef PS_GNSS_DB_DIR
        QStringLiteral(PS_GNSS_DB_DIR) + QStringLiteral("/ubx_database.json"),
#endif
        appDir + QStringLiteral("/ubx_database.json"),
        appDir + QStringLiteral("/../ubx_database.json"),
        QDir::currentPath() + QStringLiteral("/ubx_database.json"),
        QDir::currentPath() + QStringLiteral("/../src/extensions/builtin/gnss/ubx_database.json"),
        QDir::currentPath() + QStringLiteral("/src/extensions/builtin/gnss/ubx_database.json")};

    for (const QString &path : candidates)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            continue;

        for (const QJsonValue &value : document.object().value(QStringLiteral("messages")).toArray())
        {
            const QJsonObject object = value.toObject();
            UbxMessageDef def;
            def.cls = object.value(QStringLiteral("class")).toInt(-1);
            def.id = object.value(QStringLiteral("id")).toInt(-1);
            def.name = object.value(QStringLiteral("name")).toString().toStdString();
            def.timeField = object.value(QStringLiteral("timeField")).toString().toStdString();
            if (def.cls < 0 || def.id < 0 || def.name.empty())
                continue;

            for (const QJsonValue &fieldValue : object.value(QStringLiteral("fields")).toArray())
            {
                const QJsonObject field = fieldValue.toObject();
                UbxFieldDef fd;
                fd.name = field.value(QStringLiteral("name")).toString().toStdString();
                fd.offset = field.value(QStringLiteral("offset")).toInt(-1);
                fd.type = field.value(QStringLiteral("type")).toString().toStdString();
                fd.scale = field.value(QStringLiteral("scale")).toDouble(1.0);
                fd.unit = field.value(QStringLiteral("unit")).toString().toStdString();
                if (!fd.name.empty() && fd.offset >= 0 && !fd.type.empty())
                    def.fields.push_back(fd);
            }
            if (!def.fields.empty())
                db[messageKey(def.cls, def.id)] = def;
        }
        return db;
    }
    return db;
}

bool parseFile(const std::string &path, std::vector<Channel> &channels, std::string &err)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        err = "无法打开 UBX 文件：" + path;
        return false;
    }
    const UbxDatabase database = loadUbxDatabase();
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
        const uint8_t cls = buf[pos + 2], id = buf[pos + 3];
        const uint32_t len = u2(buf.data() + pos + 4);
        if (pos + 8 + len > buf.size())
            break;
        const uint8_t *payload = buf.data() + pos + 6;

        const auto it = database.find(messageKey(cls, id));
        if (it != database.end())
        {
            const UbxMessageDef &def = it->second;
            double stamp = 0;
            for (const UbxFieldDef &field : def.fields)
                if (field.name == def.timeField)
                {
                    double raw = 0;
                    if (decodeField(payload, len, field, raw))
                        stamp = raw;
                    break;
                }

            const std::string prefix = "ubx." + def.name;
            for (const UbxFieldDef &field : def.fields)
            {
                if (field.name == def.timeField)
                    continue;
                double value = 0;
                if (decodeField(payload, len, field, value))
                    add(acc, prefix + "." + field.name, field.unit, stamp, value);
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
