#include "CanFrames.h"

#include <cmath>
#include <limits>
#include <memory>

#include <cstdio>
#include <map>
#include <tuple>

std::string psCanIdText(const CanFrame &f)
{
    char buf[16];
    if (f.ext)
        std::snprintf(buf, sizeof(buf), "%08X", static_cast<unsigned>(f.id));
    else
        std::snprintf(buf, sizeof(buf), "%03X", static_cast<unsigned>(f.id));
    return buf;
}

std::vector<PsChannelData> psCanFramesToChannels(const std::vector<CanFrame> &frames)
{
    std::vector<PsChannelData> out;
    if (frames.empty())
        return out;
    const double t0 = frames[0].t;
    std::map<std::pair<uint8_t, std::string>, std::vector<size_t>> groups;
    for (size_t i = 0; i < frames.size(); ++i)
        groups[{frames[i].bus, psCanIdText(frames[i])}].push_back(i);

    for (const auto &kv : groups)
    {
        const uint8_t bus = kv.first.first;
        const std::string &id = kv.first.second;
        const std::vector<size_t> &idx = kv.second;
        size_t maxLen = 0;
        for (size_t i : idx)
            maxLen = std::max(maxLen, frames[i].data.size());

        std::map<size_t, bool> varies;
        for (size_t b = 0; b < maxLen; ++b)
        {
            int seen = -1;
            bool has = false;
            bool changed = false;
            for (size_t i : idx)
            {
                if (b >= frames[i].data.size())
                    continue;
                has = true;
                if (seen < 0)
                    seen = frames[i].data[b];
                else if (seen != static_cast<int>(frames[i].data[b]))
                {
                    changed = true;
                    break;
                }
            }
            if (has && changed)
                varies[b] = true;
        }
        if (varies.empty())
            continue;

        std::map<size_t, std::vector<double>> cols;
        std::vector<double> time;
        time.reserve(idx.size());
        for (size_t i : idx)
        {
            time.push_back(frames[i].t - t0);
            for (const auto &vb : varies)
            {
                auto &col = cols[vb.first];
                col.push_back(vb.first < frames[i].data.size()
                                  ? static_cast<double>(frames[i].data[vb.first])
                                  : std::numeric_limits<double>::quiet_NaN());
            }
        }
        if (idx.size() >= 2)
        {
            auto &period = cols[static_cast<size_t>(-1)];
            auto &dlc = cols[static_cast<size_t>(-2)];
            period.push_back(std::numeric_limits<double>::quiet_NaN());
            dlc.push_back(static_cast<double>(frames[idx[0]].data.size()));
            for (size_t i = 1; i < idx.size(); ++i)
            {
                period.push_back((frames[idx[i]].t - frames[idx[i - 1]].t) * 1000.0);
                dlc.push_back(static_cast<double>(frames[idx[i]].data.size()));
            }
        }
        auto axis = std::make_shared<const std::vector<double>>(time);
        for (const auto &cb : cols)
        {
            PsChannelData c;
            char tableName[64];
            char name[80];
            std::snprintf(tableName, sizeof(tableName), "CAN%u.%s",
                          static_cast<unsigned>(bus), id.c_str());
            if (cb.first == static_cast<size_t>(-1))
                std::snprintf(name, sizeof(name), "%s.period", tableName);
            else if (cb.first == static_cast<size_t>(-2))
                std::snprintf(name, sizeof(name), "%s.dlc", tableName);
            else
                std::snprintf(name, sizeof(name), "%s.b%u", tableName,
                              static_cast<unsigned>(cb.first));
            c.name = name;
            c.table = tableName;
            c.axis = axis;
            c.v = cb.second;
            c.value_type = PS_VALUE_U8;
            out.push_back(std::move(c));
        }
    }
    return out;
}
