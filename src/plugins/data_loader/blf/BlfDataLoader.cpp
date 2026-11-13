#include "ps_plugin_api.h"

#include "ps_kernel.h"

namespace {

int32_t blfLoad(const char *path, const char *, void **dataset, char *err, uint32_t errCap)
{
    return ps_blf_load(path, reinterpret_cast<PsDataset **>(dataset), err, errCap);
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

const PsPluginDescriptor kBlfPlugin = {
    PS_PLUGIN_ABI_VERSION,
    R"({"id":"data_loader_blf","name":"BLF","version":"1.0.0","sdk":"1.0","family":"data_loader","kind":"file","entry":"ps_plugin_entry_data_loader_blf"})",
    PS_FAMILY_DATA_LOADER,
    PS_CAP_FILE | PS_CAP_CACHE,
    &kBlfLoader,
    nullptr,
};

} // namespace

extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_blf(void)
{
    return &kBlfPlugin;
}
