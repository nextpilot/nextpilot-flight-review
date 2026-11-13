#include "ps_plugin_api.h"

#include "UlogParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
    UlogLog log;
    std::string perr;
    if (!psParseUlog(path, log, perr))
    {
        psWriteErr(err, errCap, perr);
        return 1;
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
    PS_PLUGIN_ABI_VERSION,
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

const PsPluginDescriptor kUlogPlugin = {
    PS_PLUGIN_ABI_VERSION,
    R"({"id":"data_loader_ulog","name":"ULog","version":"1.0.0","sdk":"1.0","family":"data_loader","kind":"file","entry":"ps_plugin_entry_data_loader_ulog"})",
    PS_FAMILY_DATA_LOADER,
    PS_CAP_FILE | PS_CAP_PARAMS | PS_CAP_CACHE,
    &kUlogLoader,
    nullptr,
};

} // namespace

extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_ulog(void)
{
    return &kUlogPlugin;
}
