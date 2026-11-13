#include "ps_plugin_api.h"

#include "ps_kernel.h"

#include <cstring>

namespace {

int32_t apbinParams(const char *path, char *out, uint32_t cap)
{
    char err[256] = {0};
    const int32_t rc = ps_apbin_params(path, out, cap, err, sizeof(err));
    if (rc != 0)
        return -1;
    return static_cast<int32_t>(std::strlen(out));
}

int32_t apbinLoad(const char *path, const char *, void **dataset, char *err, uint32_t errCap)
{
    return ps_apbin_load(path, reinterpret_cast<PsDataset **>(dataset), err, errCap);
}

const PsLoaderVtable kApbinLoader = {
    PS_PLUGIN_ABI_VERSION,
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

const PsPluginDescriptor kApbinPlugin = {
    PS_PLUGIN_ABI_VERSION,
    R"({"id":"data_loader_apbin","name":"DataFlash","version":"1.0.0","sdk":"1.0","family":"data_loader","kind":"file","entry":"ps_plugin_entry_data_loader_apbin"})",
    PS_FAMILY_DATA_LOADER,
    PS_CAP_FILE | PS_CAP_PARAMS | PS_CAP_CACHE,
    &kApbinLoader,
    nullptr,
};

} // namespace

extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_apbin(void)
{
    return &kApbinPlugin;
}
