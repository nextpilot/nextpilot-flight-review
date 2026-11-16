#include "ps_extension_api.h"
#include "PsDataSourceBase.hpp"
#include "PsCaptureFormat.h"
#include "MavlinkMessageParser.h"

#include <QByteArray>
#include <QFile>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QUdpSocket>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace {

constexpr uint8_t kMavlinkV1 = 0xFE;
constexpr uint8_t kMavlinkV2 = 0xFD;

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

const char *kSchema = R"JSON({
  "title":"MAVLink 遥测源",
  "transport":{"supported":["udp","file"],"default":"udp://127.0.0.1:1223"},
  "parser":{
    "label":"解析参数",
    "fields":[
      {"key":"sysid","type":"int","label":"只接收系统 ID（0=全部）","default":0,"min":0,"max":255}
    ]
  }
})JSON";
const char *kDefaults = R"({"transport":"udp","port":14550,"bind":"0.0.0.0","sysid":0,"raw_path":"","filepath":""})";

struct MavState
{
    PsSourceSink sink{};
    ps::capture::RawWriter rawWriter;
    ps::capture::RawReader rawReader;
    QFile legacyReplayFile;
    bool legacyReplay = false;
    bool replayMode = false;
    bool replayFinished = false;
    bool rawDropped = false;
    bool replayStarted = false;
    bool external = false;
    std::string rawError;
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
    double replay_t = 0.0;
    int64_t replayBaseNs = 0;
};

int64_t nowNs()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
}

double nowSeconds(const MavState *st)
{
    return st->replayMode ? st->replay_t : st->clock.elapsed() / 1000.0;
}

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

constexpr double kRadToDeg = 57.29577951308232;

PsDecodedField findField(const PsDecodedFrame &frame, const QString &name)
{
    for (const PsDecodedField &field : frame.fields) {
        if (field.name == name)
            return field;
    }
    return {};
}

double findNumber(const PsDecodedFrame &frame, const QString &name, double fallback = 0.0)
{
    const PsDecodedField field = findField(frame, name);
    bool ok = false;
    const double value = field.value.toDouble(&ok);
    return ok ? value : fallback;
}

double findDegrees(const PsDecodedFrame &frame, const QString &name)
{
    const PsDecodedField field = findField(frame, name);
    bool ok = false;
    double value = field.value.toDouble(&ok);
    if (!ok)
        return 0.0;
    if (field.unit == QLatin1String("rad") || field.unit == QLatin1String("rad/s"))
        value *= kRadToDeg;
    return value;
}

double messageTime(const PsDecodedFrame &frame, double fallback)
{
    for (const QString &name : {QStringLiteral("time_usec"), QStringLiteral("time_boot_ms"),
                                QStringLiteral("time_ms"), QStringLiteral("time")}) {
        const double value = findNumber(frame, name, -1.0);
        if (value >= 0.0)
            return value;
    }
    return fallback;
}

void addDecodedSamples(MavState *st, const PsDecodedFrame &frame, double fallback)
{
    const int sysid = static_cast<int>(findNumber(frame, QStringLiteral("sysid")));
    if (st->sysid && sysid != st->sysid)
        return;

    if (frame.frameName == QLatin1String("HEARTBEAT")) {
        st->lastSysid = sysid;
        return;
    }

    const double t = messageTime(frame, fallback);
    if (frame.frameName == QLatin1String("ATTITUDE")) {
        addSample(st, QStringLiteral("mav.att.roll"), QStringLiteral("deg"), t,
                  findDegrees(frame, QStringLiteral("roll")));
        addSample(st, QStringLiteral("mav.att.pitch"), QStringLiteral("deg"), t,
                  findDegrees(frame, QStringLiteral("pitch")));
        addSample(st, QStringLiteral("mav.att.yaw"), QStringLiteral("deg"), t,
                  findDegrees(frame, QStringLiteral("yaw")));
        addSample(st, QStringLiteral("mav.att.roll_rate"), QStringLiteral("deg/s"), t,
                  findDegrees(frame, QStringLiteral("rollspeed")));
        addSample(st, QStringLiteral("mav.att.pitch_rate"), QStringLiteral("deg/s"), t,
                  findDegrees(frame, QStringLiteral("pitchspeed")));
        addSample(st, QStringLiteral("mav.att.yaw_rate"), QStringLiteral("deg/s"), t,
                  findDegrees(frame, QStringLiteral("yawspeed")));
        return;
    }

    if (frame.frameName == QLatin1String("VFR_HUD")) {
        addSample(st, QStringLiteral("mav.vfr.airspeed"), QStringLiteral("m/s"), fallback,
                  findNumber(frame, QStringLiteral("airspeed")));
        addSample(st, QStringLiteral("mav.vfr.groundspeed"), QStringLiteral("m/s"), fallback,
                  findNumber(frame, QStringLiteral("groundspeed")));
        addSample(st, QStringLiteral("mav.vfr.heading"), QStringLiteral("deg"), fallback,
                  findNumber(frame, QStringLiteral("heading")));
        addSample(st, QStringLiteral("mav.vfr.throttle"), QStringLiteral("%"), fallback,
                  findNumber(frame, QStringLiteral("throttle")));
        addSample(st, QStringLiteral("mav.vfr.alt"), QStringLiteral("m"), fallback,
                  findNumber(frame, QStringLiteral("alt")));
        addSample(st, QStringLiteral("mav.vfr.climb"), QStringLiteral("m/s"), fallback,
                  findNumber(frame, QStringLiteral("climb")));
        return;
    }

    if (frame.frameName == QLatin1String("GLOBAL_POSITION_INT")) {
        addSample(st, QStringLiteral("mav.gps.lat"), QStringLiteral("deg"), t,
                  findNumber(frame, QStringLiteral("lat")));
        addSample(st, QStringLiteral("mav.gps.lon"), QStringLiteral("deg"), t,
                  findNumber(frame, QStringLiteral("lon")));
        addSample(st, QStringLiteral("mav.gps.alt"), QStringLiteral("m"), t,
                  findNumber(frame, QStringLiteral("alt")));
        addSample(st, QStringLiteral("mav.gps.rel_alt"), QStringLiteral("m"), t,
                  findNumber(frame, QStringLiteral("relative_alt")));
        addSample(st, QStringLiteral("mav.gps.vx"), QStringLiteral("m/s"), t,
                  findNumber(frame, QStringLiteral("vx")));
        addSample(st, QStringLiteral("mav.gps.vy"), QStringLiteral("m/s"), t,
                  findNumber(frame, QStringLiteral("vy")));
        addSample(st, QStringLiteral("mav.gps.vz"), QStringLiteral("m/s"), t,
                  findNumber(frame, QStringLiteral("vz")));
        addSample(st, QStringLiteral("mav.gps.hdg"), QStringLiteral("deg"), t,
                  findNumber(frame, QStringLiteral("hdg")));
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
            MavlinkMessageParser parser;
            PsDecodedFrame decoded;
            if (parser.parse(st->rx.left(total), decoded))
                addDecodedSamples(st, decoded, nowSeconds(st));
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
            MavlinkMessageParser parser;
            PsDecodedFrame decoded;
            if (parser.parse(st->rx.left(total), decoded))
                addDecodedSamples(st, decoded, nowSeconds(st));
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

int32_t openSource(const char *config, const PsSourceSink *sink, const PsHostApi *host, void **handle,
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
    const std::string cfgJson = (config && config[0]) ? config : "{}";
    QString replayPath = cfg.value(QStringLiteral("filepath")).toString();
    if (replayPath.isEmpty())
        replayPath = cfg.value(QStringLiteral("__replay_path")).toString();
    if (!replayPath.isEmpty())
    {
        const std::string path = replayPath.toStdString();
        if (ps::capture::isRawFile(path))
        {
            std::string re;
            if (!st->rawReader.open(path, re))
            {
                if (err && errCap)
                    std::snprintf(err, errCap, "打开回放文件失败：%s", re.c_str());
                delete st;
                return 3;
            }
        }
        else
        {
            // 旧版本直接落裸字节；保留兼容，不让历史文件失效。
            st->legacyReplayFile.setFileName(replayPath);
            if (!st->legacyReplayFile.open(QIODevice::ReadOnly))
            {
                if (err && errCap)
                    std::snprintf(err, errCap, "打开回放文件失败：%s", replayPath.toUtf8().constData());
                delete st;
                return 3;
            }
            st->legacyReplay = true;
        }
        st->replayMode = true;
        st->clock.start();
        *handle = st;
        return 0;
    }
    const QString rawPath = cfg.value(QStringLiteral("raw_path")).toString();
    if (!rawPath.isEmpty())
    {
        ps::capture::Header h;
        h.source_id = "data_source_mavlink";
        h.extension_id = "data_source_mavlink";
        h.config_json = cfgJson;
        h.created_at_ns = nowNs();
        std::string re;
        if (!st->rawWriter.open(rawPath.toStdString(), h, re))
        {
            if (err && errCap)
                std::snprintf(err, errCap, "无法写原始流：%s", re.c_str());
            delete st;
            return 4;
        }
    }
    if (cfg.value(QStringLiteral("transport")).toString() == QLatin1String("external"))
    {
        st->external = true;
        st->clock.start();
        *handle = st;
        return 0;
    }
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
    if (st->external)
        return 0;
    if (st->replayMode)
    {
        if (st->replayFinished)
            return 0;
        if (st->legacyReplay)
        {
            const QByteArray chunk = st->legacyReplayFile.read(256 * 1024);
            if (chunk.isEmpty())
            {
                parse(st);
                flush(st);
                st->replayFinished = true;
                return 0;
            }
            st->rx.append(chunk);
            ++st->packets;
            parse(st);
            flush(st);
            return 0;
        }

        size_t budget = 256 * 1024;
        while (budget > 0)
        {
            ps::capture::Record record;
            std::string re;
            if (!st->rawReader.next(record, re))
            {
                if (!re.empty())
                {
                    if (err && errCap)
                        std::snprintf(err, errCap, "读取 raw 失败：%s", re.c_str());
                    return 1;
                }
                parse(st);
                flush(st);
                st->replayFinished = true;
                return 0;
            }
            if (record.encoding != "mavlink")
            {
                if (err && errCap)
                    std::snprintf(err, errCap, "raw 记录 encoding=%s，不是 mavlink",
                                  record.encoding.c_str());
                return 1;
            }
            if (!st->replayStarted)
            {
                st->replayBaseNs = record.timestamp_ns;
                st->replayStarted = true;
            }
            st->replay_t = static_cast<double>(record.timestamp_ns - st->replayBaseNs) / 1e9;
            if (!record.payload.empty())
                st->rx.append(reinterpret_cast<const char *>(record.payload.data()),
                              static_cast<int>(record.payload.size()));
            ++st->packets;
            parse(st);
            flush(st);
            budget = record.payload.size() >= budget ? 0 : budget - record.payload.size();
        }
        return 0;
    }
    while (st->socket && st->socket->hasPendingDatagrams())
    {
        QByteArray dgram;
        dgram.resize(static_cast<int>(st->socket->pendingDatagramSize()));
        st->socket->readDatagram(dgram.data(), dgram.size());
        if (st->sink.raw_batch)
        {
            st->sink.raw_batch(st->sink.ctx, "mavlink", "mavlink", nowNs(),
                               reinterpret_cast<const uint8_t *>(dgram.constData()),
                               static_cast<uint32_t>(dgram.size()));
        }
        else if (st->rawWriter.isOpen() && !st->rawDropped)
        {
            ps::capture::Record record;
            record.timestamp_ns = nowNs();
            record.topic = "mavlink";
            record.encoding = "mavlink";
            record.part_count = 1;
            record.part_lengths = {static_cast<uint32_t>(dgram.size())};
            record.payload.assign(reinterpret_cast<const uint8_t *>(dgram.constData()),
                                  reinterpret_cast<const uint8_t *>(dgram.constData()) + dgram.size());
            std::string re;
            if (!st->rawWriter.write(record, re))
            {
                st->rawDropped = true;
                st->rawError = re;
            }
        }
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
    auto *st = static_cast<MavState *>(handle);
    if (st)
    {
        if (st->rawWriter.isOpen())
        {
            std::string re;
            st->rawWriter.close(re);
        }
        if (st->legacyReplayFile.isOpen())
            st->legacyReplayFile.close();
    }
    delete st;
    return 0;
}

int32_t feedSource(void *handle, const char *topic, const char *encoding, int64_t timestamp_ns,
                   const uint8_t *data, uint32_t size, char *err, uint32_t errCap)
{
    (void)topic; (void)encoding; (void)timestamp_ns;
    auto *st = static_cast<MavState *>(handle);
    if (!st || !st->external)
        return 0;
    if (size && !data)
        return 1;
    if (size)
        st->rx.append(reinterpret_cast<const char *>(data), static_cast<int>(size));
    ++st->packets;
    parse(st);
    flush(st);
    if (st->rx.size() > 4 * 1024 * 1024)
    {
        st->rx.clear();
        ++st->dropped;
        if (err && errCap)
            std::snprintf(err, errCap, "MAVLink rx 溢出");
        return 1;
    }
    return 0;
}

int32_t statusSource(void *handle, char *out, uint32_t cap)
{
    auto *st = static_cast<MavState *>(handle);
    if (!st)
        return writeJson("{\"state\":\"closed\"}", out, cap);
    QJsonObject res;
    res[QStringLiteral("source")] = st->replayMode ? QStringLiteral("file") : QStringLiteral("udp");
    res[QStringLiteral("format")] = st->replayMode
                                        ? (st->legacyReplay ? QStringLiteral("legacy")
                                                            : QStringLiteral("PSRAW1"))
                                        : QStringLiteral("PSRAW1");
    res[QStringLiteral("state")] = st->replayMode
                                       ? (st->replayFinished ? QStringLiteral("replay_done")
                                                             : QStringLiteral("replaying"))
                                       : QStringLiteral("listening");
    res[QStringLiteral("packets")] = static_cast<double>(st->packets);
    res[QStringLiteral("frames")] = static_cast<double>(st->frames);
    res[QStringLiteral("dropped")] = static_cast<double>(st->dropped);
    res[QStringLiteral("last_sysid")] = st->lastSysid;
    if (!st->rawError.empty())
        res[QStringLiteral("raw_error")] = QString::fromStdString(st->rawError);
    const QByteArray json = QJsonDocument(res).toJson(QJsonDocument::Compact);
    return writeJson(json.constData(), out, cap);
}

int32_t schema(char *out, uint32_t cap) { return writeJson(kSchema, out, cap); }
int32_t defaults(char *out, uint32_t cap) { return writeJson(kDefaults, out, cap); }

} // namespace

class MavInput final : public ps::DataSourceBase
{
public:
    uint64_t capabilities() const override
    {
        return PS_INPUT_STREAM | PS_INPUT_FILE | PS_INPUT_DIRECT | PS_INPUT_REPLAY |
               PS_INPUT_RECORD | PS_INPUT_DIALOG;
    }

    bool loadConfig(std::string_view json, PsError *err) override
    {
        config_.assign(json.data() ? json.data() : "", json.size());
        if (!config_.empty())
        {
            const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(config_.c_str()));
            if (!doc.isObject())
            {
                ps::fillError(err, 2, "config 不是 JSON 对象");
                return false;
            }
        }
        return true;
    }

    bool saveConfig(std::string &out, PsError *err) override
    {
        (void)err;
        out = config_.empty() ? "{}" : config_;
        return true;
    }

    PsInputDialog dialog() const override
    {
        return PsInputDialog{nullptr, kSchema, kDefaults};
    }

    bool start(PsError *err) override
    {
        if (handle_)
            return true;
        const PsSourceSink *s = sink();
        if (!s || !s->channel_batch)
        {
            ps::fillError(err, 1, "宿主 sink 未绑定");
            return false;
        }
        char e[256] = {0};
        if (openSource(config_.c_str(), s, host(), &handle_, e, sizeof(e)) != 0)
        {
            ps::fillError(err, 3, e[0] ? e : "打开 MAVLink 失败");
            return false;
        }
        state_ = PS_INPUT_RUNNING;
        return true;
    }

    bool poll(PsError *err) override
    {
        if (!handle_)
            return true;
        char e[256] = {0};
        if (pollSource(handle_, 0, e, sizeof(e)) != 0)
        {
            ps::fillError(err, 4, e[0] ? e : "MAVLink poll 失败");
            return false;
        }
        char st[640] = {0};
        if (statusSource(handle_, st, sizeof(st)) > 0 && std::strstr(st, "replay_done"))
            state_ = PS_INPUT_DONE;
        return true;
    }

    void stop() override
    {
        if (handle_)
        {
            closeSource(handle_);
            handle_ = nullptr;
        }
        state_ = PS_INPUT_IDLE;
    }

    PsInputState currentState() const override { return state_; }

    bool statusJson(std::string &out) override
    {
        char b[768] = {0};
        const int n = statusSource(handle_, b, sizeof(b));
        if (n < 0)
            return false;
        out.assign(b, static_cast<size_t>(n));
        return true;
    }

    bool feedRaw(std::string_view topic, std::string_view encoding, int64_t ts,
                 const uint8_t *data, uint32_t size, PsError *err) override
    {
        char e[256] = {0};
        if (feedSource(handle_, topic.data(), encoding.data(), ts, data, size, e, sizeof(e)) != 0) {
            ps::fillError(err, 5, e[0] ? e : "MAVLink feed 失败");
            return false;
        }
        return true;
    }

private:
    std::string config_;
    void *handle_ = nullptr;
    PsInputState state_ = PS_INPUT_IDLE;
};

namespace {
const PsDataSourceVtable *mavVtable()
{
    static const PsDataSourceVtable vt = ps::VtableBuilder::make<MavInput>(R"({"id":"data_source_mavlink","name":"MAVLink 遥测","version":"1.0.0","kind":"telemetry","transport":"udp","family":"data_source","file_extensions":[".mav",".bin",".dat",".log",".tlog"]})");
    return &vt;
}
} // namespace


extern "C" const PsExtensionDescriptor *ps_extension_entry_data_source_mavlink()
{
    static const PsExtensionDescriptor desc = {
        PS_EXTENSION_ABI_VERSION,
        R"({"id":"data_source_mavlink","name":"MAVLink 遥测","version":"1.0.0","kind":"telemetry","transport":"udp","family":"data_source","file_extensions":[".mav",".bin",".dat",".log",".tlog"]})",
        PS_FAMILY_DATA_SOURCE,
        0,
        nullptr,
        nullptr,
        nullptr,
        mavVtable(),
    };
    return &desc;
}
