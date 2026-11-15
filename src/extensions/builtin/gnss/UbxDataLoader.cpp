#include "ps_extension_api.h"
#include "LegacyFileInput.hpp"
#include "PsLoaderCommon.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

struct Channel
{
    std::string name;
    std::string unit;
    std::vector<double> t;
    std::vector<double> v;
};

uint16_t u2(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t u4(const uint8_t *p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
int32_t i4(const uint8_t *p) { return static_cast<int32_t>(u4(p)); }

void add(std::map<std::string, Channel> &acc, const std::string &name, const std::string &unit,
         double t, double v)
{
    Channel &c = acc[name];
    c.name = name;
    c.unit = unit;
    c.t.push_back(t);
    c.v.push_back(v);
}

void parsePvt(const uint8_t *p, uint32_t len, std::map<std::string, Channel> &acc)
{
    if (len < 72)
        return;
    const double t = u4(p) / 1000.0;
    add(acc, "ubx.nav_pvt.fix_type", "", t, p[26]);
    add(acc, "ubx.nav_pvt.num_sv", "", t, p[29]);
    add(acc, "ubx.nav_pvt.latitude", "deg", t, i4(p + 34) * 1e-7);
    add(acc, "ubx.nav_pvt.longitude", "deg", t, i4(p + 30) * 1e-7);
    add(acc, "ubx.nav_pvt.altitude", "m", t, i4(p + 42) / 1000.0);
    add(acc, "ubx.nav_pvt.vel_n", "m/s", t, i4(p + 54) / 100.0);
    add(acc, "ubx.nav_pvt.vel_e", "m/s", t, i4(p + 58) / 100.0);
    add(acc, "ubx.nav_pvt.vel_d", "m/s", t, i4(p + 62) / 100.0);
    add(acc, "ubx.nav_pvt.heading", "deg", t, i4(p + 70) * 1e-5);
}

void parsePosllh(const uint8_t *p, uint32_t len, std::map<std::string, Channel> &acc)
{
    if (len < 28)
        return;
    const double t = u4(p) / 1000.0;
    add(acc, "ubx.nav_posllh.latitude", "deg", t, i4(p + 8) * 1e-7);
    add(acc, "ubx.nav_posllh.longitude", "deg", t, i4(p + 4) * 1e-7);
    add(acc, "ubx.nav_posllh.altitude", "m", t, i4(p + 16) / 1000.0);
    add(acc, "ubx.nav_posllh.h_acc", "m", t, u4(p + 20) / 1000.0);
    add(acc, "ubx.nav_posllh.v_acc", "m", t, u4(p + 24) / 1000.0);
}

bool parseFile(const std::string &path, std::vector<Channel> &channels, std::string &err)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        err = "无法打开 UBX 文件：" + path;
        return false;
    }
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::map<std::string, Channel> acc;
    size_t pos = 0;
    while (pos + 8 <= buf.size())
    {
        if (buf[pos] != 0xB5 || buf[pos + 1] != 0x62)
        {
            ++pos;
            continue;
        }
        const uint8_t cls = buf[pos + 2], id = buf[pos + 3];
        const uint32_t len = u2(buf.data() + pos + 4);
        if (pos + 8 + len > buf.size())
            break;
        const uint8_t *payload = buf.data() + pos + 6;
        if (cls == 0x01 && id == 0x07)
            parsePvt(payload, len, acc);
        else if (cls == 0x01 && id == 0x02)
            parsePosllh(payload, len, acc);
        pos += 8 + len;
    }
    channels.clear();
    for (auto &kv : acc)
        channels.push_back(std::move(kv.second));
    if (channels.empty())
    {
        err = "UBX 里没有 NAV-PVT / NAV-POSLLH";
        return false;
    }
    return true;
}

int32_t ubxLoad(const char *path, const char *, const PsHostApi *host, void **dataset, char *err,
                uint32_t errCap)
{
    std::vector<Channel> channels;
    std::string perr;
    if (!parseFile(path, channels, perr))
    {
        psWriteErr(err, errCap, perr);
        return 1;
    }
    double t0 = 0;
    bool got = false;
    for (const auto &c : channels)
        if (!c.t.empty())
        {
            t0 = got ? std::min(t0, c.t.front()) : c.t.front();
            got = true;
        }
    std::vector<PsChannelData> data;
    data.reserve(channels.size());
    for (auto &c : channels)
    {
        PsChannelData pc;
        pc.name = c.name;
        pc.unit = c.unit;
        pc.t = std::move(c.t);
        pc.v = std::move(c.v);
        for (double &t : pc.t)
            t -= t0;
        data.push_back(std::move(pc));
    }
    PsDataset *ds = psBuildDataset(data, perr, host);
    if (!ds)
    {
        psWriteErr(err, errCap, perr);
        return 2;
    }
    *dataset = ds;
    return 0;
}

const PsLoaderVtable kUbxLoader = {
    PS_EXTENSION_ABI_VERSION,
    "data_loader_ubx",
    "UBX · GNSS",
    "u-blox UBX (*.ubx);;全部 (*)",
    ".ubx",
    "UBX · GNSS",
    0,
    0,
    {0, 0, 0, 0, 0, 0},
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    ubxLoad,
};

const PsDataSourceVtable *ubxInputVtable()
{
    static const PsDataSourceVtable vt = ps::VtableBuilder::make<LegacyFileInput<&kUbxLoader>>(R"({"id":"data_loader_ubx","name":"UBX","version":"1.0.0","sdk":"1.0","kind":"file"})");
    return &vt;
}

const PsExtensionDescriptor kUbxExtension = {
    PS_EXTENSION_ABI_VERSION,
    R"({"id":"data_loader_ubx","name":"UBX","version":"1.0.0","sdk":"1.0","family":"data_source","kind":"file","entry":"ps_extension_entry_data_loader_ubx"})",
    PS_FAMILY_DATA_SOURCE,
    PS_CAP_FILE | PS_CAP_CACHE,
    &kUbxLoader,
    nullptr,
    nullptr,
    ubxInputVtable(),
};

} // namespace

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_ubx(void)
{
    return &kUbxExtension;
}
