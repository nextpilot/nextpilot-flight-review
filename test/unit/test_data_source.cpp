#include "ps_plugin_api.h"
#include "app/plugin_host/HostApi.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

extern "C" const PsPluginDescriptor *ps_plugin_entry_data_source_sim(void);

struct SinkState
{
    int batches = 0;
    int points = 0;
    std::vector<std::string> names;
};

static int32_t onBatch(void *ctx, const char *name, const char *unit,
                       const double *time_s, const double *value, uint32_t count)
{
    auto *s = static_cast<SinkState *>(ctx);
    assert(name && unit && time_s && value && count > 0);
    ++s->batches;
    s->points += static_cast<int>(count);
    for (const auto &n : s->names)
        if (n == name)
            return 0;
    s->names.emplace_back(name);
    return 0;
}

static int32_t onEvent(void *, const char *, const char *)
{
    return 0;
}

int main()
{
    const PsPluginDescriptor *d = ps_plugin_entry_data_source_sim();
    assert(d && d->abi_version == PS_PLUGIN_ABI_VERSION);
    assert(d->family == PS_FAMILY_DATA_SOURCE);
    assert(d->source && d->source->id && std::strcmp(d->source->id, "data_source_sim") == 0);
    assert(d->source->open && d->source->poll && d->source->close);

    char schema[8192] = {0};
    assert(d->source->config_schema_json(schema, sizeof(schema)) > 0);
    assert(std::strstr(schema, "rate_hz"));
    char defaults[1024] = {0};
    assert(d->source->default_config_json(defaults, sizeof(defaults)) > 0);

    SinkState state;
    PsSourceSink sink{&state, onBatch, onEvent};
    void *handle = nullptr;
    char err[256] = {0};
    assert(d->source->open("{\"rate_hz\":200,\"channels\":2}", &sink, ps_host_api(), &handle, err, sizeof(err)) == 0);
    assert(handle);
    for (int i = 0; i < 20; ++i)
    {
        assert(d->source->poll(handle, 0, err, sizeof(err)) == 0);
        struct timespec ts{0, 10 * 1000 * 1000};
        nanosleep(&ts, nullptr);
    }
    assert(state.batches > 0);
    assert(state.points > 0);
    assert(state.names.size() == 2);
    char status[512] = {0};
    assert(d->source->status_json(handle, status, sizeof(status)) > 0);
    assert(std::strstr(status, "running") || std::strstr(status, "finished"));
    assert(d->source->close(handle) == 0);

    std::cout << "data source ok: " << state.points << " points\n";
    return 0;
}
