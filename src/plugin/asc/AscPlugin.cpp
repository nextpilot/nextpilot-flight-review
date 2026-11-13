#include "plugin_sdk/ps_plugin_api.h"

#include "ps_kernel.h"

namespace {

int32_t ascLoad(const char *path, const char *, void **dataset, char *err, uint32_t errCap)
{
    return ps_asc_load(path, reinterpret_cast<PsDataset **>(dataset), err, errCap);
}

const PsLoaderVtable kAscLoader = {
    PS_PLUGIN_ABI_VERSION,
    "asc",
    "ASC · CAN",
    "Vector ASC (*.asc);;全部 (*)",
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
    R"({"id":"asc","name":"ASC","version":"1.0.0","sdk":"1.0","kind":"file","entry":"ps_plugin_entry_asc"})",
    &kAscLoader,
    nullptr,
};

} // namespace

extern "C" const PsPluginDescriptor *ps_plugin_entry_asc(void)
{
    return &kAscPlugin;
}
