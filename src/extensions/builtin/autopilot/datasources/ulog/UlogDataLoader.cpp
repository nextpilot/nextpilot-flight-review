#include "ps_extension_api.h"
#include "LegacyFileInput.hpp"

#include "UlogParser.h"
#include "PsLoaderCommon.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QString>

namespace {

int32_t ulogParams(const char *path, char *out, uint32_t cap)
{
    UlogLog log;
    std::string err;
    if (!psParseUlog(path, log, err))
        return -1;
    QJsonArray arr;
    for (const auto &p : log.params)
    {
        QJsonObject o;
        o[QStringLiteral("name")] = QString::fromStdString(p.first);
        o[QStringLiteral("value")] = QString::fromStdString(p.second);
        arr.append(o);
    }
    return psWriteJson(QJsonDocument(arr).toJson(QJsonDocument::Compact), out, cap);
}

int32_t ulogLoad(const char *path, const char *, const PsHostApi *host, void **dataset, char *err, uint32_t errCap)
{
    psLoaderSetHost(host);
    UlogLog log;
    std::string perr;
    if (!psParseUlog(path, log, perr))
    {
        psWriteErr(err, errCap, perr);
        return 1;
    }
    if (!log.params.empty())
    {
        const uint32_t n = static_cast<uint32_t>(log.params.size());
        auto axis = std::make_shared<const std::vector<double>>();
        std::vector<double> time(n);
        for (uint32_t i = 0; i < n; ++i)
            time[i] = i * 1e-6;
        axis = std::make_shared<const std::vector<double>>(std::move(time));
        PsChannelData names;
        names.name = "params.name";
        names.table = "params";
        names.axis = axis;
        names.value_type = PS_VALUE_TEXT;
        names.v.assign(n, 0.0);
        PsChannelData values = names;
        values.name = "params.value";
        for (const auto &p : log.params)
        {
            names.text.push_back(p.first);
            values.text.push_back(p.second);
        }
        log.channels.push_back(std::move(names));
        log.channels.push_back(std::move(values));
    }
    auto textTable = [&](const std::string &table, const std::vector<std::pair<double, std::pair<std::string, std::string>>> &rows) {
        if (rows.empty()) return;
        const uint32_t n = static_cast<uint32_t>(rows.size());
        std::vector<double> time(n);
        for (uint32_t i = 0; i < n; ++i) time[i] = rows[i].first;
        auto axis = std::make_shared<const std::vector<double>>(std::move(time));
        PsChannelData keys, values;
        keys.name = table + ".key";
        keys.table = table;
        keys.axis = axis;
        keys.value_type = PS_VALUE_TEXT;
        keys.v.assign(n, 0.0);
        values = keys;
        values.name = table + ".value";
        for (const auto &r : rows) {
            keys.text.push_back(r.second.first);
            values.text.push_back(r.second.second);
        }
        log.channels.push_back(std::move(keys));
        log.channels.push_back(std::move(values));
    };
    std::vector<std::pair<double, std::pair<std::string, std::string>>> infoRows;
    infoRows.push_back({0.0, {"format", "ulog"}});
    infoRows.push_back({0.0, {"file", QFileInfo(QString::fromUtf8(path)).fileName().toStdString()}});
    infoRows.push_back({0.0, {"params", std::to_string(log.params.size())}});
    for (const auto &kv : log.facts) infoRows.push_back({0.0, kv});
    textTable("facts", infoRows);

    if (!log.events.empty())
    {
        const uint32_t n = static_cast<uint32_t>(log.events.size());
        std::vector<double> time(n);
        for (uint32_t i = 0; i < n; ++i) time[i] = log.events[i].t;
        auto axis = std::make_shared<const std::vector<double>>(std::move(time));
        PsChannelData level, tag, text;
        level.name = "events.level"; level.table = "events"; level.axis = axis;
        level.value_type = PS_VALUE_U8; level.v.resize(n);
        tag.name = "events.tag"; tag.table = "events"; tag.axis = axis;
        tag.value_type = PS_VALUE_U16; tag.v.resize(n);
        text.name = "events.text"; text.table = "events"; text.axis = axis;
        text.value_type = PS_VALUE_TEXT; text.v.assign(n, 0.0);
        for (uint32_t i = 0; i < n; ++i) {
            level.v[i] = log.events[i].level;
            tag.v[i] = log.events[i].tagged ? log.events[i].tag : 0;
            text.text.push_back(log.events[i].text);
            if (!log.events[i].tagged) tag.nulls.push_back(0);
        }
        log.channels.push_back(std::move(level));
        log.channels.push_back(std::move(tag));
        log.channels.push_back(std::move(text));
    }

    if (!log.dropouts.empty())
    {
        const uint32_t n = static_cast<uint32_t>(log.dropouts.size());
        std::vector<double> time(n);
        for (uint32_t i = 0; i < n; ++i) time[i] = log.dropouts[i].first;
        auto axis = std::make_shared<const std::vector<double>>(std::move(time));
        PsChannelData dur;
        dur.name = "dropouts.duration"; dur.table = "dropouts"; dur.axis = axis;
        dur.value_type = PS_VALUE_U16; dur.v.resize(n);
        for (uint32_t i = 0; i < n; ++i) dur.v[i] = log.dropouts[i].second;
        log.channels.push_back(std::move(dur));
    }
    PsDataset *ds = psBuildDataset(log.channels, perr, host);
    if (!ds)
    {
        psWriteErr(err, errCap, perr);
        return 2;
    }
    *dataset = ds;
    return 0;
}

const PsLoaderVtable kUlogLoader = {
    PS_EXTENSION_ABI_VERSION,
    "data_loader_ulog",
    "ULog",
    "ULog (*.ulg);;全部 (*)",
    ".ulg",
    "ULog",
    1,
    1,
    {0, 0, 0, 0, 0, 0},
    nullptr,
    nullptr,
    nullptr,
    ulogParams,
    ulogLoad,
};

const PsDataSourceVtable *ulogInputVtable()
{
    static const PsDataSourceVtable vt = ps::VtableBuilder::make<LegacyFileInput<&kUlogLoader>>(R"({"id":"data_loader_ulog","name":"ULog","version":"1.0.0","sdk":"1.0","kind":"file"})");
    return &vt;
}

const PsExtensionDescriptor kUlogExtension = {
    PS_EXTENSION_ABI_VERSION,
    R"({"id":"data_loader_ulog","name":"ULog","version":"1.0.0","sdk":"1.0","family":"data_source","kind":"file","entry":"ps_extension_entry_data_loader_ulog"})",
    PS_FAMILY_DATA_SOURCE,
    PS_CAP_FILE | PS_CAP_PARAMS | PS_CAP_CACHE,
    &kUlogLoader,
    nullptr,
    nullptr,
    ulogInputVtable(),
};

} // namespace

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_ulog(void)
{
    return &kUlogExtension;
}
