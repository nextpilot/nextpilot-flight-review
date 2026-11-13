#include "app/plugin_host/HostApi.h"

#include "ps_kernel.h"

namespace {

extern "C" void *host_dataset_new(void)
{
    return ps_dataset_new();
}

extern "C" int32_t host_dataset_add_channel(void *dataset, const char *name, const char *unit,
                                            const double *time_s, const double *value, uint32_t count,
                                            char *err, uint32_t err_cap)
{
    return ps_dataset_add_channel(static_cast<PsDataset *>(dataset), name, unit, time_s, value,
                                  count, err, err_cap);
}

extern "C" void host_dataset_free(void *dataset)
{
    ps_dataset_free(static_cast<PsDataset *>(dataset));
}

const PsHostApi kHostApi = {
    PS_HOST_API_VERSION,
    PS_ABI_VERSION,
    &host_dataset_new,
    &host_dataset_add_channel,
    &host_dataset_free,
};

} // namespace

extern "C" const PsHostApi *ps_host_api(void)
{
    return &kHostApi;
}
