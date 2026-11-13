#include "ps_plugin_api.h"

#include "ps_kernel.h"

namespace {

int32_t ascLoad(const char *path, const char *, void **dataset, char *err, uint32_t errCap)
{
    return ps_asc_load(path, reinterpret_cast<PsDataset **>(dataset), err, errCap);
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
