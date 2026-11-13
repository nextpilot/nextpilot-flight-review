#include "plugin_sdk/ps_plugin_api.h"
#include "ps_kernel.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

extern "C" const PsPluginDescriptor *ps_plugin_entry_csv(void);

int main()
{
    const PsPluginDescriptor *desc = ps_plugin_entry_csv();
    assert(desc && desc->abi_version == PS_PLUGIN_ABI_VERSION);
    assert(desc->loader && std::strcmp(desc->loader->id, "csv") == 0);

    char schema[4096] = {0};
    const int32_t sn = desc->loader->schema_json(schema, sizeof(schema));
    assert(sn > 0);
    assert(std::strstr(schema, "\"delimiter\"") != nullptr);

    const auto path = std::filesystem::temp_directory_path() / "ps_csv_plugin_test.csv";
    {
        std::ofstream f(path);
        assert(f.good());
        f << "time,roll,pitch\n0,1.0,2.0\n1,3.0,4.0\n";
    }
    const std::string p = path.string();

    char opts[4096] = {0};
    const int32_t on = desc->loader->default_options(p.c_str(), opts, sizeof(opts));
    assert(on > 0);

    void *raw = nullptr;
    char err[256] = {0};
    const int32_t rc = desc->loader->load(p.c_str(), opts, &raw, err, sizeof(err));
    if (rc != 0)
    {
        std::cerr << "load failed: " << err << "\n";
        return 1;
    }
    PsDataset *ds = static_cast<PsDataset *>(raw);
    assert(ds != nullptr);
    assert(ps_dataset_channel_count(ds) >= 2);
    ps_dataset_free(ds);
    std::filesystem::remove(path);
    return 0;
}
