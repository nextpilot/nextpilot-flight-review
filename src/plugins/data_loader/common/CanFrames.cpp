#include "CanFrames.h"

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

        for (size_t b = 0; b < maxLen; ++b)
        {
            bool has = false, varies = false;
            int seen = -1;
            for (size_t i : idx)
            {
                if (b >= frames[i].data.size())
                    continue;
                has = true;
                if (seen < 0)
                    seen = frames[i].data[b];
                else if (seen != static_cast<int>(frames[i].data[b]))
                {
                    varies = true;
                    break;
                }
            }
            if (!has || !varies)
                continue;
            PsChannelData c;
            char name[64];
            std::snprintf(name, sizeof(name), "CAN%u.%s.b%u", static_cast<unsigned>(bus), id.c_str(),
                          static_cast<unsigned>(b));
            c.name = name;
            for (size_t i : idx)
            {
                if (b >= frames[i].data.size())
                    continue;
                c.t.push_back(frames[i].t - t0);
                c.v.push_back(static_cast<double>(frames[i].data[b]));
            }
            out.push_back(std::move(c));
        }

        if (idx.size() >= 3)
        {
            PsChannelData c;
            char name[64];
            std::snprintf(name, sizeof(name), "CAN%u.%s.period", static_cast<unsigned>(bus), id.c_str());
            c.name = name;
            for (size_t i = 1; i < idx.size(); ++i)
                c.t.push_back(frames[idx[i]].t - t0);
            for (size_t i = 1; i < idx.size(); ++i)
                c.v.push_back((frames[idx[i]].t - frames[idx[i - 1]].t) * 1000.0);
            out.push_back(std::move(c));
        }
    }
    return out;
}
