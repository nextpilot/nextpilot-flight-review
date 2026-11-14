#include "ps_extension_api.h"
#include "LegacyFileInput.hpp"
#include "Dbc.h"

#include "BlfParser.h"

namespace {

const char *kCanSchema = R"({"title":"BLF · CAN","fields":[{"key":"dbc","type":"file","label":"DBC 文件（可选，留空导原始字节）","default":""}]})";

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

int32_t blfLoad(const char *path, const char *opts, const PsHostApi *host, void **dataset, char *err, uint32_t errCap)
{
    BlfLog log;
    std::string perr;
    if (!psParseBlfFile(path, log, perr))
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
        psWriteErr(err, errCap, "BLF 里没有变化的 CAN 字节（可能全是常量帧）");
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

const PsLoaderVtable kBlfLoader = {
    PS_EXTENSION_ABI_VERSION,
    "data_loader_blf",
    "BLF · CAN",
    "Vector BLF (*.blf);;全部 (*)",
    ".blf",
    "BLF · CAN",
    1,
    0,
    {0, 0, 0, 0, 0, 0},
    canSchema,
    nullptr,
    canDefaults,
    nullptr,
    blfLoad,
};

const PsDataInputVtable *blfInputVtable()
{
    static const PsDataInputVtable vt = ps::VtableBuilder::make<LegacyFileInput<&kBlfLoader>>(R"({"id":"data_loader_blf","name":"BLF","version":"1.0.0","sdk":"1.0","kind":"file"})");
    return &vt;
}

const PsExtensionDescriptor kBlfExtension = {
    PS_EXTENSION_ABI_VERSION,
    R"({"id":"data_loader_blf","name":"BLF","version":"1.0.0","sdk":"1.0","family":"data_input","kind":"file","entry":"ps_extension_entry_data_loader_blf"})",
    PS_FAMILY_DATA_INPUT,
    PS_CAP_FILE | PS_CAP_CACHE,
    &kBlfLoader,
    nullptr,
    nullptr,
    blfInputVtable(),
};

} // namespace

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_blf(void)
{
    return &kBlfExtension;
}
