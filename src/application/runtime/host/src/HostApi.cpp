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

extern "C" int32_t host_dataset_add_table(void *dataset, const char *table, const double *time_s,
                                          uint32_t count, char *err, uint32_t err_cap)
{
    return ps_dataset_add_table(static_cast<PsDataset *>(dataset), table, time_s, count, err, err_cap);
}

extern "C" int32_t host_dataset_add_table_channel(void *dataset, const char *table, const char *name,
                                                  const char *unit, const double *value,
                                                  uint32_t count, char *err, uint32_t err_cap)
{
    return ps_dataset_add_table_channel(static_cast<PsDataset *>(dataset), table, name, unit, value,
                                        count, err, err_cap);
}

extern "C" int32_t host_dataset_add_table_typed(void *dataset, const char *table,
                                                const int64_t *time_us, uint32_t count,
                                                char *err, uint32_t err_cap)
{
    return ps_dataset_add_table_typed(static_cast<PsDataset *>(dataset), table, time_us, count,
                                      err, err_cap);
}

extern "C" int32_t host_dataset_add_table_column_typed(void *dataset, const char *table,
                                                       const char *name, const char *unit,
                                                       uint32_t value_type, const void *values,
                                                       const uint8_t *nulls, uint32_t count,
                                                       char *err, uint32_t err_cap)
{
    return ps_dataset_add_table_column_typed(static_cast<PsDataset *>(dataset), table, name, unit,
                                             value_type, values, nulls, count, err, err_cap);
}

extern "C" int32_t host_dataset_append_frame(void *dataset, const char *table,
                                             const int64_t *time_us, uint32_t rows,
                                             const PsFrameColumn *columns, uint32_t column_count,
                                             char *err, uint32_t err_cap)
{
    return ps_dataset_append_frame(static_cast<PsDataset *>(dataset), table, time_us, rows, columns,
                                   column_count, err, err_cap);
}

extern "C" int32_t host_dataset_set_ring_budget(void *dataset, const char *table, uint64_t bytes,
                                                 char *err, uint32_t err_cap)
{
    return ps_dataset_set_ring_budget(static_cast<PsDataset *>(dataset), table, bytes, err, err_cap);
}

extern "C" uint64_t host_dataset_table_rows(void *dataset, const char *table)
{
    return ps_dataset_table_rows(static_cast<PsDataset *>(dataset), table);
}

extern "C" uint32_t host_dataset_lod_minmax(void *dataset, const char *table, const char *column,
                                            int64_t t0_us, int64_t t1_us, uint32_t px,
                                            double *out, uint32_t cap)
{
    return ps_dataset_lod_minmax(static_cast<PsDataset *>(dataset), table, column, t0_us, t1_us,
                                 px, out, cap);
}

extern "C" void host_dataset_free(void *dataset)
{
    ps_dataset_free(static_cast<PsDataset *>(dataset));
}

const PsHostApi kHostApi = {
    PS_HOST_API_VERSION,
    PS_ABI_VERSION,
    &host_dataset_new,
    &host_dataset_free,
    &host_progress,
    &host_dataset_add_table,
    &host_dataset_add_table_channel,
    &host_dataset_add_table_typed,
    &host_dataset_add_table_column_typed,
    &host_dataset_append_frame,
    &host_dataset_set_ring_budget,
    &host_dataset_table_rows,
    &host_dataset_lod_minmax,
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
