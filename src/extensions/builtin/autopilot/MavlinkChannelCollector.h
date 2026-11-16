#pragma once

#include "datasource/DecodedFrame.h"
#include "ps_extension_api.h"

#include <map>
#include <string>
#include <vector>

struct MavlinkChannelData
{
    std::string name;
    std::string unit;
    std::vector<double> t;
    std::vector<double> v;
};

// 把任意 MAVLink 帧的 payload 字段展平成通道，不再按消息名写白名单。
class MavlinkChannelCollector
{
public:
    void addFrame(const PsDecodedFrame &frame, double time);
    void flush(PsSourceSink *sink);
    const std::vector<MavlinkChannelData> &channels() const { return m_channels; }
    bool hasData() const;

private:
    std::map<std::string, size_t> m_index;
    std::vector<MavlinkChannelData> m_channels;
};
