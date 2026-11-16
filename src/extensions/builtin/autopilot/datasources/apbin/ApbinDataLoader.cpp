#include "ps_extension_api.h"
#include "LegacyFileInput.hpp"

#include "ApbinParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace {

int32_t apbinParams(const char *path, char *out, uint32_t cap)
{
    ApbinLog log;
    std::string err;
    if (!psParseApbin(path, log, err))
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

int32_t apbinLoad(const char *path, const char *, const PsHostApi *host, void **dataset, char *err, uint32_t errCap)
{
    psLoaderSetHost(host);
    ApbinLog log;
    std::string perr;
    if (!psParseApbin(path, log, perr))
    {
        psWriteErr(err, errCap, perr);
        return 1;
    }
    if (log.channels.empty())
    {
        psWriteErr(err, errCap, "没有可画的数值通道");
        return 2;
    }
    PsDataset *ds = psBuildDataset(log.channels, perr, host);
    if (!ds)
    {
        psWriteErr(err, errCap, perr);
        return 3;
    }
    *dataset = ds;
    return 0;
}

const PsLoaderVtable kApbinLoader = {
    PS_EXTENSION_ABI_VERSION,
    "data_loader_apbin",
    "DataFlash",
    "ArduPilot BIN (*.bin);;全部 (*)",
    ".bin",
    "ArduPilot · BIN",
    1,
    1,
    {0, 0, 0, 0, 0, 0},
    nullptr,
    nullptr,
    nullptr,
    apbinParams,
    apbinLoad,
};

const PsDataSourceVtable *apbinInputVtable()
{
    static const PsDataSourceVtable vt = ps::VtableBuilder::make<LegacyFileInput<&kApbinLoader>>(R"({"id":"data_loader_apbin","name":"DataFlash","version":"1.0.0","sdk":"1.0","kind":"file"})");
    return &vt;
}

const PsExtensionDescriptor kApbinExtension = {
    PS_EXTENSION_ABI_VERSION,
    R"({"id":"data_loader_apbin","name":"DataFlash","version":"1.0.0","sdk":"1.0","family":"data_source","kind":"file","entry":"ps_extension_entry_data_loader_apbin"})",
    PS_FAMILY_DATA_SOURCE,
    PS_CAP_FILE | PS_CAP_PARAMS | PS_CAP_CACHE,
    &kApbinLoader,
    nullptr,
    nullptr,
    apbinInputVtable(),
};

} // namespace

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_apbin(void)
{
    return &kApbinExtension;
}
