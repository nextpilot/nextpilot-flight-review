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
    {
        struct Row { const char *key; std::string value; };
        std::vector<Row> rows = {
            {"format", "ulog"},
            {"file", QFileInfo(QString::fromUtf8(path)).fileName().toStdString()},
            {"params", std::to_string(log.params.size())},
        };
        const uint32_t n = static_cast<uint32_t>(rows.size());
        auto axis = std::make_shared<const std::vector<double>>();
        std::vector<double> time(n);
        for (uint32_t i = 0; i < n; ++i) time[i] = i * 1e-6;
        axis = std::make_shared<const std::vector<double>>(std::move(time));
        PsChannelData keys, values;
        keys.name = "info.key";
        keys.table = "info";
        keys.axis = axis;
        keys.value_type = PS_VALUE_TEXT;
        keys.v.assign(n, 0.0);
        values = keys;
        values.name = "info.value";
        for (const auto &r : rows)
        {
            keys.text.push_back(r.key);
            values.text.push_back(r.value);
        }
        log.channels.push_back(std::move(keys));
        log.channels.push_back(std::move(values));
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
