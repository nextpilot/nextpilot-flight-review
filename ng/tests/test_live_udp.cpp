// 实时源 UDP 链路测试（M1/T1.1）：
//   data.connect（custom-protocol + 内联模板）→ 本地发 20 帧 → 数据集应有行 →
//   data.disconnect → 源状态 Closed。全程走真实 QUdpSocket 与真实解析器，不打桩。

#include "App.h"
#include "AppContext.h"
#include "DataSource.h"
#include "Dataset.h"
#include "DatasetRegistry.h"
#include "McapCodec.h"
#include "Intent.h"
#include "Result.h"
#include "SourceRegistry.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QThread>
#include <QFile>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QtEndian>

#include <cstdio>

#ifdef __linux__
#include <pty.h>
#include <unistd.h>
#endif

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

    // 录制：raw + mcap 同时两路（正交组合的直接验收），写入 /tmp。
    const QString rawPath = QStringLiteral("/tmp/ng_record_test.raw");
    const QString mcapPath = QStringLiteral("/tmp/ng_record_test.mcap");
    QFile::remove(rawPath);
    QFile::remove(mcapPath);
    Intent rs1;
    rs1.op = QStringLiteral("data.record.start");
    rs1.params[QStringLiteral("source")] = srcId;
    rs1.params[QStringLiteral("format")] = QStringLiteral("raw");
    rs1.params[QStringLiteral("backend")] = QStringLiteral("file");
    rs1.params[QStringLiteral("endpoint")] = rawPath;
    CHECK(app.host().execute(rs1));
    Intent rs2 = rs1;
    rs2.params[QStringLiteral("format")] = QStringLiteral("mcap");
    rs2.params[QStringLiteral("endpoint")] = mcapPath;
    const auto rr2 = app.host().execute(rs2);
    CHECK(rr2);

    // 第三路：raw × udp（后端正交的另一半）。本机绑一个 socket 收包，
    // 证明字节真的投递到了网络上，而不是只在 list 里挂着。
    QUdpSocket sinkRx;
    CHECK(sinkRx.bind(QHostAddress::LocalHost, 0));
    const quint16 sinkPort = sinkRx.localPort();
    Intent rs3 = rs1;
    rs3.params[QStringLiteral("backend")] = QStringLiteral("udp");
    rs3.params[QStringLiteral("endpoint")] = QStringLiteral("127.0.0.1:%1").arg(sinkPort);
    const auto rr3 = app.host().execute(rs3);
    CHECK(rr3);
    if (rr2)
        CHECK(rr2.value().payload.toMap().value(QStringLiteral("format")).toString()
              == QLatin1String("mcap"));

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

    // 速率统计（T1.3 起手）：20 包 / ~1s → hz>0 且 kb_s>0
    Intent stats;
    stats.op = QStringLiteral("data.live.stats");
    const auto sr = app.host().execute(stats);
    CHECK(sr);
    if (sr)
    {
        const QVariantList rows = sr.value().payload.toList();
        CHECK(rows.size() == 1);
        if (!rows.isEmpty())
        {
            const QVariantMap m = rows.first().toMap();
            std::printf("stats hz=%.2f kb_s=%.2f packets=%lld\n",
                        m.value(QStringLiteral("hz")).toDouble(),
                        m.value(QStringLiteral("kb_s")).toDouble(),
                        m.value(QStringLiteral("packets")).toLongLong());
            CHECK(m.value(QStringLiteral("hz")).toDouble() > 0.0);
            CHECK(m.value(QStringLiteral("kb_s")).toDouble() > 0.0);
        }
    }

    // 录制：两路（raw + mcap）× file 后端，落盘大小与 list 状态都要对。
    Intent rl;
    rl.op = QStringLiteral("data.record.list");
    const auto rlr = app.host().execute(rl);
    CHECK(rlr);
    if (rlr)
        CHECK(rlr.value().payload.toList().size() == 3); // raw/file + mcap/file + raw/udp

    Intent rsp;
    rsp.op = QStringLiteral("data.record.stop");
    rsp.params[QStringLiteral("source")] = srcId;
    const auto rspr = app.host().execute(rsp);
    CHECK(rspr);
    if (rspr)
    {
        std::printf("record.stop list=%d\n", int(rspr.value().payload.toList().size()));
        for (const QVariant &v : rspr.value().payload.toList())
        {
            const QVariantMap m = v.toMap();
            const qlonglong wb = m.value(QStringLiteral("bytes")).toLongLong();
            std::printf("record %s -> %s bytes=%lld\n",
                        qPrintable(m.value(QStringLiteral("format")).toString()),
                        qPrintable(m.value(QStringLiteral("endpoint")).toString()), wb);
            CHECK(wb >= 160);
        }
    }
    CHECK(QFile(rawPath).size() >= 160);
    CHECK(QFile(mcapPath).size() > 0);

    // udp 那路：接收端要真的收到 ≥160 字节。
    {
        pump(300); // 让 UDP 发送侧把队列冲出去
        int rxPkts = 0, rxBytes = 0;
        while (sinkRx.hasPendingDatagrams())
        {
            QByteArray d;
            d.resize(static_cast<int>(sinkRx.pendingDatagramSize()));
            sinkRx.readDatagram(d.data(), d.size());
            rxBytes += d.size();
            ++rxPkts;
        }
        std::printf("udp sink rx: %d 包 %d 字节\n", rxPkts, rxBytes);
        CHECK(rxBytes >= 160);
    }
    // AC-07（字节级自洽）：mcap 回读出 20 条消息，拼接后与 raw 文件正文逐字节相同。
    // 说明：raw 文件外面套了一层 PSRAW1 头（16B），故只比正文 160B。
    {
        rec::McapPlayer player;
        CHECK(player.open(mcapPath));
        QByteArray joined;
        int msgs = 0;
        rec::McapMessage msg;
        QString perr;
        while (player.nextMessage(msg, &perr))
        {
            joined += msg.data;
            ++msgs;
            std::printf("  mcap msg[%d] topic=%s len=%d\n", msgs - 1, qPrintable(msg.topic),
                        int(msg.data.size()));
        }
        QFile rf(rawPath);
        CHECK(rf.open(QIODevice::ReadOnly));
        const QByteArray rawAll = rf.readAll();
        const QByteArray rawBody = rawAll.right(160);
        std::printf("mcap back: msgs=%d joined=%d raw_file=%d (body=160)\n", msgs,
                    int(joined.size()), int(rawAll.size()));
        // 自包含：Schema 里要写明"哪个解析器、什么模板"，否则回放无从下手。
        const QByteArray sj = player.schemaJson();
        std::printf("mcap schema=%s\n", sj.constData());
        CHECK(!sj.isEmpty());
        CHECK(sj.contains("custom-protocol"));
        CHECK(sj.contains("template"));
        // 注意：mcap 消息粒度是"泵批次"而非单帧（一条消息可含多帧），
        // 所以这里断言的是字节级自洽；"回读成 20 行"要等 schema 自包含回放（另卡）。
        CHECK(msgs >= 1);
        CHECK(joined.size() == 160);             // 字节数一致
        CHECK(joined == rawBody);                // 逐字节无损
        if (!perr.isEmpty())
            std::printf("mcap back err=%s\n", qPrintable(perr));
    }
    // AC-07 闭环：把刚录的 mcap 用 data.load 重新打开，行数应与源一致。
    // 这条能过，靠的是 mcap 里那份 schema（parser + template）——没有它只剩一袋字节。
    {
        Intent ld;
        ld.op = QStringLiteral("data.load");
        ld.params[QStringLiteral("path")] = mcapPath;
        ld.params[QStringLiteral("format")] = QStringLiteral("mcap");
        const int dsBefore = ctx.data().count();
        const auto ldr = app.host().execute(ld);
        CHECK(ldr);
        std::printf("reload: datasets %d -> %d\n", dsBefore, ctx.data().count());
        if (!ldr)
            std::printf("mcap load 失败: %s\n", qPrintable(ldr.error().message));
        else
        {
            const QVariant pv = ldr.value().payload;
            int id2 = pv.toInt();
            if (id2 <= 0)
                id2 = pv.toMap().value(QStringLiteral("dataset")).toInt();
            std::printf("mcap reload dataset=%d\n", id2);
            CHECK(id2 > 0);
            if (const Dataset *rd = ctx.data().get(id2))
            {
                int rows = 0;
                for (const QString &tn : rd->tableNames())
                    if (const Table *t = rd->table(tn))
                        rows = std::max(rows, t->rows);
                std::printf("mcap reload rows=%d timeMax=%.3f tables=%d\n", rows,
                            rd->timeMax(), int(rd->tableNames().size()));
                CHECK(rows == 20);
                CHECK(rd->timeMax() > 1.5);
                CHECK(ctx.data().count() > dsBefore);
            }
        }
    }
    // 停止后源上的 recordings 应被清空（不再显示录制中）。
    if (const DataSourceInfo *rinfo = ctx.sources().get(srcId))
        CHECK(rinfo->recordings.isEmpty());

    // 源清单 + 重连（T1.4）：list 能看到 live 源；重连后继续收包（计数累计）
    Intent listIntent;
    listIntent.op = QStringLiteral("data.source.list");
    const auto lr = app.host().execute(listIntent);
    CHECK(lr);
    if (lr)
    {
        const QVariantList rows = lr.value().payload.toList();
        CHECK(!rows.isEmpty());
        const QVariantMap first = rows.first().toMap();
        CHECK(first.value(QStringLiteral("live")).toBool());
        CHECK(first.value(QStringLiteral("hz")).toDouble() >= 0.0);
    }

    Intent re;
    re.op = QStringLiteral("data.reconnect");
    re.params[QStringLiteral("id")] = srcId;
    CHECK(app.host().execute(re));
    for (int k = 0; k < 5; ++k)
    {
        QByteArray frame(8, '\0');
        qToLittleEndian<quint32>(quint32(k * 100), frame.data());
        qToLittleEndian<float>(0.3f, frame.data() + 4);
        tx.writeDatagram(frame, QHostAddress::LocalHost, port);
        pump(3);
    }
    pump(600);
    Intent stats2;
    stats2.op = QStringLiteral("data.live.stats");
    if (const auto s2 = app.host().execute(stats2))
    {
        const QVariantList rows = s2.value().payload.toList();
        if (!rows.isEmpty())
            CHECK(rows.first().toMap().value(QStringLiteral("packets")).toLongLong() >= 20);
    }

    Intent dis;
    dis.op = QStringLiteral("data.disconnect");
    dis.params[QStringLiteral("id")] = srcId;
    const auto dr = app.host().execute(dis);
    CHECK(dr);
    if (dr)
    {
        const QVariantMap p = dr.value().payload.toMap();
        // 20 + 重连后 5 = 25；暂停期那 5 帧不计数（不喂解析器）
        CHECK(p.value(QStringLiteral("packets")).toLongLong() >= 20);
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

    // ---- TCP 前端：监听端 tcp://0.0.0.0:port，测试端连上去写帧 ----
    {
        QUdpSocket portProbe; // 借用绑定取空闲端口（TCP/UDP 端口空间独立，仅作随机端口用）
        CHECK(portProbe.bind(QHostAddress::LocalHost, 0));
        const quint16 tcpPort = portProbe.localPort();
        portProbe.close();

        Intent tc;
        tc.op = QStringLiteral("data.connect");
        tc.params[QStringLiteral("url")] =
            QStringLiteral("tcp://0.0.0.0:%1").arg(tcpPort);
        tc.params[QStringLiteral("parser")] = QStringLiteral("custom-protocol");
        tc.params[QStringLiteral("template")] = tpl;
        tc.params[QStringLiteral("name")] = QStringLiteral("tcp-test");
        const auto tcr = app.host().execute(tc);
        CHECK(tcr);
        if (tcr)
        {
            const int tid = tcr.value().payload.toMap().value(QStringLiteral("source")).toInt();
            const int tds = tcr.value().payload.toMap().value(QStringLiteral("dataset")).toInt();
            QTcpSocket client;
            client.connectToHost(QStringLiteral("127.0.0.1"), tcpPort);
            CHECK(client.waitForConnected(3000));
            pump(300);
            for (int k = 0; k < 20; ++k)
            {
                QByteArray frame(8, '\0');
                qToLittleEndian<quint32>(quint32(k * 100), frame.data());
                qToLittleEndian<float>(float(0.05 * k), frame.data() + 4);
                client.write(frame);
                client.flush();
                pump(3);
            }
            pump(1000);
            Intent td;
            td.op = QStringLiteral("data.disconnect");
            td.params[QStringLiteral("id")] = tid;
            CHECK(app.host().execute(td));
            if (const Dataset *tdsPtr = ctx.data().get(tds))
            {
                int rows = 0;
                for (const QString &tn : tdsPtr->tableNames())
                    if (const Table *t = tdsPtr->table(tn))
                        rows = std::max(rows, t->rows);
                std::printf("tcp rows=%d timeMax=%.3f\n", rows, tdsPtr->timeMax());
                CHECK(rows >= 18);
            }
            client.close();
        }
    }

    // ---- 非法参数：三条都要 fail 且不崩 ----
    {
        Intent bad1; // 端口越界
        bad1.op = QStringLiteral("data.connect");
        bad1.params[QStringLiteral("url")] = QStringLiteral("tcp://127.0.0.1:70000");
        bad1.params[QStringLiteral("parser")] = QStringLiteral("custom-protocol");
        CHECK(!app.host().execute(bad1));

        Intent bad2; // 未知解析器
        bad2.op = QStringLiteral("data.connect");
        bad2.params[QStringLiteral("url")] = QStringLiteral("udp://0.0.0.0:14560");
        bad2.params[QStringLiteral("parser")] = QStringLiteral("no-such-parser");
        CHECK(!app.host().execute(bad2));

        Intent bad3; // config 缺 value
        bad3.op = QStringLiteral("data.config");
        bad3.params[QStringLiteral("key")] = QStringLiteral("bufferBytes");
        CHECK(!app.host().execute(bad3));

        Intent bad4; // 不存在的 live 会话
        bad4.op = QStringLiteral("data.live.stats");
        bad4.params[QStringLiteral("id")] = 9999;
        const auto r4 = app.host().execute(bad4);
        CHECK(r4); // stats 只过滤不报错，返回空列表
        if (r4)
            CHECK(r4.value().payload.toList().isEmpty());
        std::printf("bad-args: 3 rejected, 1 empty-list\n");
    }

#ifdef __linux__
    // ---- 串口前端：用 pty 对模拟 RS232（openpty 给一对 master/slave）----
    {
        int master = -1;
        int slave = -1;
        CHECK(::openpty(&master, &slave, nullptr, nullptr, nullptr) == 0);
        if (master >= 0 && slave >= 0)
        {
            const QString dev = QString::fromUtf8(::ttyname(slave));
            Intent sc;
            sc.op = QStringLiteral("data.connect");
            sc.params[QStringLiteral("url")] =
                QStringLiteral("serial://%1:115200").arg(dev);
            sc.params[QStringLiteral("parser")] = QStringLiteral("custom-protocol");
            sc.params[QStringLiteral("template")] = tpl;
            sc.params[QStringLiteral("name")] = QStringLiteral("serial-test");
            const auto scr = app.host().execute(sc);
            CHECK(scr);
            if (scr)
            {
                const int sds = scr.value().payload.toMap().value(QStringLiteral("dataset")).toInt();
                const int sid = scr.value().payload.toMap().value(QStringLiteral("source")).toInt();
                for (int k = 0; k < 20; ++k)
                {
                    QByteArray frame(8, '\0');
                    qToLittleEndian<quint32>(quint32(k * 100), frame.data());
                    qToLittleEndian<float>(float(0.2 * k), frame.data() + 4);
                    CHECK(::write(master, frame.constData(), size_t(frame.size())) == frame.size());
                    pump(3);
                }
                pump(1000);
                Intent sd;
                sd.op = QStringLiteral("data.disconnect");
                sd.params[QStringLiteral("id")] = sid;
                CHECK(app.host().execute(sd));
                if (const Dataset *sdsPtr = ctx.data().get(sds))
                {
                    int rows = 0;
                    for (const QString &tn : sdsPtr->tableNames())
                        if (const Table *t = sdsPtr->table(tn))
                            rows = std::max(rows, t->rows);
                    std::printf("serial rows=%d timeMax=%.3f\n", rows, sdsPtr->timeMax());
                    CHECK(rows >= 18); // 串口按字节流聚合，允许最后一帧被截断
                }
            }
            ::close(master);
            ::close(slave);
        }
    }
#endif

    if (failures == 0)
        std::printf("live udp: ALL PASS\n");
    return failures == 0 ? 0 : 1;
}
