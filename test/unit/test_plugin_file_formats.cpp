#include "ps_plugin_api.h"
#include "ps_kernel.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_ulog(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_apbin(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_asc(void);
extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_blf(void);

namespace {

bool loadAndCheck(const PsPluginDescriptor *d, const std::string &path, uint32_t minCh)
{
    assert(d && d->loader);
    void *raw = nullptr;
    char err[256] = {0};
    const int32_t rc = d->loader->load(path.c_str(), "", &raw, err, sizeof(err));
    if (rc != 0)
    {
        std::cerr << d->loader->id << " load failed: " << err << "\n";
        return false;
    }
    PsDataset *ds = static_cast<PsDataset *>(raw);
    const uint32_t n = ps_dataset_channel_count(ds);

    ps_dataset_free(ds);
    if (n < minCh)
    {
    
        return false;
    }
    return true;
}

} // namespace

int main()
{
    const std::string fix = PS_FIXTURE_DIR;

    assert(ps_plugin_entry_data_loader_ulog()->loader->params_json != nullptr);
    assert(ps_plugin_entry_data_loader_apbin()->loader->params_json != nullptr);
    assert(ps_plugin_entry_data_loader_asc()->loader->params_json == nullptr);
    assert(ps_plugin_entry_data_loader_blf()->loader->params_json == nullptr);

    if (!loadAndCheck(ps_plugin_entry_data_loader_ulog(), fix + "/demo_flight.ulg", 13))
        return 1;
    if (!loadAndCheck(ps_plugin_entry_data_loader_apbin(), fix + "/demo_apbin.bin", 2))
        return 1;
    if (!loadAndCheck(ps_plugin_entry_data_loader_asc(), fix + "/demo_can.asc", 10))
        return 1;
    if (!loadAndCheck(ps_plugin_entry_data_loader_blf(), fix + "/demo_can.blf", 10))
        return 1;

    char params[256 * 1024] = {0};
    const int32_t pn = ps_plugin_entry_data_loader_ulog()->loader->params_json(
        (fix + "/demo_params.ulg").c_str(), params, sizeof(params));
    if (pn <= 0)
    {
        std::cerr << "ulog params empty\n";
        return 1;
    }
    const int32_t an = ps_plugin_entry_data_loader_apbin()->loader->params_json(
        (fix + "/demo_apbin.bin").c_str(), params, sizeof(params));
    if (an <= 0 || std::strstr(params, "TEST_PARAM") == nullptr)
    {
        std::cerr << "apbin params missing\n";
        return 1;
    }
    return 0;
}
