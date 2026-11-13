#include "plugin_sdk/ps_plugin_api.h"

#include "ps_kernel.h"

namespace {

int32_t blfLoad(const char *path, const char *, void **dataset, char *err, uint32_t errCap)
{
    return ps_blf_load(path, reinterpret_cast<PsDataset **>(dataset), err, errCap);
}

const PsLoaderVtable kBlfLoader = {
    PS_PLUGIN_ABI_VERSION,
    "blf",
    "BLF · CAN",
    "Vector BLF (*.blf);;全部 (*)",
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
    R"({"id":"blf","name":"BLF","version":"1.0.0","sdk":"1.0","kind":"file","entry":"ps_plugin_entry_blf"})",
    &kBlfLoader,
    nullptr,
};

} // namespace

extern "C" const PsPluginDescriptor *ps_plugin_entry_blf(void)
{
    return &kBlfPlugin;
}
