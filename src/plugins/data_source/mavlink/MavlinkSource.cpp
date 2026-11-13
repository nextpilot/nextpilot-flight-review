#include "ps_plugin_api.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QUdpSocket>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace {

constexpr uint8_t kMavlinkV1 = 0xFE;
constexpr uint8_t kMavlinkV2 = 0xFD;

uint32_t u32(const uint8_t *p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
int16_t i16(const uint8_t *p)
{
    return static_cast<int16_t>(uint16_t(p[0]) | (uint16_t(p[1]) << 8));
}
uint16_t u16(const uint8_t *p)
{
    return static_cast<uint16_t>(uint16_t(p[0]) | (uint16_t(p[1]) << 8));
}
int32_t i32(const uint8_t *p)
{
    return static_cast<int32_t>(u32(p));
}
float f32(const uint8_t *p)
{
    float v = 0.0f;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

int32_t writeJson(const char *json, char *out, uint32_t cap)
{
    if (!out || cap == 0 || !json)
        return -1;
    const size_t len = std::strlen(json);
    if (len + 1 > cap)
        return -1;
    std::memcpy(out, json, len + 1);
    return static_cast<int32_t>(len);
}

const char *kSchema = R"({
  "title":"MAVLink 遥测源",
  "fields":[
    {"key":"port","type":"int","label":"UDP 端口","default":14550,"min":1,"max":65535},
    {"key":"bind","type":"string","label":"绑定地址","default":"0.0.0.0"},
    {"key":"sysid","type":"int","label":"只接收系统 ID（0=全部）","default":0,"min":0,"max":255}
  ]
})";
const char *kDefaults = R"({"port":14550,"bind":"0.0.0.0","sysid":0})";

struct MavState
{
    PsSourceSink sink{};
    std::unique_ptr<QUdpSocket> socket;
    QElapsedTimer clock;
    QByteArray rx;
    QHash<QString, QVector<double>> times;
    QHash<QString, QVector<double>> values;
    QHash<QString, QString> units;
    QStringList order;
    uint64_t packets = 0;
    uint64_t frames = 0;
    uint64_t dropped = 0;
    int sysid = 0;
    int lastSysid = 0;
};

void addSample(MavState *st, const QString &name, const QString &unit, double t, double v)
{
    if (!st->times.contains(name))
        st->order.append(name);
    st->times[name].append(t);
    st->values[name].append(v);
    if (!st->units.contains(name))
        st->units.insert(name, unit);
}

void flush(MavState *st)
{
    if (st->times.isEmpty())
        return;
    for (const QString &name : st->order)
    {
        const QVector<double> &t = st->times.value(name);
        const QVector<double> &v = st->values.value(name);
        const uint32_t n = static_cast<uint32_t>(qMin(t.size(), v.size()));
        if (n == 0)
            continue;
        st->sink.channel_batch(st->sink.ctx, name.toUtf8().constData(),
                               st->units.value(name).toUtf8().constData(),
                               t.constData(), v.constData(), n);
    }
    st->order.clear();
    st->times.clear();
    st->values.clear();
}

void decodePayload(MavState *st, uint8_t msgid, const uint8_t *p, int len, int sysid)
{
    if (st->sysid && sysid != st->sysid)
        return;
    const double fallback = st->clock.elapsed() / 1000.0;
    switch (msgid)
    {
    case 30: // ATTITUDE
        if (len >= 16)
        {
            const double t = u32(p) / 1000.0;
            addSample(st, QStringLiteral("mav.att.roll"), QStringLiteral("deg"), t, i16(p + 4) * 1e-4 * 57.2957795);
            addSample(st, QStringLiteral("mav.att.pitch"), QStringLiteral("deg"), t, i16(p + 6) * 1e-4 * 57.2957795);
            addSample(st, QStringLiteral("mav.att.yaw"), QStringLiteral("deg"), t, i16(p + 8) * 1e-4 * 57.2957795);
            addSample(st, QStringLiteral("mav.att.roll_rate"), QStringLiteral("deg/s"), t, i16(p + 10) * 1e-3 * 57.2957795);
            addSample(st, QStringLiteral("mav.att.pitch_rate"), QStringLiteral("deg/s"), t, i16(p + 12) * 1e-3 * 57.2957795);
            addSample(st, QStringLiteral("mav.att.yaw_rate"), QStringLiteral("deg/s"), t, i16(p + 14) * 1e-3 * 57.2957795);
        }
        break;
    case 74: // VFR_HUD
        if (len >= 20)
        {
            addSample(st, QStringLiteral("mav.vfr.airspeed"), QStringLiteral("m/s"), fallback, f32(p));
            addSample(st, QStringLiteral("mav.vfr.groundspeed"), QStringLiteral("m/s"), fallback, f32(p + 4));
            addSample(st, QStringLiteral("mav.vfr.heading"), QStringLiteral("deg"), fallback, i16(p + 8));
            addSample(st, QStringLiteral("mav.vfr.throttle"), QStringLiteral("%"), fallback, u16(p + 10));
            addSample(st, QStringLiteral("mav.vfr.alt"), QStringLiteral("m"), fallback, f32(p + 12));
            addSample(st, QStringLiteral("mav.vfr.climb"), QStringLiteral("m/s"), fallback, f32(p + 16));
        }
        break;
    case 33: // GLOBAL_POSITION_INT
        if (len >= 28)
        {
            const double t = u32(p) / 1000.0;
            addSample(st, QStringLiteral("mav.gps.lat"), QStringLiteral("deg"), t, i32(p + 4) * 1e-7);
            addSample(st, QStringLiteral("mav.gps.lon"), QStringLiteral("deg"), t, i32(p + 8) * 1e-7);
            addSample(st, QStringLiteral("mav.gps.alt"), QStringLiteral("m"), t, i32(p + 12) * 1e-3);
            addSample(st, QStringLiteral("mav.gps.rel_alt"), QStringLiteral("m"), t, i32(p + 16) * 1e-3);
            addSample(st, QStringLiteral("mav.gps.vx"), QStringLiteral("m/s"), t, i16(p + 20) * 1e-2);
            addSample(st, QStringLiteral("mav.gps.vy"), QStringLiteral("m/s"), t, i16(p + 22) * 1e-2);
            addSample(st, QStringLiteral("mav.gps.vz"), QStringLiteral("m/s"), t, i16(p + 24) * 1e-2);
            addSample(st, QStringLiteral("mav.gps.hdg"), QStringLiteral("deg"), t, u16(p + 26) * 1e-2);
        }
        break;
    case 0: // HEARTBEAT
        if (len >= 9)
            st->lastSysid = sysid;
        break;
    default:
        break;
    }
}

void parse(MavState *st)
{
    while (st->rx.size() >= 8)
    {
        int at = -1;
        for (int i = 0; i < st->rx.size(); ++i)
        {
            const uint8_t c = static_cast<uint8_t>(st->rx.at(i));
            if (c == kMavlinkV1 || c == kMavlinkV2)
            {
                at = i;
                break;
            }
        }
        if (at < 0)
        {
            st->rx.clear();
            return;
        }
        if (at > 0)
            st->rx.remove(0, at);

        const uint8_t magic = static_cast<uint8_t>(st->rx.at(0));
        const int len = static_cast<uint8_t>(st->rx.size() > 1 ? st->rx.at(1) : 0);
        if (magic == kMavlinkV1)
        {
            const int total = 8 + len;
            if (st->rx.size() < total)
                return;
            const uint8_t msgid = static_cast<uint8_t>(st->rx.at(5));
            const int sysid = static_cast<uint8_t>(st->rx.at(3));
            decodePayload(st, msgid, reinterpret_cast<const uint8_t *>(st->rx.constData()) + 6, len, sysid);
            st->rx.remove(0, total);
            ++st->frames;
        }
        else
        {
            if (st->rx.size() < 12)
                return;
            const uint8_t incompat = static_cast<uint8_t>(st->rx.at(2));
            const int total = 12 + len + ((incompat & 0x01) ? 13 : 0);
            if (st->rx.size() < total)
                return;
            const uint32_t msgid = uint32_t(uint8_t(st->rx.at(7))) |
                                   (uint32_t(uint8_t(st->rx.at(8))) << 8) |
                                   (uint32_t(uint8_t(st->rx.at(9))) << 16);
            const int sysid = static_cast<uint8_t>(st->rx.at(5));
            decodePayload(st, static_cast<uint8_t>(msgid & 0xFF),
                          reinterpret_cast<const uint8_t *>(st->rx.constData()) + 10, len, sysid);
            st->rx.remove(0, total);
            ++st->frames;
        }
        if (st->rx.size() > 4 * 1024 * 1024)
        {
            st->rx.clear();
            ++st->dropped;
            return;
        }
    }
}

int32_t openSource(const char *config, const PsSourceSink *sink, void **handle,
                   char *err, uint32_t errCap)
{
    if (!handle || !sink || !sink->channel_batch)
    {
        if (err && errCap)
            std::snprintf(err, errCap, "sink 不完整");
        return 1;
    }
    QJsonObject cfg;
    if (config && config[0])
    {
        const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(config));
        if (!doc.isObject())
        {
            if (err && errCap)
                std::snprintf(err, errCap, "config 不是 JSON 对象");
            return 2;
        }
        cfg = doc.object();
    }
    auto *st = new MavState;
    st->sink = *sink;
    st->sysid = qBound(0, cfg.value(QStringLiteral("sysid")).toInt(0), 255);
    st->socket = std::make_unique<QUdpSocket>();
    const quint16 port = static_cast<quint16>(qBound(1, cfg.value(QStringLiteral("port")).toInt(14550), 65535));
    const QString bind = cfg.value(QStringLiteral("bind")).toString(QStringLiteral("0.0.0.0"));
    if (!st->socket->bind(QHostAddress(bind), port,
                          QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))
    {
        if (err && errCap)
            std::snprintf(err, errCap, "绑定 UDP %s:%u 失败：%s", bind.toUtf8().constData(),
                          static_cast<unsigned>(port), st->socket->errorString().toUtf8().constData());
        delete st;
        return 3;
    }
    st->socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 4 * 1024 * 1024);
    st->clock.start();
    *handle = st;
    return 0;
}

int32_t pollSource(void *handle, int timeoutMs, char *err, uint32_t errCap)
{
    Q_UNUSED(timeoutMs);
    Q_UNUSED(err);
    Q_UNUSED(errCap);
    auto *st = static_cast<MavState *>(handle);
    if (!st)
        return 1;
    while (st->socket && st->socket->hasPendingDatagrams())
    {
        QByteArray dgram;
        dgram.resize(static_cast<int>(st->socket->pendingDatagramSize()));
        st->socket->readDatagram(dgram.data(), dgram.size());
        st->rx.append(dgram);
        ++st->packets;
        parse(st);
        if (st->rx.size() > 4 * 1024 * 1024)
        {
            st->rx.clear();
            ++st->dropped;
        }
    }
    flush(st);
    return 0;
}

int32_t closeSource(void *handle)
{
    delete static_cast<MavState *>(handle);
    return 0;
}

int32_t statusSource(void *handle, char *out, uint32_t cap)
{
    auto *st = static_cast<MavState *>(handle);
    if (!st)
        return writeJson("{\"state\":\"closed\"}", out, cap);
    static thread_local char buf[512];
    std::snprintf(buf, sizeof(buf),
                  R"({"state":"listening","packets":%llu,"frames":%llu,"dropped":%llu,"last_sysid":%d})",
                  static_cast<unsigned long long>(st->packets),
                  static_cast<unsigned long long>(st->frames),
                  static_cast<unsigned long long>(st->dropped), st->lastSysid);
    return writeJson(buf, out, cap);
}

int32_t schema(char *out, uint32_t cap) { return writeJson(kSchema, out, cap); }
int32_t defaults(char *out, uint32_t cap) { return writeJson(kDefaults, out, cap); }

} // namespace

extern "C" const PsPluginDescriptor *ps_plugin_entry_data_source_mavlink()
{
    static const PsDataSourceVtable vtable = {
        PS_PLUGIN_ABI_VERSION,
        "data_source_mavlink",
        "MAVLink",
        "telemetry",
        "udp",
        1,
        {0, 0, 0, 0, 0, 0, 0},
        schema,
        defaults,
        openSource,
        pollSource,
        closeSource,
        statusSource,
    };
    static const PsPluginDescriptor desc = {
        PS_PLUGIN_ABI_VERSION,
        R"({"id":"data_source_mavlink","name":"MAVLink 遥测","version":"1.0.0","family":"data_source"})",
        PS_FAMILY_DATA_SOURCE,
        PS_CAP_STREAM | PS_CAP_RECORD | PS_CAP_SCHEMA,
        nullptr,
        nullptr,
        &vtable,
    };
    return &desc;
}
