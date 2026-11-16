#include "MavlinkChannelCollector.h"

#include <QString>

#include <cmath>

void MavlinkChannelCollector::addFrame(const PsDecodedFrame &frame, double time)
{
    if (!frame.checksumOk || !std::isfinite(time))
        return;

    const QString prefix = QStringLiteral("mav.") + frame.frameName.toLower() + QLatin1Char('.');
    for (const PsDecodedField &field : frame.fields)
    {
        // offset < 0 是 magic/sysid/msgid 等帧头字段，不进绘制通道。
        if (field.offset < 0 || field.name.isEmpty())
            continue;
        bool ok = false;
        const double value = field.value.toDouble(&ok);
        if (!ok)
            continue;

        const std::string name = (prefix + field.name).toStdString();
        auto it = m_index.find(name);
        if (it == m_index.end())
        {
            it = m_index.emplace(name, m_channels.size()).first;
            MavlinkChannelData channel;
            channel.name = name;
            channel.unit = field.unit.toStdString();
            m_channels.push_back(std::move(channel));
        }
        MavlinkChannelData &channel = m_channels[it->second];
        if (channel.unit.empty())
            channel.unit = field.unit.toStdString();
        channel.t.push_back(time);
        channel.v.push_back(value);
    }
}

void MavlinkChannelCollector::flush(PsSourceSink *sink)
{
    if (!sink || !sink->channel_batch)
        return;
    for (MavlinkChannelData &channel : m_channels)
    {
        if (channel.t.empty() || channel.v.empty())
            continue;
        const uint32_t count = static_cast<uint32_t>(
            std::min(channel.t.size(), channel.v.size()));
        sink->channel_batch(sink->ctx, channel.name.c_str(), channel.unit.c_str(),
                            channel.t.data(), channel.v.data(), count);
        channel.t.clear();
        channel.v.clear();
    }
}

bool MavlinkChannelCollector::hasData() const
{
    for (const MavlinkChannelData &channel : m_channels)
        if (!channel.t.empty())
            return true;
    return false;
}
