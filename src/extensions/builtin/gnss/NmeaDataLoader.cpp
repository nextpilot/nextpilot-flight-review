#include "ps_extension_api.h"
#include "LegacyFileInput.hpp"
#include "PsLoaderCommon.h"

#include <QString>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
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

double dmToDeg(const std::string &dm, const std::string &hemi, bool &ok)
{
    ok = false;
    if (dm.empty() || hemi.empty())
        return 0;
    double raw = std::atof(dm.c_str());
    const int deg = static_cast<int>(raw / 100.0);
    const double min = raw - deg * 100.0;
    double v = deg + min / 60.0;
    if (hemi[0] == 'S' || hemi[0] == 'W')
        v = -v;
    ok = true;
    return v;
}

double utcSeconds(const std::string &hhmmss)
{
    if (hhmmss.size() < 6)
        return 0;
    return std::atof(hhmmss.substr(0, 2).c_str()) * 3600.0 +
           std::atof(hhmmss.substr(2, 2).c_str()) * 60.0 + std::atof(hhmmss.substr(4).c_str());
}

std::vector<std::string> split(const std::string &s)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
    {
        if (c == ',')
        {
            out.push_back(cur);
            cur.clear();
        }
        else
            cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

bool parseFile(const std::string &path, std::vector<Channel> &channels, std::string &err)
{
    std::ifstream in(path);
    if (!in)
    {
        err = "无法打开 NMEA 文件：" + path;
        return false;
    }
    std::map<std::string, Channel> acc;
    double lastT = 0, dayOffset = 0;
    bool first = true;
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.size() < 7 || line[0] != '$')
            continue;
        const size_t star = line.find('*');
        if (star != std::string::npos)
            line = line.substr(0, star);
        auto f = split(line);
        const std::string type = f[0].size() >= 6 ? f[0].substr(3, 3) : "";
        double t = f.size() > 1 ? utcSeconds(f[1]) : 0;
        if (first)
        {
            lastT = t;
            first = false;
        }
        else
        {
            if (t + dayOffset + 43200 < lastT)
                dayOffset += 86400;
            if (t + dayOffset < lastT)
                t += 86400;
            lastT = t + dayOffset;
        }
        const double stamp = t + dayOffset;

        auto add = [&](const std::string &name, const std::string &unit, double v) {
            Channel &c = acc[name];
            c.name = name;
            c.unit = unit;
            c.t.push_back(stamp);
            c.v.push_back(v);
        };

        if (type == "GGA")
        {
            bool okLat = false, okLon = false;
            const double lat = f.size() > 2 ? dmToDeg(f[2], f.size() > 3 ? f[3] : "", okLat) : 0;
            const double lon = f.size() > 4 ? dmToDeg(f[4], f.size() > 5 ? f[5] : "", okLon) : 0;
            if (okLat)
                add("nmea.gga.latitude", "deg", lat);
            if (okLon)
                add("nmea.gga.longitude", "deg", lon);
            if (f.size() > 6 && !f[6].empty())
                add("nmea.gga.fix", "", std::atof(f[6].c_str()));
            if (f.size() > 7 && !f[7].empty())
                add("nmea.gga.sats", "", std::atof(f[7].c_str()));
            if (f.size() > 8 && !f[8].empty())
                add("nmea.gga.hdop", "", std::atof(f[8].c_str()));
            if (f.size() > 9 && !f[9].empty())
                add("nmea.gga.altitude", "m", std::atof(f[9].c_str()));
        }
        else if (type == "RMC")
        {
            bool okLat = false, okLon = false;
            const double lat = f.size() > 3 ? dmToDeg(f[3], f.size() > 4 ? f[4] : "", okLat) : 0;
            const double lon = f.size() > 5 ? dmToDeg(f[5], f.size() > 6 ? f[6] : "", okLon) : 0;
            if (okLat)
                add("nmea.rmc.latitude", "deg", lat);
            if (okLon)
                add("nmea.rmc.longitude", "deg", lon);
            if (f.size() > 7 && !f[7].empty())
                add("nmea.rmc.speed_kn", "kn", std::atof(f[7].c_str()));
            if (f.size() > 8 && !f[8].empty())
                add("nmea.rmc.course", "deg", std::atof(f[8].c_str()));
        }
    }
    channels.clear();
    for (auto &kv : acc)
        channels.push_back(std::move(kv.second));
    if (channels.empty())
    {
        err = "NMEA 里没有 GGA/RMC 有效语句";
        return false;
    }
    return true;
}

int32_t nmeaLoad(const char *path, const char *, const PsHostApi *host, void **dataset, char *err,
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
    bool gotT0 = false;
    for (const auto &c : channels)
        if (!c.t.empty())
        {
            t0 = gotT0 ? std::min(t0, c.t.front()) : c.t.front();
            gotT0 = true;
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

const PsLoaderVtable kNmeaLoader = {
    PS_EXTENSION_ABI_VERSION,
    "data_loader_nmea",
    "NMEA · GNSS",
    "NMEA (*.nmea *.txt);;全部 (*)",
    ".nmea,.txt",
    "NMEA · GNSS",
    0,
    0,
    {0, 0, 0, 0, 0, 0},
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nmeaLoad,
};

const PsDataSourceVtable *nmeaInputVtable()
{
    static const PsDataSourceVtable vt = ps::VtableBuilder::make<LegacyFileInput<&kNmeaLoader>>(R"({"id":"data_loader_nmea","name":"NMEA","version":"1.0.0","sdk":"1.0","kind":"file"})");
    return &vt;
}

const PsExtensionDescriptor kNmeaExtension = {
    PS_EXTENSION_ABI_VERSION,
    R"({"id":"data_loader_nmea","name":"NMEA","version":"1.0.0","sdk":"1.0","family":"data_source","kind":"file","entry":"ps_extension_entry_data_loader_nmea"})",
    PS_FAMILY_DATA_SOURCE,
    PS_CAP_FILE | PS_CAP_CACHE,
    &kNmeaLoader,
    nullptr,
    nullptr,
    nmeaInputVtable(),
};

} // namespace

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_nmea(void)
{
    return &kNmeaExtension;
}
