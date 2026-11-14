#include "ps_extension_api.h"
#include "ps_kernel.h"
#include "host/extension/HostApi.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_load_mcap(void);

namespace {

using Bytes = std::vector<uint8_t>;

void putU16(Bytes &out, uint16_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void putU32(Bytes &out, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
}

void putU64(Bytes &out, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
}

void putString(Bytes &out, const std::string &s)
{
    putU32(out, static_cast<uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

void putRecord(Bytes &out, uint8_t op, const Bytes &body)
{
    out.push_back(op);
    putU64(out, body.size());
    out.insert(out.end(), body.begin(), body.end());
}

void writeMcap(const std::filesystem::path &path, const std::string &encoding,
               const std::string &json1, const std::string &json2)
{
    static const uint8_t magic[8] = {0x89, 'M', 'C', 'A', 'P', '0', '\r', '\n'};
    Bytes out;
    out.insert(out.end(), magic, magic + 8);

    Bytes header;
    putString(header, "plotscope");
    putString(header, "test");
    putRecord(out, 0x01, header);

    Bytes schema;
    putU16(schema, 1);
    putString(schema, "json");
    putString(schema, "jsonschema");
    const std::string schemaData = "{}";
    putU32(schema, static_cast<uint32_t>(schemaData.size()));
    schema.insert(schema.end(), schemaData.begin(), schemaData.end());
    putRecord(out, 0x03, schema);

    Bytes channel;
    putU16(channel, 1);
    putU16(channel, 1);
    putString(channel, "/telemetry");
    putString(channel, encoding);
    putU32(channel, 0);
    putRecord(out, 0x04, channel);

    for (int i = 0; i < 2; ++i)
    {
        Bytes msg;
        putU16(msg, 1);
        putU32(msg, static_cast<uint32_t>(i));
        const uint64_t t = 1000000000ull + static_cast<uint64_t>(i) * 100000000ull;
        putU64(msg, t);
        putU64(msg, t);
        const std::string &json = i == 0 ? json1 : json2;
        msg.insert(msg.end(), json.begin(), json.end());
        putRecord(out, 0x05, msg);
    }

    Bytes dataEnd;
    putU32(dataEnd, 0);
    putRecord(out, 0x0F, dataEnd);

    Bytes footer;
    putU64(footer, 0);
    putU64(footer, 0);
    putU32(footer, 0);
    putRecord(out, 0x02, footer);
    out.insert(out.end(), magic, magic + 8);

    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(out.data()), static_cast<std::streamsize>(out.size()));
}

const PsChannelInfo *findChannel(PsDataset *ds, const char *name)
{
    for (uint32_t i = 0; i < ps_dataset_channel_count(ds); ++i)
    {
        const PsChannelInfo *ch = ps_dataset_channel(ds, i);
        if (ch && std::strcmp(ch->name, name) == 0)
            return ch;
    }
    return nullptr;
}

} // namespace

int main()
{
    const PsExtensionDescriptor *desc = ps_extension_entry_data_load_mcap();
    assert(desc && desc->abi_version == PS_EXTENSION_ABI_VERSION);
    assert(desc->family == PS_FAMILY_DATA_INPUT);
    const PsDataInputVtable *vt = desc->data_input;
    assert(vt && vt->import_file);

    const auto path = std::filesystem::temp_directory_path() / "ps_mcap_loader_test.mcap";
    writeMcap(path, "json", R"({"roll":1.5,"pitch":-2.0})", R"({"roll":2.5,"pitch":-3.0})");

    void *ctx = vt->create();
    assert(ctx);
    PsDataInputServices services{};
    services.abi_version = PS_HOST_API_VERSION;
    services.host = ps_host_api();
    PsError pe{};
    assert(vt->bind(ctx, services, &pe));

    void *raw = nullptr;
    char err[256] = {0};
    const int32_t rc = vt->import_file(ctx, path.string().c_str(), "{}", &raw, err, sizeof(err));
    if (rc != 0)
    {
        std::cerr << "mcap import failed: " << err << "\n";
        return 1;
    }
    PsDataset *ds = static_cast<PsDataset *>(raw);
    assert(ds && ps_dataset_channel_count(ds) == 2);
    const PsChannelInfo *roll = findChannel(ds, "/telemetry/roll");
    const PsChannelInfo *pitch = findChannel(ds, "/telemetry/pitch");
    assert(roll && pitch);
    assert(roll->point_count == 2 && pitch->point_count == 2);
    assert(roll->value[0] == 1.5 && roll->value[1] == 2.5);
    assert(pitch->value[0] == -2.0 && pitch->value[1] == -3.0);
    assert(roll->time[1] - roll->time[0] > 0.09 && roll->time[1] - roll->time[0] < 0.11);
    ps_dataset_free(ds);
    vt->destroy(ctx);

    const auto bad = std::filesystem::temp_directory_path() / "ps_mcap_bad_encoding.mcap";
    writeMcap(bad, "mavlink", "{}", "{}");
    ctx = vt->create();
    assert(ctx);
    assert(vt->bind(ctx, services, &pe));
    raw = nullptr;
    std::memset(err, 0, sizeof(err));
    const int32_t badRc = vt->import_file(ctx, bad.string().c_str(), "{}", &raw, err, sizeof(err));
    assert(badRc != 0);
    assert(std::strstr(err, "不支持的 encoding") != nullptr);
    vt->destroy(ctx);

    std::filesystem::remove(path);
    std::filesystem::remove(bad);
    std::puts("mcap loader ok");
    return 0;
}
