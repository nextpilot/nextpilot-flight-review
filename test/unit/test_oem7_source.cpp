#include "ps_plugin_api.h"

#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <thread>

extern "C" const PsPluginDescriptor *ps_plugin_entry_data_source_oem7(void);

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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto *d = ps_plugin_entry_data_source_oem7();
    assert(d && d->family == PS_FAMILY_DATA_SOURCE && d->source);
    char schema[4096] = {0};
    assert(d->source->config_schema_json(schema, sizeof(schema)) > 0);
    assert(std::strstr(schema, "port"));

    QTcpServer server;
    assert(server.listen(QHostAddress::LocalHost, 14601));
    Sink sink;
    PsSourceSink ps{&sink, onBatch, onEvent};
    void *h = nullptr;
    char err[256] = {0};
    assert(d->source->open("{\"host\":\"127.0.0.1\",\"port\":14601,\"timeout_ms\":1000}",
                           &ps, &h, err, sizeof(err)) == 0);
    QTcpSocket *peer = nullptr;
    assert(server.waitForNewConnection(1000));
    peer = server.nextPendingConnection();
    assert(peer);
    peer->write("#BESTPOSA,COM1,0,83.5,FINESTEERING,2163,329760.000,02400000,b1f6,65535;"
                "SOL_COMPUTED,SINGLE,51.15043874397,-114.03066788586,1097.6822,-17.0000,WGS84,"
                "1.3648,1.1806,3.1112,\"\",0.000,0.000,18,18,18,0,00,02,11,01*c3194e35\r\n");
    peer->flush();
    for (int i = 0; i < 20 && sink.data.empty(); ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        d->source->poll(h, 0, err, sizeof(err));
    }
    assert(sink.data.count("oem7.bestpos.lat") == 1);
    assert(std::fabs(sink.data["oem7.bestpos.lat"][0].second - 51.15043874397) < 1e-6);
    assert(d->source->close(h) == 0);
    std::printf("oem7 ok: %zu channels\n", sink.data.size());
    return 0;
}
