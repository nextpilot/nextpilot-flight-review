#include "ps_extension_api.h"
#include "ps_kernel.h"
#include "HostApi.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_ulog(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_apbin(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_asc(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_blf(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_nmea(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_ubx(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_json(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_binary(void);
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_mdf4(void);
#ifdef PS_HAVE_CUSTOM_PROTOCOL
extern "C" const PsExtensionDescriptor *ps_custom_protocol_loader(void);
#endif
extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_rosbag2(void);

namespace {

bool loadAndCheck(const PsExtensionDescriptor *d, const std::string &path, uint32_t minCh)
{
    assert(d && d->loader);
    void *raw = nullptr;
    char err[256] = {0};
    const int32_t rc = d->loader->load(path.c_str(), "", ps_host_api(), &raw, err, sizeof(err));
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
        std::cerr << d->loader->id << " channels " << n << " < " << minCh
                  << " (" << path << ")\n";
        return false;
    }
    return true;
}

bool checkMdf4Value(const PsExtensionDescriptor *d, const std::string &path,
                    const char *channel, size_t index, double expected,
                    const char *unit = "rad")
{
    void *raw = nullptr;
    char err[256] = {0};
    const int32_t rc = d->loader->load(path.c_str(), "", ps_host_api(), &raw, err, sizeof(err));
    if (rc != 0)
    {
        std::cerr << "mdf4 conversion load failed: " << err << "\n";
        return false;
    }
    PsDataset *ds = static_cast<PsDataset *>(raw);
    bool found = false;
    for (uint32_t i = 0; i < ps_dataset_channel_count(ds); ++i)
    {
        const PsChannelInfo *ch = ps_dataset_channel(ds, i);
        if (!ch || !ch->name || std::strcmp(ch->name, channel) != 0)
            continue;
        found = ch->point_count > index && std::fabs(ch->value[index] - expected) < 1e-9 &&
                (!unit || (ch->unit && std::strcmp(ch->unit, unit) == 0));
        break;
    }
    ps_dataset_free(ds);
    if (!found)
    {
        std::cerr << "mdf4 conversion mismatch (" << path << "): " << channel
                  << "[" << index << "] expected " << expected << "\n";
        for (uint32_t i = 0; i < ps_dataset_channel_count(ds); ++i)
        {
            const PsChannelInfo *ch = ps_dataset_channel(ds, i);
            std::cerr << "  ch " << i << " " << (ch && ch->name ? ch->name : "?")
                      << " pts=" << (ch ? ch->point_count : 0) << "\n";
        }
    }
    return found;
}

bool checkMdf4Points(const PsExtensionDescriptor *d, const std::string &path, uint32_t expected,
                     uint32_t expectedChannels = 0)
{
    void *raw = nullptr;
    char err[256] = {0};
    const int32_t rc = d->loader->load(path.c_str(), "", ps_host_api(), &raw, err, sizeof(err));
    if (rc != 0)
    {
        std::cerr << "mdf4 load failed: " << err << " (" << path << ")\n";
        return false;
    }
    PsDataset *ds = static_cast<PsDataset *>(raw);
    bool ok = true;
    if (expectedChannels && ps_dataset_channel_count(ds) != expectedChannels)
    {
        std::cerr << "mdf4 channels " << ps_dataset_channel_count(ds) << " != "
                  << expectedChannels << " (" << path << ")\n";
        ok = false;
    }
    for (uint32_t i = 0; ok && i < ps_dataset_channel_count(ds); ++i)
    {
        const PsChannelInfo *ch = ps_dataset_channel(ds, i);
        if (!ch || ch->point_count != expected)
        {
            std::cerr << "mdf4 points mismatch: "
                      << (ch && ch->name ? ch->name : "?") << " "
                      << (ch ? ch->point_count : 0) << " != " << expected << "\n";
            ok = false;
            break;
        }
    }
    ps_dataset_free(ds);
    return ok;
}

bool loadOptsAndCheck(const PsExtensionDescriptor *d, const std::string &path,
                      const std::string &opts, uint32_t minCh)
{
    void *raw = nullptr;
    char err[256] = {0};
    const int32_t rc = d->loader->load(path.c_str(), opts.c_str(), ps_host_api(), &raw, err, sizeof(err));
    if (rc != 0) { std::cerr << "load failed: " << err << "\n"; return false; }
    PsDataset *ds = static_cast<PsDataset *>(raw);
    const uint32_t n = ps_dataset_channel_count(ds);
    ps_dataset_free(ds);
    return n >= minCh;
}

bool loadDbcAndCheck(const PsExtensionDescriptor *d, const std::string &path, const std::string &dbc,
                     const std::string &channel)
{
    assert(d && d->loader);
    void *raw = nullptr;
    char err[256] = {0};
    const std::string opts = "{\"dbc\":\"" + dbc + "\"}";
    const int32_t rc = d->loader->load(path.c_str(), opts.c_str(), ps_host_api(), &raw, err, sizeof(err));
    if (rc != 0)
    {
        std::cerr << d->loader->id << " dbc load failed: " << err << "\n";
        return false;
    }
    PsDataset *ds = static_cast<PsDataset *>(raw);
    bool found = false;
    for (uint32_t i = 0; i < ps_dataset_channel_count(ds); ++i)
    {
        const PsChannelInfo *c = ps_dataset_channel(ds, i);
        if (c && channel == c->name)
        {
            found = true;
            break;
        }
    }
    ps_dataset_free(ds);
    return found;
}

} // namespace

int main()
{
    const std::string fix = PS_FIXTURE_DIR;

    assert(ps_extension_entry_data_loader_ulog()->loader->params_json != nullptr);
    assert(ps_extension_entry_data_loader_apbin()->loader->params_json != nullptr);
    assert(ps_extension_entry_data_loader_asc()->loader->params_json == nullptr);
    assert(ps_extension_entry_data_loader_blf()->loader->params_json == nullptr);

    if (!loadAndCheck(ps_extension_entry_data_loader_ulog(), fix + "/demo_flight.ulg", 13))
        return 1;
    if (!loadAndCheck(ps_extension_entry_data_loader_apbin(), fix + "/demo_apbin.bin", 2))
        return 1;
    if (!loadAndCheck(ps_extension_entry_data_loader_asc(), fix + "/demo_can.asc", 10))
        return 1;
    if (!loadAndCheck(ps_extension_entry_data_loader_blf(), fix + "/demo_can.blf", 10))
        return 1;
    if (!loadAndCheck(ps_extension_entry_data_loader_nmea(), fix + "/demo.nmea", 7))
        return 1;
    if (!loadAndCheck(ps_extension_entry_data_loader_ubx(), fix + "/demo.ubx", 10))
        return 1;
    if (!loadAndCheck(ps_extension_entry_data_loader_json(), fix + "/demo.jsonl", 3))
        return 1;
    if (!loadAndCheck(ps_extension_entry_data_loader_binary(), fix + "/demo.cbor", 4))
        return 1;
    if (!loadAndCheck(ps_extension_entry_data_loader_binary(), fix + "/demo.msgpack", 4))
        return 1;
    if (!loadAndCheck(ps_extension_entry_data_loader_mdf4(), fix + "/demo.mf4", 2))
        return 1;
    if (!loadAndCheck(ps_extension_entry_data_loader_mdf4(), fix + "/demo_compressed.mf4", 2))
        return 1;
    if (!checkMdf4Points(ps_extension_entry_data_loader_mdf4(), fix + "/demo_multiblock.mf4", 100))
        return 1;
    if (!checkMdf4Points(ps_extension_entry_data_loader_mdf4(), fix + "/demo_transposed.mf4", 100))
        return 1;
    if (!checkMdf4Points(ps_extension_entry_data_loader_mdf4(), fix + "/demo_multi_cg.mf4", 2, 2))
        return 1;
    if (!checkMdf4Points(ps_extension_entry_data_loader_mdf4(), fix + "/demo_multi_dg.mf4", 2, 2))
        return 1;
    if (!checkMdf4Value(ps_extension_entry_data_loader_mdf4(), fix + "/demo_linear.mf4", "mdf4.roll", 0, -0.5))
        return 1;
    if (!checkMdf4Value(ps_extension_entry_data_loader_mdf4(), fix + "/demo_rational.mf4", "mdf4.roll", 0, -0.5))
        return 1;
    if (!checkMdf4Value(ps_extension_entry_data_loader_mdf4(), fix + "/demo_polynomial.mf4", "mdf4.roll", 1, 0.003942649342761085))
        return 1;
    if (!checkMdf4Value(ps_extension_entry_data_loader_mdf4(), fix + "/demo_multi_cg.mf4", "mdf4.grpA.sigA", 1, 3, nullptr))
        return 1;
    if (!checkMdf4Value(ps_extension_entry_data_loader_mdf4(), fix + "/demo_multi_dg.mf4", "mdf4.g1.sig", 1, 2, nullptr))
        return 1;
    if (!checkMdf4Value(ps_extension_entry_data_loader_mdf4(), fix + "/demo_multi_dg.mf4", "mdf4.g2.sig", 1, 20, nullptr))
        return 1;
#ifdef PS_HAVE_CUSTOM_PROTOCOL
#ifdef PS_HAVE_CUSTOM_PROTOCOL
    {
        const PsExtensionDescriptor *fd = ps_custom_protocol_loader();
        if (!fd->protocol || std::strcmp(fd->protocol->kind, "sequential") != 0)
        {
            std::fprintf(stderr, "custom protocol vtable missing\n");
            return 1;
        }
        char err[256] = {};
        if (fd->protocol->validate("{bad json", err, sizeof(err)) <= 0)
        {
            std::fprintf(stderr, "custom protocol validate did not reject invalid json\n");
            return 1;
        }
    }
#endif
    if (!loadOptsAndCheck(ps_custom_protocol_loader(), fix + "/demo.frames",
                          "{\"protocol\":\"" + fix + "/demo.psproto\"}", 2))
        return 1;
#endif
    if (!loadAndCheck(ps_extension_entry_data_loader_rosbag2(), fix + "/demo_rosbag2.db3", 2))
        return 1;
#ifdef PS_HAVE_CUSTOM_PROTOCOL
    if (!loadOptsAndCheck(ps_custom_protocol_loader(), fix + "/demo_variable.frames",
                          "{\"protocol\":\"" + fix + "/demo_variable.psproto\"}", 2))
        return 1;
#endif
#ifdef PS_HAVE_CUSTOM_PROTOCOL
    if (!loadOptsAndCheck(ps_custom_protocol_loader(), fix + "/demo_family.frames",
                          "{\"protocol\":\"" + fix + "/demo_family.psproto\"}", 2))
        return 1;
#endif
#ifdef PS_HAVE_CUSTOM_PROTOCOL
    if (!loadOptsAndCheck(ps_custom_protocol_loader(), fix + "/demo_sequential.frames",
                          "{\"protocol\":\"" + fix + "/demo_sequential.psproto\"}", 5))
        return 1;
#endif
#ifdef PS_HAVE_CUSTOM_PROTOCOL
    if (!loadOptsAndCheck(ps_custom_protocol_loader(), fix + "/demo_nmea.frames",
                          "{\"protocol\":\"" + fix + "/demo_nmea.psproto\"}", 4))
        return 1;
#endif
#ifdef PS_HAVE_CUSTOM_PROTOCOL
    if (!loadOptsAndCheck(ps_custom_protocol_loader(), fix + "/demo_generic.frames",
                          "{\"protocol\":\"" + fix + "/demo_generic.psproto\"}", 2))
        return 1;
#endif

    // DBC 模式：0x1A0 的两个字节解成命名信号，而不是 b0..b7 原始字节。
    const std::string dbcPath = "/tmp/plotscope-test-can.dbc";
    {
        std::ofstream dbc(dbcPath);
        dbc << "BO_ 416 MSG_A: 8 Vector__XXX\n"
            << " SG_ byte0 : 0|8@1+ (1,0) [0|255] \"\" Vector__XXX\n"
            << " SG_ temp : 8|8@1+ (1,-40) [0|200] \"C\" Vector__XXX\n";
    }
    if (!loadDbcAndCheck(ps_extension_entry_data_loader_asc(), fix + "/demo_can.asc", dbcPath,
                         "CAN2.MSG_A.byte0"))
        return 1;

    char params[256 * 1024] = {0};
    const int32_t pn = ps_extension_entry_data_loader_ulog()->loader->params_json(
        (fix + "/demo_params.ulg").c_str(), params, sizeof(params));
    if (pn <= 0)
    {
        std::cerr << "ulog params empty\n";
        return 1;
    }
    const int32_t an = ps_extension_entry_data_loader_apbin()->loader->params_json(
        (fix + "/demo_apbin.bin").c_str(), params, sizeof(params));
    if (an <= 0 || std::strstr(params, "TEST_PARAM") == nullptr)
    {
        std::cerr << "apbin params missing\n";
        return 1;
    }
    return 0;
}
