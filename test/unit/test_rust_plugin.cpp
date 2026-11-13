#include "app/plugin_host/HostApi.h"
#include "app/plugin_host/PluginRuntime.h"
#include "ps_kernel.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

int main()
{
    PluginRuntime &rt = PluginRuntime::instance();
    rt.clear();
    rt.scanDirectory(PS_RUST_PLUGIN_DIR);
    const PsPluginDescriptor *desc = nullptr;
    for (const PsLoadedPlugin &p : rt.plugins())
        if (p.descriptor && p.descriptor->loader &&
            std::strcmp(p.descriptor->loader->id, "data_loader_rustcsv") == 0)
            desc = p.descriptor;
    if (!desc)
    {
        std::cerr << "rust plugin not loaded\n";
        for (const auto &e : rt.errors())
            std::cerr << e << "\n";
        return 1;
    }

    const auto path = std::filesystem::temp_directory_path() / "ps_rust_plugin.csv";
    {
        std::ofstream f(path);
        f << "t,roll,pitch\n0,1,2\n1,3,4\n2,5,6\n";
    }
    void *raw = nullptr;
    char err[256] = {0};
    const int32_t rc = desc->loader->load(path.c_str(), "{\"has_header\":true}", ps_host_api(), &raw,
                                          err, sizeof(err));
    if (rc != 0)
    {
        std::cerr << "load failed rc=" << rc << " err=" << err << "\n";
        return 1;
    }
    PsDataset *ds = static_cast<PsDataset *>(raw);
    assert(ps_dataset_channel_count(ds) == 2);
    const PsChannelInfo *ch = ps_dataset_channel(ds, 0);
    assert(ch && std::strcmp(ch->name, "roll") == 0);
    assert(ch->point_count == 3);
    assert(ch->value[2] == 5.0);
    ps_dataset_free(ds);
    std::filesystem::remove(path);
    rt.clear();
    return 0;
}
