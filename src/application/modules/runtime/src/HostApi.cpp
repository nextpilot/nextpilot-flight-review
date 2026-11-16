#include "HostApi.h"

#include "ps_kernel.h"

namespace {

thread_local void *t_progress_ctx = nullptr;
thread_local PsHostProgressFn t_progress_fn = nullptr;

void host_progress(uint64_t done, uint64_t total)
{
    if (t_progress_fn)
        t_progress_fn(t_progress_ctx, done, total);
}

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
    &host_progress,
};

} // namespace

extern "C" const PsHostApi *ps_host_api(void)
{
    return &kHostApi;
}

extern "C" void ps_host_progress_begin(void *ctx, PsHostProgressFn fn)
{
    t_progress_ctx = ctx;
    t_progress_fn = fn;
}

extern "C" void ps_host_progress_end(void)
{
    t_progress_ctx = nullptr;
    t_progress_fn = nullptr;
}
