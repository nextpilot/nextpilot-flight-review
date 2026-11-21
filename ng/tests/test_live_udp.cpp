// 实时源 UDP 链路测试（M1/T1.1）：
//   data.connect（custom-protocol + 内联模板）→ 本地发 20 帧 → 数据集应有行 →
//   data.disconnect → 源状态 Closed。全程走真实 QUdpSocket 与真实解析器，不打桩。

#include "App.h"
#include "AppContext.h"
#include "DataSource.h"
#include "Dataset.h"
#include "DatasetRegistry.h"
#include "Intent.h"
#include "Result.h"
#include "SourceRegistry.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QThread>
#include <QUdpSocket>
#include <QtEndian>

#include <cstdio>

using namespace nextpilot::plotscope;
using namespace nextpilot::plotscope::app;
using namespace nextpilot::plotscope::context;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;

static int failures = 0;
#define CHECK(c)                                                                     \
    do                                                                               \
    {                                                                                \
        if (!(c))                                                                    \
        {                                                                            \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #c, __FILE__, __LINE__);      \
            ++failures;                                                              \
        }                                                                            \
    } while (0)

static void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication qapp(argc, argv);
    App app;
    app.startup(false);
    auto &ctx = app.context();

    // 找一个空闲端口（bind 0 再取 localPort，避免与固定端口冲突）。
    QUdpSocket probe;
    CHECK(probe.bind(QHostAddress::LocalHost, 0));
    const quint16 port = probe.localPort();
    probe.close();
    CHECK(port > 0);

    // 每帧 8 字节：u32 ms + f32 pitch（little-endian）。
    const QString tpl = QStringLiteral(
        R"({"name":"live-demo","mode":"bin","endian":"little",)"
        R"("segments":[{"name":"ms","type":"u32"},{"name":"pitch","type":"f32"}],)"
        R"("timestamp":{"expr":"ms","unit":"ms","origin":"first_frame"}})");

    Intent connect;
    connect.op = QStringLiteral("data.connect");
    connect.params[QStringLiteral("url")] =
        QStringLiteral("udp://127.0.0.1:%1").arg(port);
    connect.params[QStringLiteral("parser")] = QStringLiteral("custom-protocol");
    connect.params[QStringLiteral("template")] = tpl;
    connect.params[QStringLiteral("name")] = QStringLiteral("live-test");
    const auto cr = app.host().execute(connect);
    CHECK(cr);
    if (!cr)
        return 1; // 连不上就没有后续断言的意义
    const QVariantMap cmp = cr.value().payload.toMap();
    const int srcId = cmp.value(QStringLiteral("source")).toInt();
    const int dsId = cmp.value(QStringLiteral("dataset")).toInt();
    CHECK(srcId >= 0 && dsId >= 0);

    // 发 20 帧（0..1900ms，pitch 正弦）。
    QUdpSocket tx;
    for (int k = 0; k < 20; ++k)
    {
        QByteArray frame(8, '\0');
        qToLittleEndian<quint32>(quint32(k * 100), frame.data());
        qToLittleEndian<float>(float(0.1 * k), frame.data() + 4);
        CHECK(tx.writeDatagram(frame, QHostAddress::LocalHost, port) == frame.size());
        pump(3); // 给接收侧事件循环一点时间
    }
    pump(1500);

    // 追加入参：disconnect 后再看行数（live 通道要到 finish 才把 schema/行数落定）。
    const Dataset *ds = ctx.data().get(dsId);
    CHECK(ds != nullptr);
    if (ds)
    {
        // 此时管道未 finish：行数可能还是 0（schema 待冲刷），先只看数据集存在
    }

    // 暂停态不喂帧（发 5 帧后行数不涨）。
    Intent pause;
    pause.op = QStringLiteral("data.pause");
    pause.params[QStringLiteral("id")] = srcId;
    CHECK(app.host().execute(pause));
    for (int k = 20; k < 25; ++k)
    {
        QByteArray frame(8, '\0');
        qToLittleEndian<quint32>(quint32(k * 100), frame.data());
        qToLittleEndian<float>(0.5f, frame.data() + 4);
        tx.writeDatagram(frame, QHostAddress::LocalHost, port);
        pump(3);
    }
    pump(800);
    // 暂停期发的 5 帧只累 bytes、不计数 packets（下面用 packets==20 断言）。

    Intent dis;
    dis.op = QStringLiteral("data.disconnect");
    dis.params[QStringLiteral("id")] = srcId;
    const auto dr = app.host().execute(dis);
    CHECK(dr);
    if (dr)
    {
        const QVariantMap p = dr.value().payload.toMap();
        // 正好 20：暂停期间那 5 帧没有喂解析器（也不进 packets）
        CHECK(p.value(QStringLiteral("packets")).toLongLong() == 20);
    }
    const DataSourceInfo *info = ctx.sources().get(srcId);
    CHECK(info && info->state == SourceState::Closed);

    // finish 之后再看行数：live 通道在 disconnect 里 finish → schema/行数落定。
    if (const Dataset *disk = ctx.data().get(dsId))
    {
        int rows = 0;
        for (const QString &tn : disk->tableNames())
            if (const Table *t = disk->table(tn))
                rows = std::max(rows, t->rows);
        std::printf("live rows=%d timeMax=%.3f tables=%d\n", rows, disk->timeMax(),
                    int(disk->tableNames().size()));
        CHECK(rows >= 20);
        CHECK(disk->timeMax() > 1.5);
    }

    if (failures == 0)
        std::printf("live udp: ALL PASS\n");
    return failures == 0 ? 0 : 1;
}
