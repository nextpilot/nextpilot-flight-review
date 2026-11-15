#include "ps_extension_api.h"
#include "host/extension/HostApi.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTcpSocket>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_source_oem7(void);

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


static void put16(std::vector<unsigned char> &b, unsigned v)
{
    b.push_back(v & 0xFF);
    b.push_back((v >> 8) & 0xFF);
}
static void put32(std::vector<unsigned char> &b, unsigned v)
{
    for (int i = 0; i < 4; ++i)
        b.push_back((v >> (8 * i)) & 0xFF);
}
static void putF(std::vector<unsigned char> &b, float v)
{
    unsigned char p[4];
    std::memcpy(p, &v, 4);
    b.insert(b.end(), p, p + 4);
}
static void putD(std::vector<unsigned char> &b, double v)
{
    unsigned char p[8];
    std::memcpy(p, &v, 8);
    b.insert(b.end(), p, p + 8);
}
static std::vector<unsigned char> bestPosBinary(bool shortHeader = false)
{
    std::vector<unsigned char> body;
    put32(body, 0);          // solution_status
    put32(body, 16);         // position_type
    putD(body, 51.15043874397);
    putD(body, -114.03066788586);
    putD(body, 1097.6822);
    putF(body, -17.0f);
    put32(body, 0);          // datum_id
    putF(body, 1.3648f);
    putF(body, 1.1806f);
    putF(body, 3.1112f);
    body.insert(body.end(), {0, 0, 0, 0}); // base_id[4]
    putF(body, 0.0f);        // diff_age
    putF(body, 0.0f);        // solution_age
    body.push_back(18);      // num_svs
    body.push_back(18);      // num_soln_svs
    body.push_back(18);      // num_soln_L1_svs
    body.push_back(0);       // num_soln_multi_svs
    body.push_back(0);       // extended_solution_status2
    body.push_back(2);       // ext_sol_stat
    body.push_back(11);      // gal_and_bds_mask
    body.push_back(1);       // gps_and_glo_mask

    std::vector<unsigned char> f;
    if (shortHeader)
    {
        f = {0xAA, 0x44, 0x13, static_cast<unsigned char>(body.size())};
        put16(f, 42);
        put16(f, 2163);
        put32(f, 329760000);
    }
    else
    {
        f = {0xAA, 0x44, 0x12, 0x1C};
        put16(f, 42);            // messageID
        f.push_back(0);          // msgType
        f.push_back(32);         // port
        put16(f, static_cast<unsigned>(body.size()));
        put16(f, 0);             // sequence
        f.push_back(0);          // idle
        f.push_back(0);          // timeStatus
        put16(f, 2163);          // week
        put32(f, 329760000);     // msec
        put32(f, 0);             // status
        put16(f, 52666);         // msgDefCrc
        put16(f, 0);             // sw version
    }
    f.insert(f.end(), body.begin(), body.end());
    put32(f, 0);             // crc（当前不解校验）
    return f;
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto *d = ps_extension_entry_data_source_oem7();
    assert(d && d->family == PS_FAMILY_DATA_INPUT && d->data_input);
    const PsDataInputVtable *v = d->data_input;
    void *probe = v->create();
    assert(probe);
    const uint64_t caps = v->capabilities(probe);
    assert((caps & PS_INPUT_STREAM) != 0);
    assert((caps & PS_INPUT_COMMAND) != 0);
    PsInputDialog dlg = v->get_dialog(probe);
    assert(dlg.schema_json && std::strstr(dlg.schema_json, "port"));
    assert(std::strstr(dlg.schema_json, "serial"));
    assert(std::strstr(dlg.schema_json, "transport"));
    assert(std::strstr(dlg.schema_json, "commands"));
    v->destroy(probe);

    QTcpServer server;
    assert(server.listen(QHostAddress::LocalHost, 0));
    const int port = server.serverPort();
    Sink sink;
    PsSourceSink ps{&sink, onBatch, onEvent};
    void *h = v->create();
    assert(h);
    PsDataInputServices services{};
    services.abi_version = PS_HOST_API_VERSION;
    services.host = ps_host_api();
    services.sink = &ps;
    PsError perr{};
    assert(v->bind(h, services, &perr));
    QTemporaryDir rawDir;
    assert(rawDir.isValid());
    const QString rawPath = rawDir.filePath(QStringLiteral("oem7.raw"));
    QJsonObject cfgObj{{QStringLiteral("host"), QStringLiteral("127.0.0.1")},
                       {QStringLiteral("port"), port},
                       {QStringLiteral("timeout_ms"), 1000},
                       {QStringLiteral("database"), QString::fromUtf8(TEST_OEM7_DB)},
                       {QStringLiteral("raw_path"), rawPath}};
    const QByteArray cfg = QJsonDocument(cfgObj).toJson(QJsonDocument::Compact);
    PsStringView sv{cfg.constData(), static_cast<uint32_t>(cfg.size())};
    assert(v->load_config(h, sv, &perr));
    assert(v->start(h, &perr));
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
        v->poll(h, &perr);
    }
    const std::string latKey = sink.data.count("oem7.bestpos.latitude") ? "oem7.bestpos.latitude"
                                                                      : "oem7.bestpos.lat";
    assert(sink.data.count(latKey) == 1);
    assert(std::fabs(sink.data[latKey][0].second - 51.15043874397) < 1e-6);
    // 同一连接再发一帧 OEM7 binary BESTPOS，验证 database 数据驱动解码。
    const auto frame = bestPosBinary();
    peer->write(reinterpret_cast<const char *>(frame.data()), static_cast<qint64>(frame.size()));
    peer->flush();
    for (int i = 0; i < 20 && sink.data["oem7.bestpos.latitude"].size() < 2; ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        v->poll(h, &perr);
    }
    assert(sink.data["oem7.bestpos.latitude"].size() == 2);
    assert(std::fabs(sink.data["oem7.bestpos.latitude"].back().second - 51.15043874397) < 1e-6);

    // 短头 binary（12 字节头，无 msgDefCrc）也要能解。
    const auto shortFrame = bestPosBinary(true);
    peer->write(reinterpret_cast<const char *>(shortFrame.data()), static_cast<qint64>(shortFrame.size()));
    peer->flush();
    for (int i = 0; i < 20 && sink.data["oem7.bestpos.latitude"].size() < 3; ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        v->poll(h, &perr);
    }
    assert(sink.data["oem7.bestpos.latitude"].size() == 3);

    PsStringView cmdOut{};
    const char *cmd = "VERSION";
    assert(v->send_command && v->send_command(h, PsStringView{cmd, 7}, &cmdOut, &perr));
    assert(cmdOut.data && std::strstr(cmdOut.data, "\"ok\":true"));
    peer->waitForReadyRead(500);
    const QByteArray sent = peer->readAll();
    assert(sent.contains("$VERSION\r\n"));

    const size_t liveCount = sink.data["oem7.bestpos.latitude"].size();
    v->stop(h);
    v->destroy(h);
    assert(QFileInfo(rawPath).size() > 0);
    {
        QFile raw(rawPath);
        assert(raw.open(QIODevice::ReadOnly));
        const QByteArray magic = raw.read(6);
        assert(magic == QByteArrayLiteral("PSRAW1"));
    }

    // OEM7 原始字节回放：同一个 OEM7 扩展，filepath 模式；
    // database 已嵌入 raw 头，这里故意不再传 database。
    void *rh = v->create();
    assert(rh);
    assert(v->bind(rh, services, &perr));
    QJsonObject replayObj{{QStringLiteral("filepath"), rawPath}};
    const QByteArray replayBytes = QJsonDocument(replayObj).toJson(QJsonDocument::Compact);
    PsStringView rsv{replayBytes.constData(), static_cast<uint32_t>(replayBytes.size())};
    assert(v->load_config(rh, rsv, &perr));
    assert(v->start(rh, &perr));
    {
        char status[1024] = {0};
        const int32_t n = v->status_json(rh, status, sizeof(status));
        assert(n > 0 && std::strstr(status, "\"db_source\":\"embedded\""));
    }
    for (int i = 0; i < 5 && sink.data["oem7.bestpos.latitude"].size() <= liveCount; ++i)
        v->poll(rh, &perr);
    assert(sink.data["oem7.bestpos.latitude"].size() > liveCount);
    v->stop(rh);
    v->destroy(rh);
    std::printf("oem7 ok: %zu channels\n", sink.data.size());
    return 0;
}
