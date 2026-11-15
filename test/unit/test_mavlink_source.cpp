#include "ps_extension_api.h"
#include "host/extension/HostApi.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUdpSocket>
#include <QVector>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_source_mavlink(void);

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
static void putU64(QByteArray &b, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        b.append(char((v >> (8 * i)) & 0xff));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto *d = ps_extension_entry_data_source_mavlink();
    assert(d && d->family == PS_FAMILY_DATA_INPUT && d->data_input);
    const PsDataInputVtable *v = d->data_input;
    void *probe = v->create();
    assert(probe);
    assert((v->capabilities(probe) & PS_INPUT_STREAM) != 0);
    PsInputDialog dlg = v->get_dialog(probe);
    assert(dlg.schema_json && std::strstr(dlg.schema_json, "port"));
    v->destroy(probe);

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
    const QString rawPath = rawDir.filePath(QStringLiteral("mav.raw"));
    QJsonObject cfgObj{{QStringLiteral("port"), 14555}, {QStringLiteral("bind"), QStringLiteral("127.0.0.1")},
                       {QStringLiteral("raw_path"), rawPath}};
    const QByteArray cfgBytes = QJsonDocument(cfgObj).toJson(QJsonDocument::Compact);
    PsStringView sv{cfgBytes.constData(), static_cast<uint32_t>(cfgBytes.size())};
    assert(v->load_config(h, sv, &perr));
    assert(v->start(h, &perr));

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
        v->poll(h, &perr);
    }
    assert(!sink.data.empty());
    assert(sink.data.count("mav.att.roll") == 1);
    const auto &roll = sink.data["mav.att.roll"];
    assert(roll.size() == 1);
    assert(std::fabs(roll[0].second - 5.729578) < 0.01);
    const size_t liveCount = roll.size();
    v->stop(h);
    v->destroy(h);
    assert(QFileInfo(rawPath).size() > 0);
    {
        QFile raw(rawPath);
        assert(raw.open(QIODevice::ReadOnly));
        assert(raw.read(6) == QByteArrayLiteral("PSRAW1"));
    }

    // 共享 transport：external 模式不碰 socket，只吃宿主喂进来的字节。
    const size_t extBase = sink.data["mav.att.roll"].size();
    void *eh = v->create();
    assert(eh);
    assert(v->bind(eh, services, &perr));
    QJsonObject extObj{{QStringLiteral("transport"), QStringLiteral("external")}};
    const QByteArray extBytes = QJsonDocument(extObj).toJson(QJsonDocument::Compact);
    PsStringView esv{extBytes.constData(), static_cast<uint32_t>(extBytes.size())};
    assert(v->load_config(eh, esv, &perr));
    assert(v->start(eh, &perr));
    char feedErr[128] = {0};
    assert(v->feed_raw(eh, "mavlink", "mavlink", 0,
                       reinterpret_cast<const uint8_t *>(f.constData()),
                       static_cast<uint32_t>(f.size()), feedErr, sizeof(feedErr)) == 0);
    assert(sink.data["mav.att.roll"].size() == extBase + 1);
    v->stop(eh);
    v->destroy(eh);

    // 原始文件由同一个 MAVLink 扩展回放，不依赖新 loader 扩展。
    void *rh = v->create();
    assert(rh);
    assert(v->bind(rh, services, &perr));
    QJsonObject replayObj{{QStringLiteral("filepath"), rawPath}};
    const QByteArray replayBytes = QJsonDocument(replayObj).toJson(QJsonDocument::Compact);
    PsStringView rsv{replayBytes.constData(), static_cast<uint32_t>(replayBytes.size())};
    assert(v->load_config(rh, rsv, &perr));
    assert(v->start(rh, &perr));
    for (int i = 0; i < 5 && sink.data["mav.att.roll"].size() <= liveCount; ++i)
        v->poll(rh, &perr);
    assert(sink.data["mav.att.roll"].size() > liveCount);
    v->stop(rh);
    v->destroy(rh);

    // .tlog：8 字节时间戳 + MAVLink 帧，同一个扩展原生回放。
    const QString tlogPath = rawDir.filePath(QStringLiteral("sample.tlog"));
    {
        QFile tlog(tlogPath);
        assert(tlog.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QByteArray rec;
        putU64(rec, 1000000);
        rec.append(f);
        tlog.write(rec);
        tlog.close();
    }
    const size_t tlogBase = sink.data["mav.att.roll"].size();
    void *th = v->create();
    assert(th);
    assert(v->bind(th, services, &perr));
    QJsonObject tlogObj{{QStringLiteral("filepath"), tlogPath}};
    const QByteArray tlogBytes = QJsonDocument(tlogObj).toJson(QJsonDocument::Compact);
    PsStringView tsv{tlogBytes.constData(), static_cast<uint32_t>(tlogBytes.size())};
    assert(v->load_config(th, tsv, &perr));
    assert(v->start(th, &perr));
    for (int i = 0; i < 5 && sink.data["mav.att.roll"].size() <= tlogBase; ++i)
        v->poll(th, &perr);
    assert(sink.data["mav.att.roll"].size() > tlogBase);
    v->stop(th);
    v->destroy(th);

    std::printf("mavlink ok: %zu channels, roll=%.4f\n", sink.data.size(), roll[0].second);
    return 0;
}
