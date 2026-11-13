#include "plugin_sdk/ps_plugin_api.h"

#include "ps_kernel.h"

#include <cstring>

namespace {

int32_t ulogParams(const char *path, char *out, uint32_t cap)
{
    char err[256] = {0};
    const int32_t rc = ps_ulog_params(path, out, cap, err, sizeof(err));
    if (rc != 0)
        return -1;
    return static_cast<int32_t>(std::strlen(out));
}

int32_t ulogLoad(const char *path, const char *, void **dataset, char *err, uint32_t errCap)
{
    return ps_ulog_load(path, reinterpret_cast<PsDataset **>(dataset), err, errCap);
}

const PsLoaderVtable kUlogLoader = {
    PS_PLUGIN_ABI_VERSION,
    "ulog",
    "ULog",
    "ULog (*.ulg);;全部 (*)",
    "ULog",
    1,
    1,
    {0, 0, 0, 0, 0, 0},
    nullptr,
    nullptr,
    nullptr,
    ulogParams,
    ulogLoad,
};

const PsPluginDescriptor kUlogPlugin = {
    PS_PLUGIN_ABI_VERSION,
    R"({"id":"ulog","name":"ULog","version":"1.0.0","sdk":"1.0","kind":"file","entry":"ps_plugin_entry_ulog"})",
    &kUlogLoader,
    nullptr,
};

} // namespace

extern "C" const PsPluginDescriptor *ps_plugin_entry_ulog(void)
{
    return &kUlogPlugin;
}
