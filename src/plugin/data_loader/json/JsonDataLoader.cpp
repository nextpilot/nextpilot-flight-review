#include "ps_plugin_api.h"
#include "LegacyFileInput.hpp"
#include "PsLoaderCommon.h"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <cmath>
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

void flatten(const QString &prefix, const QJsonObject &obj, double t, std::map<std::string, Channel> &out)
{
    for (auto it = obj.begin(); it != obj.end(); ++it)
    {
        if (it.key() == QStringLiteral("time") || it.key() == QStringLiteral("t") ||
            it.key() == QStringLiteral("timestamp") || it.key() == QStringLiteral("channel") ||
            it.key() == QStringLiteral("name") || it.key() == QStringLiteral("value") ||
            it.key() == QStringLiteral("unit"))
            continue;
        const QString name = prefix.isEmpty() ? it.key() : prefix + QLatin1Char('.') + it.key();
        if (it.value().isObject())
            flatten(name, it.value().toObject(), t, out);
        else if (it.value().isDouble())
        {
            auto &c = out[("json." + name).toStdString()];
            c.name = "json." + name.toStdString();
            c.t.push_back(t);
            c.v.push_back(it.value().toDouble());
        }
    }
}

bool parseObjects(const QJsonArray &arr, std::map<std::string, Channel> &out, std::string &err)
{
    double index = 0;
    for (const QJsonValue &v : arr)
    {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        double t = index++;
        for (const char *k : {"time", "t", "timestamp"})
        {
            const QJsonValue tv = o.value(QLatin1String(k));
            if (tv.isDouble())
            {
                t = tv.toDouble();
                break;
            }
        }
        if (o.contains(QStringLiteral("channel")) && o.value(QStringLiteral("value")).isDouble())
        {
            const QString ch = o.value(QStringLiteral("channel")).toString(
                o.value(QStringLiteral("name")).toString());
            if (!ch.isEmpty())
            {
                const std::string key = "json." + ch.toStdString();
                auto &c = out[key];
                c.name = key;
                c.unit = o.value(QStringLiteral("unit")).toString().toStdString();
                c.t.push_back(t);
                c.v.push_back(o.value(QStringLiteral("value")).toDouble());
                continue;
            }
        }
        flatten(QString(), o, t, out);
    }
    if (out.empty())
    {
        err = "JSON 里没有数值字段";
        return false;
    }
    return true;
}

bool loadFile(const std::string &path, std::map<std::string, Channel> &out, std::string &err)
{
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly))
    {
        err = "无法打开 JSON 文件：" + path;
        return false;
    }
    const QByteArray bytes = f.readAll();
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &pe);
    if (pe.error == QJsonParseError::NoError)
    {
        if (doc.isArray())
            return parseObjects(doc.array(), out, err);
        if (doc.isObject())
        {
            // 支持 {"data":[...]} 或单对象
            const QJsonObject o = doc.object();
            if (o.value(QStringLiteral("data")).isArray())
                return parseObjects(o.value(QStringLiteral("data")).toArray(), out, err);
            return parseObjects(QJsonArray{o}, out, err);
        }
    }
    // JSONL / NDJSON：逐行解析
    out.clear();
    QJsonArray arr;
    for (const QByteArray &line : bytes.split('\n'))
    {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        QJsonParseError le{};
        const QJsonDocument ld = QJsonDocument::fromJson(trimmed, &le);
        if (le.error == QJsonParseError::NoError && ld.isObject())
            arr.append(ld.object());
    }
    if (arr.isEmpty())
    {
        err = "不是合法 JSON 或 JSONL";
        return false;
    }
    return parseObjects(arr, out, err);
}

int32_t jsonLoad(const char *path, const char *, const PsHostApi *host, void **dataset, char *err,
                 uint32_t errCap)
{
    std::map<std::string, Channel> acc;
    std::string perr;
    if (!loadFile(path, acc, perr))
    {
        psWriteErr(err, errCap, perr);
        return 1;
    }
    std::vector<PsChannelData> data;
    data.reserve(acc.size());
    for (auto &kv : acc)
    {
        PsChannelData pc;
        pc.name = kv.second.name;
        pc.unit = kv.second.unit;
        pc.t = std::move(kv.second.t);
        pc.v = std::move(kv.second.v);
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

const PsLoaderVtable kJsonLoader = {
    PS_PLUGIN_ABI_VERSION,
    "data_loader_json",
    "JSON · 结构化日志",
    "JSON/JSONL (*.json *.jsonl *.ndjson);;全部 (*)",
    ".json,.jsonl,.ndjson",
    "JSON · 结构化日志",
    0,
    0,
    {0, 0, 0, 0, 0, 0},
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    jsonLoad,
};

const PsDataInputVtable *jsonInputVtable()
{
    static const PsDataInputVtable vt = ps::VtableBuilder::make<LegacyFileInput<&kJsonLoader>>(R"({"id":"data_loader_json","name":"JSON","version":"1.0.0","sdk":"1.0","kind":"file"})");
    return &vt;
}

const PsPluginDescriptor kJsonPlugin = {
    PS_PLUGIN_ABI_VERSION,
    R"({"id":"data_loader_json","name":"JSON","version":"1.0.0","sdk":"1.0","family":"data_input","kind":"file","entry":"ps_plugin_entry_data_loader_json"})",
    PS_FAMILY_DATA_INPUT,
    PS_CAP_FILE | PS_CAP_CACHE,
    &kJsonLoader,
    nullptr,
    nullptr,
    jsonInputVtable(),
};

} // namespace

extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_json(void)
{
    return &kJsonPlugin;
}
