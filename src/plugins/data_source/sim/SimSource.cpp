#include "ps_plugin_api.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

struct SimState
{
    PsSourceSink sink{};
    double rate = 50.0;
    int channels = 3;
    double duration = 0.0;
    uint64_t generated = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    bool finished = false;
};

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
  "title":"实时仿真源",
  "fields":[
    {"key":"rate_hz","type":"int","label":"采样率","default":50,"min":1,"max":1000,"suffix":" Hz"},
    {"key":"channels","type":"int","label":"通道数","default":3,"min":1,"max":4},
    {"key":"duration_s","type":"int","label":"时长（0=不限）","default":0,"min":0,"max":3600,"suffix":" s"}
  ]
})";

const char *kDefaults = R"({"rate_hz":50,"channels":3,"duration_s":0})";

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
    auto *st = new SimState;
    st->sink = *sink;
    st->rate = qBound(1.0, cfg.value(QStringLiteral("rate_hz")).toDouble(50.0), 1000.0);
    st->channels = qBound(1, cfg.value(QStringLiteral("channels")).toInt(3), 4);
    st->duration = qMax(0.0, cfg.value(QStringLiteral("duration_s")).toDouble(0.0));
    st->start = std::chrono::steady_clock::now();
    *handle = st;
    return 0;
}

int32_t pollSource(void *handle, int32_t, char *err, uint32_t errCap)
{
    auto *st = static_cast<SimState *>(handle);
    if (!st)
        return 1;
    if (st->finished)
        return 0;
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - st->start).count();
    const uint64_t due = static_cast<uint64_t>(elapsed * st->rate);
    uint64_t upto = due;
    if (upto > st->generated + 4096)
        upto = st->generated + 4096;
    if (upto <= st->generated)
        return 0;

    const uint32_t count = static_cast<uint32_t>(upto - st->generated);
    std::vector<double> t(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint64_t k = st->generated + i;
        t[i] = static_cast<double>(k) / st->rate;
    }
    std::vector<double> v(count);
    for (int ch = 0; ch < st->channels; ++ch)
    {
        const char *name = nullptr;
        const char *unit = nullptr;
        switch (ch)
        {
        case 0: name = "sim.roll"; unit = "deg"; break;
        case 1: name = "sim.pitch"; unit = "deg"; break;
        case 2: name = "sim.rpm"; unit = "rpm"; break;
        default: name = "sim.temp"; unit = "C"; break;
        }
        const double freq = 0.7 + 0.35 * ch;
        for (uint32_t i = 0; i < count; ++i)
        {
            const double x = t[i];
            if (ch == 0)
                v[i] = 1.2 * std::sin(2.0 * kPi * freq * x);
            else if (ch == 1)
                v[i] = 0.8 * std::sin(2.0 * kPi * freq * x + 0.7);
            else if (ch == 2)
                v[i] = 1000.0 + 180.0 * std::sin(2.0 * kPi * 0.45 * x);
            else
                v[i] = 30.0 + 2.0 * std::sin(2.0 * kPi * 0.2 * x);
        }
        if (st->sink.channel_batch(st->sink.ctx, name, unit, t.data(), v.data(), count) != 0)
        {
            if (err && errCap)
                std::snprintf(err, errCap, "宿主缓冲区已满");
            st->finished = true;
            return 0;
        }
    }
    st->generated = upto;
    if (st->duration > 0.0 && t[count - 1] >= st->duration)
        st->finished = true;
    return 0;
}

int32_t closeSource(void *handle)
{
    delete static_cast<SimState *>(handle);
    return 0;
}

int32_t statusSource(void *handle, char *out, uint32_t cap)
{
    auto *st = static_cast<SimState *>(handle);
    if (!st)
        return writeJson(R"({"state":"closed"})", out, cap);
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  R"({"state":"%s","rate_hz":%.0f,"channels":%d,"samples":%llu})",
                  st->finished ? "finished" : "running", st->rate, st->channels,
                  static_cast<unsigned long long>(st->generated));
    return writeJson(buf, out, cap);
}

const PsDataSourceVtable kSource = {
    PS_PLUGIN_ABI_VERSION,
    "data_source_sim",
    "实时仿真",
    "sim",
    "internal",
    1,
    {0, 0, 0, 0, 0, 0, 0},
    [](char *o, uint32_t c) { return writeJson(kSchema, o, c); },
    [](char *o, uint32_t c) { return writeJson(kDefaults, o, c); },
    openSource,
    pollSource,
    closeSource,
    statusSource,
};

const PsPluginDescriptor kDescriptor = {
    PS_PLUGIN_ABI_VERSION,
    R"({"id":"data_source_sim","name":"实时仿真源","version":"1.0.0","family":"data_source","description":"可配置采样率/通道数的内置实时源，用于验证 source 生命周期与实时记录"})",
    PS_FAMILY_DATA_SOURCE,
    PS_CAP_STREAM | PS_CAP_RECORD | PS_CAP_SCHEMA,
    nullptr,
    nullptr,
    &kSource,
};

} // namespace

extern "C" const PsPluginDescriptor *ps_plugin_entry_data_source_sim(void)
{
    return &kDescriptor;
}
