#include "ps_extension_api.h"
#include "HostApi.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_source_sim(void);

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
    const PsExtensionDescriptor *d = ps_extension_entry_data_source_sim();
    assert(d && d->abi_version == PS_EXTENSION_ABI_VERSION);
    assert(d->family == PS_FAMILY_DATA_INPUT && d->data_input);
    const PsDataInputVtable *v = d->data_input;
    assert(v->protocol_version == PS_DATA_INPUT_PROTOCOL_VERSION);
    assert(v->struct_size >= sizeof(PsDataInputVtable));

    void *probe = v->create();
    assert(probe);
    assert((v->capabilities(probe) & PS_INPUT_STREAM) != 0);
    PsInputDialog dlg = v->get_dialog(probe);
    assert(dlg.schema_json && std::strstr(dlg.schema_json, "rate_hz"));
    assert(dlg.default_config_json && std::strstr(dlg.default_config_json, "rate_hz"));
    v->destroy(probe);

    SinkState state;
    PsSourceSink sink{&state, onBatch, onEvent};
    void *handle = v->create();
    assert(handle);
    PsDataInputServices services{};
    services.abi_version = PS_HOST_API_VERSION;
    services.host = ps_host_api();
    services.sink = &sink;
    PsError err{};
    assert(v->bind(handle, services, &err));
    const char *config = "{\"rate_hz\":200,\"channels\":2}";
    PsStringView cfg{config, static_cast<uint32_t>(std::strlen(config))};
    assert(v->load_config(handle, cfg, &err));
    assert(v->start(handle, &err));
    for (int i = 0; i < 20; ++i)
    {
        assert(v->poll(handle, &err));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(state.batches > 0);
    assert(state.points > 0);
    assert(state.names.size() == 2);
    assert(v->current_state(handle) == PS_INPUT_RUNNING || v->current_state(handle) == PS_INPUT_DONE);
    v->stop(handle);
    v->destroy(handle);

    std::cout << "data source ok: " << state.points << " points\n";
    return 0;
}
