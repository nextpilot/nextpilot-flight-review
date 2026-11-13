#include "ps_plugin_api.h"
#include "LegacyFileInput.hpp"

#include "BlfParser.h"

namespace {

int32_t blfLoad(const char *path, const char *, const PsHostApi *host, void **dataset, char *err, uint32_t errCap)
{
    BlfLog log;
    std::string perr;
    if (!psParseBlfFile(path, log, perr))
    {
        psWriteErr(err, errCap, perr);
        return 1;
    }
    auto channels = psCanFramesToChannels(log.frames);
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
    PS_PLUGIN_ABI_VERSION,
    "data_loader_blf",
    "BLF · CAN",
    "Vector BLF (*.blf);;全部 (*)",
    ".blf",
    "BLF · CAN",
    1,
    0,
    {0, 0, 0, 0, 0, 0},
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    blfLoad,
};

const PsDataInputVtable *blfInputVtable()
{
    static const PsDataInputVtable vt = ps::VtableBuilder::make<LegacyFileInput<&kBlfLoader>>(R"({"id":"data_loader_blf","name":"BLF","version":"1.0.0","sdk":"1.0","kind":"file"})");
    return &vt;
}

const PsPluginDescriptor kBlfPlugin = {
    PS_PLUGIN_ABI_VERSION,
    R"({"id":"data_loader_blf","name":"BLF","version":"1.0.0","sdk":"1.0","family":"data_input","kind":"file","entry":"ps_plugin_entry_data_loader_blf"})",
    PS_FAMILY_DATA_INPUT,
    PS_CAP_FILE | PS_CAP_CACHE,
    &kBlfLoader,
    nullptr,
    nullptr,
    blfInputVtable(),
};

} // namespace

extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_blf(void)
{
    return &kBlfPlugin;
}
