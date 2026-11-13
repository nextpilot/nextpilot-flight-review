#include "ps_plugin_api.h"
#include "app/plugin_host/HostApi.h"

#include <QCoreApplication>
#include <QUdpSocket>
#include <QVector>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>

extern "C" const PsPluginDescriptor *ps_plugin_entry_data_source_mavlink(void);

struct Sink
{
    std::map<std::string, std::vector<std::pair<double, double>>> data;
};

static int32_t onBatch(void *ctx, const char *name, const char *, const double *t,
                       const double *v, uint32_t n)
{
    auto *s = static_cast<Sink *>(ctx);
    auto &d = s->data[name];
    for (uint32_t i = 0; i < n; ++i)
        d.emplace_back(t[i], v[i]);
    return 0;
}
static int32_t onEvent(void *, const char *, const char *) { return 0; }

static void putU32(QByteArray &b, uint32_t v)
{
    b.append(char(v & 0xff));
    b.append(char((v >> 8) & 0xff));
    b.append(char((v >> 16) & 0xff));
    b.append(char((v >> 24) & 0xff));
}
static void putI16(QByteArray &b, int16_t v)
{
    b.append(char(static_cast<uint8_t>(v)));
    b.append(char(static_cast<uint8_t>(static_cast<uint16_t>(v) >> 8)));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto *d = ps_plugin_entry_data_source_mavlink();
    assert(d && d->family == PS_FAMILY_DATA_SOURCE && d->source);
    char schema[4096] = {0};
    assert(d->source->config_schema_json(schema, sizeof(schema)) > 0);
    assert(std::strstr(schema, "port"));

    Sink sink;
    PsSourceSink ps{&sink, onBatch, onEvent};
    void *h = nullptr;
    char err[256] = {0};
    assert(d->source->open("{\"port\":14555,\"bind\":\"127.0.0.1\"}", &ps, ps_host_api(), &h, err, sizeof(err)) == 0);

    QByteArray f;
    f.append(char(0xFD)).append(char(16)).append(char(0)).append(char(0)).append(char(0));
    f.append(char(1)).append(char(1)).append(char(30)).append(char(0)).append(char(0));
    putU32(f, 0);          // time_boot_ms
    putI16(f, 1000);       // roll 0.1 rad
    putI16(f, -500);       // pitch
    putI16(f, 0);          // yaw
    putI16(f, 0); putI16(f, 0); putI16(f, 0);
    f.append(char(0)).append(char(0)); // crc（解析器不校验）

    QUdpSocket tx;
    tx.writeDatagram(f, QHostAddress::LocalHost, 14555);
    for (int i = 0; i < 20 && sink.data.empty(); ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        d->source->poll(h, 0, err, sizeof(err));
    }
    assert(!sink.data.empty());
    assert(sink.data.count("mav.att.roll") == 1);
    const auto &roll = sink.data["mav.att.roll"];
    assert(roll.size() == 1);
    assert(std::fabs(roll[0].second - 5.729578) < 0.01);
    assert(d->source->close(h) == 0);
    std::printf("mavlink ok: %zu channels, roll=%.4f\n", sink.data.size(), roll[0].second);
    return 0;
}
