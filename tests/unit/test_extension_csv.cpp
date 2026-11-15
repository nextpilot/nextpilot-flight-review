#include "ps_extension_api.h"
#include "ps_kernel.h"
#include "HostApi.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_csv(void);

int main()
{
    const PsExtensionDescriptor *desc = ps_extension_entry_data_loader_csv();
    assert(desc && desc->abi_version == PS_EXTENSION_ABI_VERSION);
    assert(desc->family == PS_FAMILY_DATA_SOURCE);
    assert(desc->capabilities & PS_CAP_CACHE);
    assert(desc->loader && std::strcmp(desc->loader->id, "data_loader_csv") == 0);
    assert(std::strcmp(desc->loader->extensions, ".csv,.tsv,.txt") == 0);

    char schema[4096] = {0};
    const int32_t sn = desc->loader->schema_json(schema, sizeof(schema));
    assert(sn > 0);
    assert(std::strstr(schema, "\"delimiter\"") != nullptr);
    assert(std::strstr(schema, "\"initial_from_probe\"") != nullptr);
    assert(std::strstr(schema, "\"ui\"") != nullptr);
    assert(std::strstr(schema, "\"preview\"") != nullptr);

    const auto path = std::filesystem::temp_directory_path() / "ps_csv_extension_test.csv";
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
    const int32_t rc = desc->loader->load(p.c_str(), opts, ps_host_api(), &raw, err, sizeof(err));
    if (rc != 0)
    {
        std::cerr << "load failed: " << err << "\n";
        return 1;
    }
    PsDataset *ds = static_cast<PsDataset *>(raw);
    assert(ds != nullptr);
    assert(ps_dataset_channel_count(ds) >= 2);
    ps_dataset_free(ds);

    // 统一 ABI 的 import_file 路径：宿主只看 PS_INPUT_FILE，不再分 loader 族。
    const PsDataSourceVtable *vt = desc->data_source;
    assert(vt && vt->import_file);
    void *ctx = vt->create();
    assert(ctx);
    PsDataSourceServices services{};
    services.abi_version = PS_HOST_API_VERSION;
    services.host = ps_host_api();
    PsError pe{};
    assert(vt->bind(ctx, services, &pe));
    void *raw2 = nullptr;
    char err2[256] = {0};
    const int32_t rc2 = vt->import_file(ctx, p.c_str(), opts, &raw2, err2, sizeof(err2));
    if (rc2 != 0)
    {
        std::cerr << "import_file failed: " << err2 << "\n";
        return 1;
    }
    PsDataset *ds2 = static_cast<PsDataset *>(raw2);
    assert(ds2 && ps_dataset_channel_count(ds2) >= 2);
    ps_dataset_free(ds2);
    vt->destroy(ctx);

    std::filesystem::remove(path);
    return 0;
}
