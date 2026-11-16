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

#include <QString>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
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

struct NmeaFieldDef
{
    std::string name;
    int index = -1;
    std::string type;
    int hemiIndex = -1;
    std::string unit;
};

struct NmeaSentenceDef
{
    std::vector<NmeaFieldDef> fields;
};

using NmeaDatabase = std::map<std::string, NmeaSentenceDef>;

double dmToDeg(const std::string &dm, const std::string &hemi, bool &ok)
{
    ok = false;
    if (dm.empty() || hemi.empty())
        return 0;
    double raw = std::atof(dm.c_str());
    const int deg = static_cast<int>(raw / 100.0);
    const double min = raw - deg * 100.0;
    double v = deg + min / 60.0;
    if (hemi[0] == 'S' || hemi[0] == 'W')
        v = -v;
    ok = true;
    return v;
}

double utcSeconds(const std::string &hhmmss)
{
    if (hhmmss.size() < 6)
        return 0;
    return std::atof(hhmmss.substr(0, 2).c_str()) * 3600.0 +
           std::atof(hhmmss.substr(2, 2).c_str()) * 60.0 + std::atof(hhmmss.substr(4).c_str());
}

std::vector<std::string> split(const std::string &s)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
    {
        if (c == ',')
        {
            out.push_back(cur);
            cur.clear();
        }
        else
            cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

NmeaDatabase loadNmeaDatabase()
{
    NmeaDatabase db;
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
#ifdef PS_GNSS_DB_DIR
        QStringLiteral(PS_GNSS_DB_DIR) + QStringLiteral("/nmea_database.json"),
#endif
        appDir + QStringLiteral("/nmea_database.json"),
        appDir + QStringLiteral("/../nmea_database.json"),
        QDir::currentPath() + QStringLiteral("/nmea_database.json"),
        QDir::currentPath() + QStringLiteral("/../src/extensions/builtin/gnss/nmea_database.json"),
        QDir::currentPath() + QStringLiteral("/src/extensions/builtin/gnss/nmea_database.json")};

    for (const QString &path : candidates)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            continue;

        for (const QJsonValue &value : document.object().value(QStringLiteral("sentences")).toArray())
        {
            const QJsonObject object = value.toObject();
            const std::string key = object.value(QStringLiteral("key")).toString().toStdString();
            NmeaSentenceDef def;
            for (const QJsonValue &fieldValue : object.value(QStringLiteral("fields")).toArray())
            {
                const QJsonObject field = fieldValue.toObject();
                NmeaFieldDef fd;
                fd.name = field.value(QStringLiteral("name")).toString().toStdString();
                fd.index = field.value(QStringLiteral("index")).toInt(-1);
                fd.type = field.value(QStringLiteral("type")).toString().toStdString();
                fd.hemiIndex = field.value(QStringLiteral("hemiIndex")).toInt(-1);
                fd.unit = field.value(QStringLiteral("unit")).toString().toStdString();
                if (!fd.name.empty() && fd.index >= 0)
                    def.fields.push_back(fd);
            }
            if (!key.empty() && !def.fields.empty())
                db[key] = def;
        }
        return db;
    }
    return db;
}

std::string lowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool parseFile(const std::string &path, std::vector<Channel> &channels, std::string &err)
{
    std::ifstream in(path);
    if (!in)
    {
        err = "无法打开 NMEA 文件：" + path;
        return false;
    }
    const NmeaDatabase database = loadNmeaDatabase();
    std::map<std::string, Channel> acc;
    double lastT = 0, dayOffset = 0;
    bool first = true;
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.size() < 7 || line[0] != '$')
            continue;
        const size_t star = line.find('*');
        if (star != std::string::npos)
            line = line.substr(0, star);
        auto f = split(line);
        if (f.empty())
            continue;

        std::string key = f[0].size() > 1 ? f[0].substr(1) : "";
        if (!key.empty() && key[0] != 'P' && key.size() > 3)
            key = key.substr(key.size() - 3);
        const auto it = database.find(key);
        const NmeaSentenceDef *def = it == database.end() ? nullptr : &it->second;

        int timeIndex = 1;
        if (def)
            for (const auto &field : def->fields)
                if (field.type == "time")
                {
                    timeIndex = field.index;
                    break;
                }

        double t = f.size() > static_cast<size_t>(timeIndex)
                       ? utcSeconds(f[timeIndex])
                       : 0;
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

        auto add = [&](const std::string &name, const std::string &unit, double v) {
            Channel &c = acc[name];
            c.name = name;
            c.unit = unit;
            c.t.push_back(stamp);
            c.v.push_back(v);
        };

        const std::string prefix = "nmea." + lowerCopy(key);
        if (def)
        {
            for (const auto &field : def->fields)
            {
                if (field.type == "time" || field.type == "date" || field.type == "string")
                    continue;
                if (field.index < 0 || static_cast<size_t>(field.index) >= f.size())
                    continue;
                if (f[field.index].empty())
                    continue;

                double value = 0;
                if (field.type == "dm")
                {
                    bool ok = false;
                    value = dmToDeg(f[field.index],
                                    field.hemiIndex >= 0 &&
                                            static_cast<size_t>(field.hemiIndex) < f.size()
                                        ? f[field.hemiIndex]
                                        : "",
                                    ok);
                    if (!ok)
                        continue;
                }
                else
                    value = std::atof(f[field.index].c_str());
                if (std::isfinite(value))
                    add(prefix + "." + field.name, field.unit, value);
            }
        }
        else
        {
            for (size_t i = 1; i < f.size(); ++i)
            {
                if (f[i].empty())
                    continue;
                char *end = nullptr;
                const double value = std::strtod(f[i].c_str(), &end);
                if (end == f[i].c_str() || *end != '\0' || !std::isfinite(value))
                    continue;
                add(prefix + ".field_" + std::to_string(i), "", value);
            }
        }
    }
    channels.clear();
    for (auto &kv : acc)
        channels.push_back(std::move(kv.second));
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
    std::vector<Channel> channels;
    std::string perr;
    if (!parseFile(path, channels, perr))
    {
        psWriteErr(err, errCap, perr);
        return 1;
    }
    double t0 = 0;
    bool gotT0 = false;
    for (const auto &c : channels)
        if (!c.t.empty())
        {
            t0 = gotT0 ? std::min(t0, c.t.front()) : c.t.front();
            gotT0 = true;
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
