#include "ps_plugin_api.h"

#include "AscParser.h"

namespace {

int32_t ascLoad(const char *path, const char *, const PsHostApi *host, void **dataset, char *err, uint32_t errCap)
{
    AscLog log;
    std::string perr;
    if (!psParseAscFile(path, log, perr))
    {
        psWriteErr(err, errCap, perr);
        return 1;
    }
    auto channels = psCanFramesToChannels(log.frames);
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
    PS_PLUGIN_ABI_VERSION,
    "data_loader_asc",
    "ASC · CAN",
    "Vector ASC (*.asc);;全部 (*)",
    ".asc",
    "ASC · CAN",
    1,
    0,
    {0, 0, 0, 0, 0, 0},
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    ascLoad,
};

const PsPluginDescriptor kAscPlugin = {
    PS_PLUGIN_ABI_VERSION,
    R"({"id":"data_loader_asc","name":"ASC","version":"1.0.0","sdk":"1.0","family":"data_loader","kind":"file","entry":"ps_plugin_entry_data_loader_asc"})",
    PS_FAMILY_DATA_LOADER,
    PS_CAP_FILE | PS_CAP_CACHE,
    &kAscLoader,
    nullptr,
};

} // namespace

extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_asc(void)
{
    return &kAscPlugin;
}
