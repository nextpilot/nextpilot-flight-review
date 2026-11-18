#include "App.h"
#include "Intent.h"
#include "DataRepository.h"
#include "Dataset.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace ng::app;
using namespace ng::core;

static QString writeCsv(const QTemporaryDir &dir)
{
    const QString path = dir.filePath(QStringLiteral("demo.csv"));
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&f);
    out << "time,roll,pitch\n"
        << "0,1.0,2.0\n"
        << "1,1.5,2.5\n"
        << "2,2.0,3.0\n";
    f.close();
    return path;
}

// ---- 合成 ULog v1 测试样本 ------------------------------------------------
// 复用旧 extension 的协议知识手工构造：一个 F 格式定义 + A 订阅 + N 条 D 数据，
// 外加 L 事件段。timestamp 为 U64 微秒，字段 float x / uint16 y（验证 typed 列）。
static void put16(QByteArray &b, quint16 v)
{
    b.append(static_cast<char>(v & 0xff));
    b.append(static_cast<char>((v >> 8) & 0xff));
}
static void put32(QByteArray &b, quint32 v)
{
    for (int i = 0; i < 4; ++i) b.append(static_cast<char>((v >> (8 * i)) & 0xff));
}
static void put64(QByteArray &b, quint64 v)
{
    for (int i = 0; i < 8; ++i) b.append(static_cast<char>((v >> (8 * i)) & 0xff));
}
static void putFloat(QByteArray &b, float v)
{
    quint32 bits = 0;
    std::memcpy(&bits, &v, 4);
    put32(b, bits);
}

static void addUlogMessage(QByteArray &out, char type, const QByteArray &body)
{
    put16(out, static_cast<quint16>(body.size()));
    out.append(type);
    out.append(body);
}

// 生成 rows 行、每 topic 两列（F32 x / U16 y）的 ULog；覆盖 kChunkRows=4096 的分批。
static QString writeUlog(const QTemporaryDir &dir, int rows, qint64 *firstUs, qint64 *lastUs)
{
    const QByteArray f("sensor:uint64_t timestamp;float x;uint16_t y;");
    const QString path = dir.filePath(QStringLiteral("demo.ulg"));
    QByteArray out;
    // 头：magic(7) + version(1) + start_timestamp(8)
    out.append("ULog", 4);
    out.append(static_cast<char>(0x01));
    out.append(static_cast<char>(0x12));
    out.append(static_cast<char>(0x35));
    out.append(static_cast<char>(0x01)); // version v1
    put64(out, 0);                        // start timestamp

    addUlogMessage(out, 'F', f);
    QByteArray a;
    a.append(static_cast<char>(0));  // multiId
    put16(a, 1);                     // msgId
    a.append("sensor", 6);
    a.append(static_cast<char>(0));  // cstring terminator
    addUlogMessage(out, 'A', a);

    *firstUs = 1000000; // 起始 1.0s
    for (int i = 0; i < rows; ++i)
    {
        const quint64 ts = static_cast<quint64>(*firstUs) + static_cast<quint64>(i) * 1000ULL; // 每行 1ms
        QByteArray d;
        put16(d, 1);          // msgId
        put64(d, ts);         // timestamp U64 微秒
        putFloat(d, static_cast<float>(i) * 0.5f); // x = i*0.5
        put16(d, static_cast<quint16>(i % 1000));   // y
        addUlogMessage(out, 'D', d);
        *lastUs = static_cast<qint64>(ts);
    }
    // L 事件段：level=6, timestamp=1.5s, "armed"
    QByteArray l;
    l.append(static_cast<char>(6));
    put64(l, 1500000);
    l.append("armed", 5);
    addUlogMessage(out, 'L', l);

    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(out);
    file.close();
    return path;
}

static Intent loadIntent(const QString &path)
{
    Intent i;
    i.op = QStringLiteral("data.load");
    i.params[QStringLiteral("path")] = path;
    i.params[QStringLiteral("format")] = QStringLiteral("csv");
    return i;
}

static Intent renderIntent(DatasetId id, const QString &channel)
{
    Intent i;
    i.op = QStringLiteral("plot.render");
    i.params[QStringLiteral("dataset")] = id;
    if (!channel.isEmpty())
        i.params[QStringLiteral("channel")] = channel;
    return i;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const QString csv = writeCsv(dir);

    // 1) 装载成功后五个模块都激活（3 个器官 + CSV/ULog 两个格式知识包）
    App application;
    const auto errors = application.startup(true);
    assert(errors.isEmpty());
    assert(application.modules().activeIds().size() == 5);
    fprintf(stdout, "[ok] 五个模块按依赖顺序激活（含 CSV/ULog 扩展）\n");

    // 2) 意图执行：CSV 扩展产出 Dataset（一张 CSV = 一张表）
    Result<IntentResult> loaded = application.modules().execute(loadIntent(csv));
    assert(loaded);
    const DatasetId id = loaded.value().payload.toInt();
    const Dataset *ds = application.data().get(id);
    assert(ds != nullptr);

    // 2a) 多表模型：一张表，表内两列 typed 列，共享 3 行时间轴
    assert(ds->tableCount() == 1);
    const Table *t = ds->table(0);
    assert(t->rowCount() == 3);
    assert(t->columns.size() == 2);
    assert(t->column(QStringLiteral("roll")) != nullptr);
    assert(t->column(QStringLiteral("pitch")) != nullptr);
    assert(t->column(QStringLiteral("roll"))->type == ValueType::F64);
    // 时间轴本体在 Rust；C++ 只存 O(1) 元数据（首/末时间），不复制整条数组。
    assert(t->firstTimeUs == 0);
    assert(t->lastTimeUs == 2000000);
    fprintf(stdout, "[ok] Dataset = 多张二维表；C++ 仅存首尾时间元数据（时间轴本体在 Rust）\n");

    // 2b) 通道投影：table.column -> (秒,值) f64 序列
    const auto ch = ds->channel(QStringLiteral("demo.roll"));
    assert(ch.has_value());
    assert(ch->size() == 3);
    assert(ch->time.first() == 0.0 && ch->time.last() == 2.0);
    assert(ch->value.at(1) == 1.5);
    fprintf(stdout, "[ok] 通道投影 table.column 产出绘图用 (t_sec, value) 序列\n");

    // 2b-2) 有界读取：cap 是硬上限，绝不返回超过 cap 的点（kernel 内等步长抽稀）。
    const auto capped = ds->channel(QStringLiteral("demo.roll"), /*cap=*/2);
    assert(capped.has_value() && capped->size() == 2);
    fprintf(stdout, "[ok] readWindow cap 硬上限：请求 2 点只读 2 点（不全量物化）\n");

    // 2c) Rust kernel 是数据地基：C++ 只持句柄，算子与缓存全部委托 Rust C ABI。
    assert(ds->nativeHandle() != nullptr); // 数据本体由 Rust PsDataset 持有
    // LOD 抽稀：把 3 点压到 2 个像素桶，Rust 返回 (x,min,max) 三元组。
    const auto lod = ds->lodMinMax(QStringLiteral("demo"), QStringLiteral("roll"),
                                   0, 2000000, 2);
    assert(!lod.isEmpty() && lod.size() <= 2);
    fprintf(stdout, "[ok] LOD 抽稀在 Rust 侧完成（%d 个三元组）\n", lod.size());
    // 统计算子（ps_stats_series）：roll = 1.0/1.5/2.0，均值 1.5、最小 1、最大 2。
    const auto st = ds->stats(ch->value, /*withMedian=*/true);
    assert(st.has_value());
    assert(st->count == 3);
    assert(std::fabs(st->mean - 1.5) < 1e-9);
    assert(std::fabs(st->min - 1.0) < 1e-9 && std::fabs(st->max - 2.0) < 1e-9);
    fprintf(stdout, "[ok] 统计/滤波/抽取算子委托 Rust ps-ops（mean=%.2f）\n", st->mean);
    // 抽取（ps_decimate_series）：factor=2 点数约减半。
    QVector<double> dt, dv;
    const int decRc = ds->decimate(2, ch->time, ch->value, dt, dv); // 副作用不能放进 assert（NDEBUG 会消除）
    assert(decRc == 0);
    assert(dt.size() < ch->size() && dt.size() >= 1);
    fprintf(stdout, "[ok] 抗混叠抽取 factor=2：%d -> %d 点\n", ch->size(), dt.size());

    // 2d) 缓存（ps-cache）：同一数据集写盘后按源文件指纹还原，schema 与列值一致。
    {
        const QString cachePath = csv + QStringLiteral(".psc");
        const QFileInfo fi(csv);
        const bool cacheSaved = ds->saveCache(cachePath, fi.size(), fi.lastModified().toMSecsSinceEpoch());
        assert(cacheSaved);
        QString cacheErr;
        auto restored = Dataset::loadCache(cachePath, fi.size(),
                                           fi.lastModified().toMSecsSinceEpoch(), &cacheErr);
        assert(restored.has_value());
        assert(restored->nativeHandle() != nullptr);
        assert(restored->tableCount() == 1);
        const auto rch = restored->channel(QStringLiteral("demo.roll"));
        assert(rch.has_value() && rch->size() == 3);
        assert(std::fabs(rch->value.at(1) - 1.5) < 1e-9);
        // 指纹不匹配应过期（size+1）。
        auto stale = Dataset::loadCache(cachePath, fi.size() + 1,
                                        fi.lastModified().toMSecsSinceEpoch());
        assert(!stale.has_value());
        fprintf(stdout, "[ok] ps-cache 指纹命中还原 + 指纹过期失效\n");
    }

    // 2e) ULog 扩展：复用旧 extension 的协议算法，流式灌入 Rust typed 表。
    //     合成 9000 行（跨 kChunkRows=4096 三批），验证多表 / typed 列 / 微秒时间轴 / 缓存。
    {
        qint64 firstUs = 0, lastUs = 0;
        const int rows = 9000;
        const QString ulg = writeUlog(dir, rows, &firstUs, &lastUs);
        Intent ui;
        ui.op = QStringLiteral("data.load");
        ui.params[QStringLiteral("path")] = ulg;
        ui.params[QStringLiteral("format")] = QStringLiteral("ulog");
        Result<IntentResult> uloaded = application.modules().execute(ui);
        if (!uloaded)
            fprintf(stderr, "[diag] ULog 加载失败: %s / %s\n",
                    uloaded.error().code.toUtf8().constData(),
                    uloaded.error().message.toUtf8().constData());
        assert(uloaded);
        const DatasetId uid = uloaded.value().payload.toInt();
        const Dataset *uds = application.data().get(uid);
        assert(uds != nullptr);
        assert(uds->format() == QStringLiteral("ulog"));

        // 多张 typed 表：sensor 数值表 + events + facts 元数据表
        if (uds->tableCount() < 3)
        {
            fprintf(stderr, "[diag] ULog 表数=%d:", uds->tableCount());
            for (int i = 0; i < uds->tableCount(); ++i)
                fprintf(stderr, " %s", uds->table(i)->name.toUtf8().constData());
            fprintf(stderr, "\n");
        }
        assert(uds->tableCount() >= 3);
        const Table *sensor = uds->table(QStringLiteral("sensor"));
        assert(sensor != nullptr);
        assert(sensor->rowCount() == rows);
        // typed 列：x=F32、y=U16（协议算法保留原始字段类型）
        const Column *cx = sensor->column(QStringLiteral("x"));
        const Column *cy = sensor->column(QStringLiteral("y"));
        assert(cx != nullptr && cx->type == ValueType::F32);
        assert(cy != nullptr && cy->type == ValueType::U16);
        // timestamp 走原生 U64 微秒，不经 double 中转——首尾时间精确
        assert(sensor->firstTimeUs == firstUs);
        assert(sensor->lastTimeUs == lastUs);
        fprintf(stdout, "[ok] ULog 流式加载 %d 行（跨 3 批）：多表 + typed 列 F32/U16 + 微秒时间轴精确\n", rows);

        // 全量取值核对（cap 提到行数）：x=i*0.5、y=i%1000，首行/末行精确
        const auto xview = uds->channel(QStringLiteral("sensor"), QStringLiteral("x"), rows);
        assert(xview.has_value() && xview->size() == rows);
        assert(std::fabs(xview->value.first() - 0.0) < 1e-5);
        assert(std::fabs(xview->value.last() - (rows - 1) * 0.5) < 1e-3);
        const auto yview = uds->channel(QStringLiteral("sensor"), QStringLiteral("y"), rows);
        assert(yview.has_value() && yview->size() == rows);
        assert(yview->value.last() == (rows - 1) % 1000);
        fprintf(stdout, "[ok] ULog typed 列值精确（F32 x / U16 y 跨批尾批 flush 正确）\n");

        // 元数据小表：events（L 段）与 facts（头信息）
        assert(uds->table(QStringLiteral("events")) != nullptr);
        assert(uds->table(QStringLiteral("events"))->rowCount() == 1);
        assert(uds->table(QStringLiteral("facts")) != nullptr);
        fprintf(stdout, "[ok] ULog 元数据段 L/I 落表（events/facts）\n");

        // 缓存命中：format 改为 ulog 还原，schema 一致
        {
            const QFileInfo fi(ulg);
            QString ce;
            auto restored = Dataset::loadCache(ulg + QStringLiteral(".psc"),
                                               fi.size(), fi.lastModified().toMSecsSinceEpoch(), &ce);
            assert(restored.has_value());
            assert(restored->table(QStringLiteral("sensor")) != nullptr);
            assert(restored->table(QStringLiteral("sensor"))->rowCount() == rows);
            fprintf(stdout, "[ok] ULog 数据集 ps-cache 指纹命中还原\n");
        }
    }

    // 3) 意图执行：plot 模块按 table.column 编译出 PlotSpec
    Result<IntentResult> rendered = application.modules().execute(renderIntent(id, QStringLiteral("demo.pitch")));
    assert(rendered);
    const QVariantMap spec = rendered.value().payload.toMap();
    assert(spec.value(QStringLiteral("type")).toString() == QStringLiteral("timeseries"));
    assert(spec.value(QStringLiteral("column")).toString() == QStringLiteral("pitch"));
    assert(spec.value(QStringLiteral("valueType")).toString() == QStringLiteral("F64"));
    fprintf(stdout, "[ok] 意图 plot.render 产出 PlotSpec（含 table/column/valueType）\n");

    // 3b) 脚本自动化：Lua 通过门面 ps.* 发意图，与 GUI/CLI 走同一执行器。
    //     脚本里不认识 CSV/plot 模块，只认识 ps 门面。
    {
        const QString script =
            QStringLiteral("local id = ps.data.load('%1', 'csv', 100)\n"
                           "ps.log('script loaded dataset ' .. id)\n"
                           "local spec = ps.plot.render(id, 'demo.roll')\n"
                           "assert(spec.type == 'timeseries')\n"
                           "assert(spec.column == 'roll')\n").arg(csv);
        Intent runScript;
        runScript.op = QStringLiteral("script.run");
        runScript.params[QStringLiteral("source")] = script.toUtf8();

        application.recorder().clear();
        application.recorder().setRecording(true);
        Result<IntentResult> sr = application.modules().execute(runScript);
        application.recorder().setRecording(false);
        assert(sr);
        fprintf(stdout, "[ok] Lua 脚本通过 ps 门面执行成功（headless，无 QWidget）\n");

        // 3c) 录制：脚本调用产生的业务意图被录成 Job（不含 script.run 本身）
        assert(application.recorder().size() == 2);
        const Job recorded = application.recorder().toJob();
        assert(recorded.at(0).op == QStringLiteral("data.load"));
        assert(recorded.at(1).op == QStringLiteral("plot.render"));
        fprintf(stdout, "[ok] 脚本操作被录制成 2 条业务意图的可回放 Job\n");

        // 3d) 回放：移除脚本数据集后重放 Job，得到相同 PlotSpec —— 闭环成立。
        //     稳定 id=100 让回放与会话状态无关，不影响主流程的 id=0 数据集。
        application.data().remove(100);
        assert(application.data().get(100) == nullptr);
        assert(application.data().get(id) != nullptr); // 主数据集仍在
        int replayedId = -1;
        QVariantMap replayedSpec;
        for (int i = 0; i < recorded.size(); ++i)
        {
            Result<IntentResult> rr = application.modules().execute(recorded.at(i));
            assert(rr);
            if (recorded.at(i).op == QStringLiteral("data.load"))
                replayedId = rr.value().payload.toInt();
            else
                replayedSpec = rr.value().payload.toMap();
        }
        assert(replayedId >= 0);
        assert(replayedSpec.value(QStringLiteral("column")).toString() == QStringLiteral("roll"));
        assert(replayedSpec.value(QStringLiteral("points")).toInt() == 3);
        fprintf(stdout, "[ok] Job 回放出相同 PlotSpec —— 脚本录制/回放闭环成立\n");
    }

    // 4) 卸载隔离：关掉绘图模块后，CSV 仍能加载，数据不消失
    assert(application.modules().deactivate(QStringLiteral("mod.plot")));
    assert(!application.modules().isActive(QStringLiteral("mod.plot")));
    Result<IntentResult> stillLoads = application.modules().execute(loadIntent(csv));
    assert(stillLoads);
    assert(application.data().get(id) != nullptr);
    fprintf(stdout, "[ok] 卸载 plot 模块后 CSV 仍可用，数据未丢失\n");

    // 5) 关掉 CSV 扩展后，data.load 不再可执行（能力随模块撤销）
    assert(application.modules().deactivate(QStringLiteral("ext.csv")));
    Result<IntentResult> noHandler = application.modules().execute(loadIntent(csv));
    assert(!noHandler);
    assert(noHandler.error().code == QStringLiteral("eunsupported"));
    fprintf(stdout, "[ok] 卸载 CSV 扩展后能力被精确撤销\n");

    // 5b) 格式知识包卸载隔离：CSV 卸载后 ULog 仍能加载；卸载 ULog 后 ulog 能力精确撤销。
    {
        qint64 firstUs = 0, lastUs = 0;
        const QString ulg = writeUlog(dir, 6000, &firstUs, &lastUs);
        Intent ui;
        ui.op = QStringLiteral("data.load");
        ui.params[QStringLiteral("path")] = ulg;
        ui.params[QStringLiteral("format")] = QStringLiteral("ulog");
        Result<IntentResult> stillLoads = application.modules().execute(ui); // CSV 已停，ULog 仍在
        assert(stillLoads);
        assert(application.modules().deactivate(QStringLiteral("ext.ulog")));
        Result<IntentResult> noUlog = application.modules().execute(ui);
        assert(!noUlog && noUlog.error().code == QStringLiteral("eunsupported"));
        fprintf(stdout, "[ok] 格式扩展互不干扰：停 CSV 不影响 ULog；停 ULog 后 ulog 能力精确撤销\n");
    }

    // 6) Job 序列化往返
    Job job;
    job.append(loadIntent(csv));
    job.append(renderIntent(id, QStringLiteral("demo.roll")));
    Result<void> parseErr;
    Job restored = Job::fromJson(job.toJson(), &parseErr);
    assert(parseErr);
    assert(restored.size() == 2);
    fprintf(stdout, "[ok] Job JSON 序列化往返\n");

    // 7) 百万行压力：验证流式加载 + 有界读取，C++ 侧内存与行数无关。
    //    用 20 万行（CI 友好）即可证明三条不变量；行数可放大到百万而不改代码路径。
    //    直接走 Dataset::appendFrame 分块灌入，模拟 CSV 扩展的加载方式。
    {
        constexpr qint64 kRows = 200000;
        constexpr int kBatch = 4096;
        Dataset big = Dataset::create(QStringLiteral("big"), QStringLiteral("stress"));
        QVector<TimeUs> tbuf(kBatch);
        QVector<double> vbuf(kBatch);
        QVector<FrameColumn> cols(1);
        cols[0].name = QStringLiteral("sig");
        cols[0].unit = QString();
        cols[0].type = ValueType::F64;
        cols[0].values = vbuf.constData();
        cols[0].nulls = nullptr;

        qint64 written = 0;
        while (written < kRows)
        {
            const int n = static_cast<int>(qMin<qint64>(kBatch, kRows - written));
            for (int i = 0; i < n; ++i)
            {
                const qint64 r = written + i;
                tbuf[i] = static_cast<TimeUs>(r) * 1000; // 每行 1ms → 微秒
                vbuf[i] = static_cast<double>(r % 1000);
            }
            QString err;
            const bool appended = big.appendFrame(QStringLiteral("trace"), tbuf.constData(), n, cols, &err);
            assert(appended && err.isEmpty());
            (void)appended; // Release(NDEBUG) 下 assert 为空，避免 unused 警告
            written += n;
        }
        big.refreshSchema();

        const Table *bt = big.table(QStringLiteral("trace"));
        assert(bt != nullptr && bt->rows == kRows);
        // 拷成局部值：Table* 指向 Dataset 内部 QVector，后续 channel() 等调用后不能假设仍有效。
        const TimeUs traceFirstUs = bt->firstTimeUs;
        const TimeUs traceLastUs = bt->lastTimeUs;
        assert(traceFirstUs == 0);
        assert(traceLastUs == (kRows - 1) * 1000);
        fprintf(stdout, "[ok] 流式灌入 %lld 行：schema 只存首尾时间，无 C++ 全量副本\n", kRows);

        // 有界读取：无论 20 万行，channel 默认只取 kDefaultWindowPoints（4096）点。
        const auto w = big.channel(QStringLiteral("trace.sig"));
        assert(w.has_value() && w->size() <= Dataset::kDefaultWindowPoints);
        assert(w->size() > 0);
        // 自定义更小的 cap 也严格生效。
        const auto w2 = big.channel(QStringLiteral("trace.sig"), /*cap=*/128);
        assert(w2.has_value() && w2->size() == 128);
        fprintf(stdout, "[ok] 20 万行有界读取：默认 %d 点 / 指定 128 点（与行数无关）\n",
                Dataset::kDefaultWindowPoints);

        // LOD：20 万行压到 800 个像素桶，三元组数恒定在 px 量级。
        const auto blod = big.lodMinMax(QStringLiteral("trace"), QStringLiteral("sig"),
                                        traceFirstUs, traceLastUs, /*px=*/800);
        assert(!blod.isEmpty() && blod.size() <= 800);
        fprintf(stdout, "[ok] 20 万行 LOD 抽稀到 %d 个三元组（px 有界）\n", blod.size());
    }

    fprintf(stdout, "\n全部架构契约测试通过\n");
    return 0;
}
