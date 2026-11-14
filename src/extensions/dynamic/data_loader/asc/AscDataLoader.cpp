#include "ps_extension_api.h"
#include "LegacyFileInput.hpp"
#include "Dbc.h"

#include "AscParser.h"

namespace {

const char *kCanSchema = R"({"title":"ASC · CAN","fields":[{"key":"dbc","type":"file","label":"DBC 文件（可选，留空导原始字节）","default":""}]})";

int32_t canSchema(char *out, uint32_t cap)
{
    if (!out || cap == 0)
        return -1;
    std::snprintf(out, cap, "%s", kCanSchema);
    return 0;
}

int32_t canDefaults(const char *, char *out, uint32_t cap)
{
    if (!out || cap == 0)
        return -1;
    std::snprintf(out, cap, "{}");
    return 0;
}

int32_t ascLoad(const char *path, const char *opts, const PsHostApi *host, void **dataset, char *err, uint32_t errCap)
{
    AscLog log;
    std::string perr;
    if (!psParseAscFile(path, log, perr))
    {
        psWriteErr(err, errCap, perr);
        return 1;
    }
    const std::string dbcPath = psResolveDbcOption(opts, path);
    std::vector<PsChannelData> channels;
    if (!dbcPath.empty())
    {
        DbcDatabase db;
        std::string dberr;
        if (!db.load(dbcPath, dberr))
        {
            psWriteErr(err, errCap, dberr);
            return 4;
        }
        channels = psCanFramesToDbcChannels(log.frames, db);
        if (channels.empty())
        {
            psWriteErr(err, errCap, "DBC 未匹配到任何 CAN 帧");
            return 5;
        }
    }
    else
    {
        channels = psCanFramesToChannels(log.frames);
    }
    if (channels.empty())
    {
        psWriteErr(err, errCap, "ASC 里没有变化的 CAN 字节（可能全是常量帧）");
        return 2;
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

const PsLoaderVtable kAscLoader = {
    PS_EXTENSION_ABI_VERSION,
    "data_loader_asc",
    "ASC · CAN",
    "Vector ASC (*.asc);;全部 (*)",
    ".asc",
    "ASC · CAN",
    1,
    0,
    {0, 0, 0, 0, 0, 0},
    canSchema,
    nullptr,
    canDefaults,
    nullptr,
    ascLoad,
};

const PsDataInputVtable *ascInputVtable()
{
    static const PsDataInputVtable vt = ps::VtableBuilder::make<LegacyFileInput<&kAscLoader>>(R"({"id":"data_loader_asc","name":"ASC","version":"1.0.0","sdk":"1.0","kind":"file"})");
    return &vt;
}

const PsExtensionDescriptor kAscExtension = {
    PS_EXTENSION_ABI_VERSION,
    R"({"id":"data_loader_asc","name":"ASC","version":"1.0.0","sdk":"1.0","family":"data_input","kind":"file","entry":"ps_extension_entry_data_loader_asc"})",
    PS_FAMILY_DATA_INPUT,
    PS_CAP_FILE | PS_CAP_CACHE,
    &kAscLoader,
    nullptr,
    nullptr,
    ascInputVtable(),
};

} // namespace

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_asc(void)
{
    return &kAscExtension;
}
