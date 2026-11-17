#include "ps_extension_api.h"
#include "ps_kernel.h"
#include "HostApi.h"

#ifndef MAVLINK_STX
#define MAVLINK_STX 253
#endif

#include "message.hpp"
#include "common/common.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QByteArray>
#include <QString>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_tlog(void);

static void appendTimestamp(QByteArray &bytes, quint64 value)
{
    for (int i = 0; i < 8; ++i)
        bytes.append(char((value >> (8 * i)) & 0xff));
}

template <typename Msg>
static QByteArray serialize(const Msg &msg)
{
    mavlink::mavlink_message_t message{};
    mavlink::MsgMap map(message);
    Msg copy = msg;
    copy.serialize(map);
    mavlink::mavlink_finalize_message(&message, 1, 1, Msg::MIN_LENGTH, Msg::LENGTH,
                                     Msg::CRC_EXTRA);
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    const uint16_t length = mavlink::mavlink_msg_to_send_buffer(buffer, &message);
    return QByteArray(reinterpret_cast<const char *>(buffer), length);
}

static bool hasChannel(PsDataset *ds, const char *name, double time, double value,
                       const char *unit = nullptr)
{
    for (uint32_t i = 0; i < ps_dataset_channel_count(ds); ++i)
    {
        const PsChannelInfo *channel = ps_dataset_channel(ds, i);
        if (!channel || !channel->name || std::strcmp(channel->name, name) != 0)
            continue;
        if (unit && (!channel->unit || std::strcmp(channel->unit, unit) != 0))
            return false;
        for (uint32_t j = 0; j < channel->point_count; ++j)
        {
            if (std::fabs(channel->time[j] - time) < 1e-6 &&
                std::fabs(ps_channel_value_at(channel, j) - value) < 1e-5)
                return true;
        }
        return false;
    }
    return false;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const PsExtensionDescriptor *d = ps_extension_entry_data_loader_tlog();
    assert(d && d->loader);
    assert(d->loader->extensions && std::strstr(d->loader->extensions, ".tlog"));
    assert(d->loader->filter && std::strstr(d->loader->filter, "*.tlog"));

    QTemporaryDir dir;
    assert(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("demo.tlog"));

    mavlink::common::msg::ATTITUDE attitude{};
    attitude.time_boot_ms = 0;
    attitude.roll = 0.1f;
    attitude.pitch = -0.05f;
    attitude.yaw = 0.0f;

    mavlink::common::msg::VFR_HUD hud{};
    hud.airspeed = 12.5f;
    hud.groundspeed = 13.0f;
    hud.heading = 90;
    hud.throttle = 40;
    hud.alt = 100.0f;
    hud.climb = 1.2f;

    QByteArray tlog;
    appendTimestamp(tlog, 1000000);
    tlog.append(serialize(attitude));
    appendTimestamp(tlog, 2000000);
    tlog.append(serialize(hud));

    QFile file(path);
    assert(file.open(QIODevice::WriteOnly));
    assert(file.write(tlog) == tlog.size());
    file.close();

    void *raw = nullptr;
    char err[512] = {0};
    const int32_t rc = d->loader->load(path.toUtf8().constData(), "{}", ps_host_api(), &raw,
                                       err, sizeof(err));
    if (rc != 0)
    {
        std::fprintf(stderr, "tlog load failed: %s\n", err);
        return 1;
    }
    PsDataset *ds = static_cast<PsDataset *>(raw);
    const bool ok = hasChannel(ds, "mav.attitude.roll", 0.0, 5.729578, "deg") &&
                    hasChannel(ds, "mav.vfr_hud.airspeed", 1.0, 12.5, "m/s");
    ps_dataset_free(ds);
    assert(ok);
    return 0;
}
