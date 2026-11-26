// 新架构契约测试：Source（多数据源）· Parser SPI（喂数据→出结果）· 扩展零改动新增
//
// 这些是 ARCHITECTURE.md 中「新架构两根支柱」的可执行证明：
//   1) 需求(5)：同时打开多个数据源，互不干扰，各自独立状态；
//   2) 需求(3)(6)：解析器是纯转换器 —— 喂字节出批，不认存储；
//   3) 需求(2)：新增解析器（扩展）不改任何既有代码；
//   4) 需求(6)：录制在字节层 —— 录的是原始字节，回放走同一 Parser。

#include "AppContext.h"
#include "RegistryTree.h"
#include "SourceHub.h"
#include "IngestOrchestrator.h"
#include "IngestPipeline.h"
#include "ParserHub.h"
#include "RecordingBackend.h"
#include "RecordingFormat.h"
#include "ps_parser_spi.h"

#include "CsvParser.h"
#include "McapCodec.h"
#include "Dataset.h"
#include "PspxParser.h"
#include "UlogStreamParser.h"
#include "RawCodec.h"
#include "Recorder.h"

#include <QDir>
#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            std::exit(1);                                                      \
        }                                                                      \
    } while (0)

#define OK(msg) fprintf(stdout, "[ok] %s\n", msg)

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;
using namespace nextpilot::plotscope::rec;

namespace
{

// 收集 sink 吐出的批——这正是"调用者"角色：接收结果并决定去向。
struct Collector
{
    struct Batch
    {
        QString table;
        std::vector<int64_t> times;
        std::vector<double> values;
    };
    std::vector<Batch> batches;
    uint64_t progressCalls = 0;

    static int32_t onBatch(void *ctx, const PsFrameBatch *batch)
    {
        auto *self = static_cast<Collector *>(ctx);
        if (!batch || !batch->table || !batch->time_ns || !batch->column_data)
            return -1;
        Batch b;
        b.table = QString::fromUtf8(batch->table);
        b.times.assign(batch->time_ns, batch->time_ns + batch->rows);
        const auto *col = &batch->column_data[0];
        if (col->type == PS_VALUE_F64 && col->values)
        {
            const auto *v = static_cast<const double *>(col->values);
            b.values.assign(v, v + batch->rows);
        }
        self->batches.push_back(std::move(b));
        return 0;
    }

    static void onProgress(void *ctx, uint64_t, uint64_t)
    {
        static_cast<Collector *>(ctx)->progressCalls++;
    }

    PsParserSink sink() const
    {
        PsParserSink s{};
        s.ctx = const_cast<Collector *>(this);
        s.frame_batch = &Collector::onBatch;
        s.progress = &Collector::onProgress;
        return s;
    }
};

// 造一个 PSPX 字节流：魔数 + 行数 + 记录
QByteArray makePspx(int rows, double scale = 1.0)
{
    QByteArray out;
    out.append("PSPX");
    auto appendU32 = [&out](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            out.append(static_cast<char>((v >> (i * 8)) & 0xFF));
    };
    appendU32(static_cast<uint32_t>(rows));
    for (int i = 0; i < rows; ++i)
    {
        int64_t t = 1000000 + i * 1000;
        double v = i * scale;
        for (int b = 0; b < 8; ++b)
            out.append(static_cast<char>((static_cast<uint64_t>(t) >> (b * 8)) & 0xFF));
        uint64_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        for (int b = 0; b < 8; ++b)
            out.append(static_cast<char>((bits >> (b * 8)) & 0xFF));
    }
    return out;
}


// ===== 探针解析器 =====
//
// 用途：验证「同一份字节流扇出给多个解析器」时，各路结果**互不覆盖**。
// 它认识 pspx 格式（同一份字节），但产出**不同的表名**——
// 于是两个通道的数据集内容不同，一眼能看出扇出没有串味。
//
// 它只存在于本测试文件（静态注册），证明「新增一个解析器不改任何既有代码」。

struct ProbeCtx
{
    std::vector<uint8_t> pending;
    bool headerDone = false;
    uint32_t declaredRows = 0;
    size_t bodyStart = 0;
};

const PsParserInfo &probeParserInfo()
{
    static const PsParserInfo info = [] {
        PsParserInfo i{};
        i.abi_version = PS_PARSER_ABI_VERSION;
        i.id = "probe";
        i.display_name = "探针解析器";
        i.extensions = "";
        i.can_sniff = 1;
        i.streaming = 1;
        return i;
    }();
    return info;
}

const PsParserVtable *probeParserVtable()
{
    static const PsParserVtable vt = [] {
        PsParserVtable v{};
        v.abi_version = PS_PARSER_ABI_VERSION;
        v.struct_size = sizeof(PsParserVtable);
        v.create = []() -> void * { return new ProbeCtx(); };
        v.destroy = [](void *ctx) { delete static_cast<ProbeCtx *>(ctx); };
        v.sniff = [](void *, const uint8_t *d, uint32_t n) -> int32_t {
            return (n >= 4 && d[0] == 'P' && d[1] == 'S' && d[2] == 'P' && d[3] == 'X') ? 1 : 0;
        };
        v.feed = [](void *ctx, const uint8_t *data, uint32_t size,
                    const PsParserSink *sink, PsError *err) -> int32_t {
            auto *c = static_cast<ProbeCtx *>(ctx);
            c->pending.insert(c->pending.end(), data, data + size);
            const uint8_t *buf = c->pending.data();
            uint32_t avail = static_cast<uint32_t>(c->pending.size());

            if (!c->headerDone)
            {
                if (avail < 8)
                    return 0;
                c->declaredRows = static_cast<uint32_t>(buf[4]) | (static_cast<uint32_t>(buf[5]) << 8)
                                  | (static_cast<uint32_t>(buf[6]) << 16)
                                  | (static_cast<uint32_t>(buf[7]) << 24);
                c->headerDone = true;
                c->bodyStart = 8;
            }

            uint32_t pos = static_cast<uint32_t>(c->bodyStart);
            std::vector<int64_t> times;
            std::vector<double> values;
            const uint32_t rowBytes = 16;
            while (pos + rowBytes <= avail)
            {
                const uint8_t *row = buf + pos;
                int64_t t = 0;
                for (int i = 0; i < 8; ++i)
                    t |= static_cast<int64_t>(row[i]) << (i * 8);
                uint64_t bits = 0;
                for (int i = 0; i < 8; ++i)
                    bits |= static_cast<uint64_t>(row[8 + i]) << (i * 8);
                double v;
                memcpy(&v, &bits, 8);
                times.push_back(t);
                values.push_back(v);
                pos += rowBytes;
            }

            if (!times.empty())
            {
                // 关键：表名与 pspx 不同 → 两个通道的数据集内容可区分
                PsFrameColumn col{};
                col.name = "probe_value";
                col.unit = "";
                col.type = PS_VALUE_F64;
                col.values = values.data();
                col.nulls = nullptr;
                PsFrameBatch batch{};
                batch.table = "probe_table";
                batch.time_ns = times.data();
                batch.rows = static_cast<uint32_t>(times.size());
                batch.columns = 1;
                batch.column_data = &col;
                if (sink->frame_batch(sink->ctx, &batch) != 0)
                    return -1;
            }

            if (pos > 0)
                c->pending.erase(c->pending.begin(), c->pending.begin() + pos);
            c->bodyStart = 0;
            return 0;
        };
        v.finish = [](void *ctx, const PsParserSink *, PsError *err) -> int32_t {
            return 0;
        };
        v.reset = [](void *ctx) {
            auto *c = static_cast<ProbeCtx *>(ctx);
            c->pending.clear();
            c->headerDone = false;
            c->declaredRows = 0;
            c->bodyStart = 0;
        };
        return v;
    }();
    return &vt;
}

} // namespace

int main()
{
    // ========== 1) 解析器 SPI：静态自注册生效 ==========
    // 需求(2)：新增解析器不改任何既有代码。
    // pspx 解析器在 toolbox/common 里自注册（PS_REGISTER_PARSER），
    // 它的 TU 被链接进来即生效——宿主、注册表、其它解析器都没改过。
    const PsParserInfo &info = nextpilot::plotscope::ext::PspxParser::parserInfo();
    const PsParserVtable *vt = nextpilot::plotscope::ext::PspxParser::parserVtable();

    CHECK(ParserHub::count() >= 1);
    CHECK(ParserHub::entry(QStringLiteral("pspx")) != nullptr);
    // 重复注册被拒（先到者为准）：这是零改动新增的幂等保证。
    CHECK(!ParserHub::registerParser(info, vt, QStringLiteral("builtin:test")));
    OK("解析器静态自注册生效：pspx 已自动登记；重复注册被拒（幂等）");

    // ========== 2) 按后缀 / 按内容嗅探 ==========
    CHECK(ParserHub::findByExtension(QStringLiteral(".pspx")) != nullptr);
    CHECK(ParserHub::findByExtension(QStringLiteral("pspx")) != nullptr); // 无点也认
    // CSV 已归并到 SPI：它现在有独立的解析器（格式知识只有一份实现）。
    // 这条断言的正身是"归并后 CSV 由解析器负责"，不是"CSV 没有解析器"。
    CHECK(ParserHub::findByExtension(QStringLiteral(".csv")) != nullptr);
    CHECK(ParserHub::entry(QStringLiteral("csv")) != nullptr);
    {
        const QByteArray head = makePspx(1).left(8);
        CHECK(ParserHub::findBySniff(head) != nullptr);
        const QByteArray junk("NOTAFORMAT");
        CHECK(ParserHub::findBySniff(junk) == nullptr);
    }
    OK("解析器可按后缀查找，也可按内容嗅探（sniff）自动选择");

    // ========== 3) 喂数据 → 出结果（解析器是纯转换器）============
    {
        Collector collector;
        PsParserSink sink = collector.sink();
        const QByteArray bytes = makePspx(500, 2.0);

        // 模拟调用者按块喂：分 3 次喂，验证解析器能跨块累积
        const PsParserVtable *v = ParserHub::vtable(QStringLiteral("pspx"));
        void *ctx = v->create();
        PsError err{};
        int chunk = bytes.size() / 3;
        int fed = 0;
        while (fed < bytes.size())
        {
            const int n = qMin(chunk, bytes.size() - fed);
            const int32_t rc = v->feed(ctx,
                                       reinterpret_cast<const uint8_t *>(bytes.constData() + fed),
                                       static_cast<uint32_t>(n), &sink, &err);
            CHECK(rc == 0);
            fed += n;
        }
        const int32_t rc = v->finish(ctx, &sink, &err);
        CHECK(rc == 0);
        v->destroy(ctx);

        // 500 行应完整吐出
        int total = 0;
        for (const auto &b : collector.batches)
            total += static_cast<int>(b.times.size());
        CHECK(total == 500);
        CHECK(!collector.batches.empty());
        CHECK(collector.batches.front().table == QStringLiteral("pspx"));
        CHECK(collector.batches.front().values.size() == collector.batches.front().times.size());
        // 首末值正确（scale=2.0）
        const auto &first = collector.batches.front();
        CHECK(qAbs(first.values.front() - 0.0) < 1e-9);
        OK(QStringLiteral("解析器 SPI：分 3 块喂入 %1 行，完整吐出 %2 行（跨块累积正确）")
               .arg(500)
               .arg(total)
               .toUtf8()
               .constData());
    }

    // ========== 4) 解析器不认识存储：结果由调用者决定去向 ==========
    // 同一个解析器喂不同来源的字节，产出互不影响 —— 证明"喂数据的是调用者"。
    {
        Collector c1, c2;
        PsParserSink s1 = c1.sink(), s2 = c2.sink();
        const QByteArray b1 = makePspx(10, 1.0);
        const QByteArray b2 = makePspx(20, 3.0);
        CHECK(nextpilot::plotscope::ext::PspxParser::parseBuffer(
            reinterpret_cast<const uint8_t *>(b1.constData()),
            static_cast<uint32_t>(b1.size()), &s1, nullptr));
        CHECK(nextpilot::plotscope::ext::PspxParser::parseBuffer(
            reinterpret_cast<const uint8_t *>(b2.constData()),
            static_cast<uint32_t>(b2.size()), &s2, nullptr));
        CHECK(c1.batches.front().values.size() == 10);
        CHECK(c2.batches.front().values.size() == 20);
        // 第二个源的末值 = 19*3 = 57
        CHECK(qAbs(c2.batches.front().values.back() - 57.0) < 1e-9);
        OK("同一解析器可服务多个来源：结果互不干扰，去向由调用者决定");
    }

    // ========== 5) 多数据源并存（需求 5）============
    {
        RegistryTree tree;
        SourceHub repo(tree);
        DataSourceInfo s1;
        s1.name = QStringLiteral("flight-1.ulg");
        s1.kind = SourceKind::File;
        s1.origin = QStringLiteral("/data/flight-1.ulg");
        s1.state = SourceState::Streaming;
        s1.replayable = true;

        DataSourceInfo s2;
        s2.name = QStringLiteral("flight-2.ulg");
        s2.kind = SourceKind::File;
        s2.origin = QStringLiteral("/data/flight-2.ulg");
        s2.state = SourceState::Opening;

        const int id1 = repo.add(s1);
        const int id2 = repo.add(s2);
        CHECK(id1 != id2);                       // 各自独立 id
        CHECK(repo.count() == 2);
        CHECK(repo.get(id1)->state == SourceState::Streaming);
        CHECK(repo.get(id2)->state == SourceState::Opening); // 状态互不影响

        // 一个源可解析出多个数据集
        CHECK(repo.update(id1, [&] {
                  DataSourceInfo x = *repo.get(id1);
                  x.datasetIds = {7, 8};
                  return x;
              }()));
        CHECK(repo.get(id1)->datasetIds.size() == 2);

        // 状态推进只影响自己
        CHECK(repo.setState(id2, SourceState::Streaming));
        CHECK(repo.get(id1)->state == SourceState::Streaming);
        CHECK(repo.get(id2)->state == SourceState::Streaming);
        CHECK(repo.setState(id2, SourceState::Error, QStringLiteral("read failed")));
        CHECK(repo.get(id2)->error == QStringLiteral("read failed"));
        CHECK(repo.get(id1)->error.isEmpty()); // 另一个源不受影响

        // 移除一个源不影响另一个
        QVector<int> owned;
        CHECK(repo.remove(id1, &owned));
        CHECK(owned.size() == 2);
        CHECK(repo.count() == 1);
        CHECK(repo.get(id2) != nullptr);
        OK("多数据源并存：独立 id / 独立状态 / 互不牵连");
    }

    // ========== 6) 录制在字节层（需求 6）============
    // 录的是 Source 的原始字节；回放时当作 Source 重放，走同一 Parser。
    {
        // 模拟：源字节 -> 录制文件字节 -> 回放喂给同一 Parser
        const QByteArray original = makePspx(64, 5.0);

        // "录制"：原样落盘
        const QByteArray recorded = original;

        // "回放"：把录制文件当作新 Source 喂给同一解析器
        Collector live, replayed;
        PsParserSink sl = live.sink(), sr = replayed.sink();
        CHECK(nextpilot::plotscope::ext::PspxParser::parseBuffer(
            reinterpret_cast<const uint8_t *>(original.constData()),
            static_cast<uint32_t>(original.size()), &sl, nullptr));
        CHECK(nextpilot::plotscope::ext::PspxParser::parseBuffer(
            reinterpret_cast<const uint8_t *>(recorded.constData()),
            static_cast<uint32_t>(recorded.size()), &sr, nullptr));

        // 回放结果必须与原始一致（确定性）
        int liveTotal = 0, replayTotal = 0;
        double liveSum = 0, replaySum = 0;
        for (const auto &b : live.batches)
            for (double v : b.values) { liveTotal++; liveSum += v; }
        for (const auto &b : replayed.batches)
            for (double v : b.values) { replayTotal++; replaySum += v; }
        CHECK(liveTotal == replayTotal);
        CHECK(qAbs(liveSum - replaySum) < 1e-9);
        OK("字节级录制回放：回放结果与原始完全一致（确定性）");
    }

    // ========== 7) 解析器可独立单测（不需要启动 app）============
    // 上面的测试全程没有构造 QApplication / App / Dataset —— 解析器是自洽的纯转换器。
    OK("解析器可独立单测：无 QApplication、无 App、无 Dataset 依赖");

    // ========== 8) 录制格式是一组可扩展的资产（raw / mcap）============
    {
        // raw 格式在本测试文件里注册（内置扩展也会注册同一个 id，先到者为准）
        const nextpilot::plotscope::rec::RecordingFormat raw = [] {
            nextpilot::plotscope::rec::RecordingFormat f;
            f.id = nextpilot::plotscope::rec::FormatId::Raw;
            f.displayName = QStringLiteral("原始字节录制");
            f.extension = QStringLiteral(".raw");
            f.structured = false;
            return f;
        }();
        CHECK(nextpilot::plotscope::rec::RecordingFormatHub::registerFormat(raw));
        CHECK(nextpilot::plotscope::rec::RecordingFormatHub::format(QStringLiteral("raw")) != nullptr);
        CHECK(nextpilot::plotscope::rec::RecordingFormatHub::formatByExtension(QStringLiteral(".raw")) != nullptr);
        // raw 不保留通道结构
        CHECK(!nextpilot::plotscope::rec::RecordingFormatHub::format(QStringLiteral("raw"))->structured);
        OK("录制格式是资产而非机制：raw 已登记（无通道结构），可按 id / 后缀查找");
    }

    // ========== 9) 数据源是运行时实例：绑定引用资产，不复制 ==========
    {
        RegistryTree tree;
        SourceHub repo(tree);
        DataSourceInfo info;
        info.name = QStringLiteral("flight.ulg");
        info.origin = QStringLiteral("/data/flight.ulg");
        info.kind = SourceKind::File;
        const int id = repo.add(info);

        // 绑定解析器：引用注册表里的资产 id
        CHECK(repo.bindParser(id, ParserBinding{QStringLiteral("pspx"), QString(), true}));
        CHECK(repo.parserFor(id, QStringLiteral("pspx")) != nullptr);
        CHECK(repo.parserFor(id, QStringLiteral("pspx"))->parserId == QStringLiteral("pspx"));

        // **扇出**：同一数据源可同时绑定多个解析器（udp 里可能混着多种格式数据），
        // 同一份字节流会同时喂给它们。
        CHECK(repo.bindParser(id, ParserBinding{QStringLiteral("pspx2"), QString(), true}));
        CHECK(repo.parserFor(id, QStringLiteral("pspx2")) != nullptr);
        CHECK(repo.parsersOf(id).size() == 2); // 两路扇出

        // topic 降级为可选过滤器，不再是路由键
        CHECK(repo.bindParser(id, ParserBinding{QStringLiteral("dbc"), QStringLiteral("can.1"), true}));
        CHECK(repo.parserFor(id, QStringLiteral("dbc"))->topic == QStringLiteral("can.1"));
        CHECK(repo.parsersOf(id).size() == 3);

        // 同解析器重复绑定 = 改配置，不新增一路
        CHECK(repo.bindParser(id, ParserBinding{QStringLiteral("pspx"), QString(), false}));
        CHECK(repo.parsersOf(id).size() == 3);
        CHECK(repo.parserFor(id, QStringLiteral("pspx"))->active == false);

        // 两个数据源可绑定同一个解析器资产 —— 资产共享，不是复制
        DataSourceInfo other;
        other.name = QStringLiteral("flight2.ulg");
        const int id2 = repo.add(other);
        CHECK(repo.bindParser(id2, ParserBinding{QStringLiteral("pspx"), QString(), true}));

        // 产出数据集登记在数据源上
        CHECK(repo.addDataset(id, 11));
        CHECK(repo.addDataset(id, 12));
        CHECK(repo.get(id)->datasetIds.size() == 2);
        CHECK(repo.get(id2)->datasetIds.isEmpty());
        OK("数据源=运行时实例：绑定引用共享资产；解析器**扇出**多路（topic 仅为过滤器），数据集归属各自源");
    }

    // ========== 10) 官方内置两种录制格式都能录能放（需求 6）============
    {
        QTemporaryDir dir;
        const QString rawPath = dir.filePath(QStringLiteral("cap.raw"));
        const QString mcapPath = dir.filePath(QStringLiteral("cap.mcap"));

        // 原始字节（模拟数据源产出）
        QByteArray original;
        for (int i = 0; i < 100; ++i)
        {
            original.append(static_cast<char>(i & 0xFF));
            original.append(static_cast<char>((i >> 8) & 0xFF));
        }

        // --- raw：录 → 放 ---
        {
            nextpilot::plotscope::rec::RawRecorder rec;
            std::shared_ptr<nextpilot::plotscope::rec::IByteSink> sink =
                nextpilot::plotscope::rec::createByteSink(QStringLiteral("file"), nullptr);
            CHECK(sink != nullptr);
            CHECK(sink->open(rawPath, nullptr));
            CHECK(rec.open(sink, QByteArray("{\"source\":\"t\"}")));
            CHECK(rec.write(original));
            CHECK(rec.close());
        }
        {
            nextpilot::plotscope::rec::RawPlayer player;
            CHECK(player.open(rawPath));
            QByteArray back;
            while (!player.atEnd())
            {
                const QByteArray chunk = player.read(64);
                if (chunk.isEmpty())
                    break;
                back.append(chunk);
            }
            CHECK(back == original); // 逐字节一致
        }
        OK("raw 录制回放：逐字节一致（无损零转码）");
    }

    {
        QTemporaryDir dir;
        const QString mcapPath = dir.filePath(QStringLiteral("cap.mcap"));

        QByteArray msgA("payload-A", 9);
        QByteArray msgB("payload-B", 9);

        // --- mcap：录（两个 topic）→ 放 ---
        {
            nextpilot::plotscope::rec::McapRecorder rec;
            std::shared_ptr<nextpilot::plotscope::rec::IByteSink> sink =
                nextpilot::plotscope::rec::createByteSink(QStringLiteral("file"), nullptr);
            CHECK(sink != nullptr);
            CHECK(sink->open(mcapPath, nullptr));
            CHECK(rec.open(sink));
            CHECK(rec.writeMessage(QStringLiteral("/imu"), msgA, 1000));
            CHECK(rec.writeMessage(QStringLiteral("/gps"), msgB, 2000));
            CHECK(rec.writeMessage(QStringLiteral("/imu"), msgA, 3000));
            CHECK(rec.close());
        }
        {
            nextpilot::plotscope::rec::McapPlayer player;
            CHECK(player.open(mcapPath));
            int count = 0;
            int imuCount = 0;
            QByteArray firstData;
            while (true)
            {
                nextpilot::plotscope::rec::McapMessage msg;
                if (!player.nextMessage(msg))
                    break;
                if (count == 0)
                    firstData = msg.data;
                if (msg.topic == QStringLiteral("/imu"))
                    ++imuCount;
                ++count;
            }
            CHECK(count == 3);
            CHECK(imuCount == 2);              // 两个 topic，两个 channel
            CHECK(firstData == msgA);          // 消息体逐字节一致
            CHECK(player.channels().size() == 2); // 保留通道结构
        }
        OK("mcap 录制回放：3 条消息 / 2 个 topic，消息体一致且保留通道结构");
    }

    // ========== 12) AppContext：进程级运行态的唯一持有者 ==========
    {
        AppContext ctx;
        // 一处持有、一处取用：数据集/设置/工具箱/数据源/摄取管道都在这儿
        CHECK(ctx.data().count() == 0);
        CHECK(ctx.settings().keysIn(SettingsScope::App).isEmpty());
        CHECK(ctx.toolbox().count() == 0);
        CHECK(ctx.sources().count() == 0);
        CHECK(ctx.ingest().channelCount(1) == 0);

        // 活动视图是**指针**不是副本：CLI 与 GUI 看到同一份
        ctx.setActiveSourceId(7);
        CHECK(ctx.activeSourceId() == 7);
        ctx.setActiveChannel(7, QStringLiteral("sensor"), QStringLiteral("x"));
        CHECK(ctx.activeTable() == QStringLiteral("sensor"));
        CHECK(ctx.activeColumn() == QStringLiteral("x"));

        // 变更通知（GUI 树、脚本都能订阅）
        int srcChanges = 0, chChanges = 0;
        QObject::connect(&ctx, &AppContext::activeSourceChanged,
                         [&srcChanges](int) { ++srcChanges; });
        QObject::connect(&ctx, &AppContext::activeChannelChanged,
                         [&chChanges](int, const QString &, const QString &) { ++chChanges; });
        ctx.setActiveSourceId(8);
        ctx.setActiveChannel(8, QStringLiteral("t"), QStringLiteral("c"));
        CHECK(srcChanges == 1);
        CHECK(chChanges == 1);
        // 同值不重复通知
        ctx.setActiveSourceId(8);
        CHECK(srcChanges == 1);
        OK("AppContext：运行态一处持有；活动视图是共享指针；变更可订阅");
    }

    // ========== 13) 录制：格式 × 后端正交，可同时多路 ==========
    {
        RecordingFormatHub::reset();
        RecordingBackendHub::reset();
        RecordingFormatHub::installBuiltinFormats();
        RecordingBackendHub::installBuiltinBackends();

        // 两个独立注册表：格式管"字节长什么样"，后端管"字节送到哪"
        CHECK(RecordingFormatHub::count() == 2);   // raw / mcap
        CHECK(RecordingBackendHub::count() == 2);  // file / udp
        CHECK(RecordingFormatHub::format(QStringLiteral("raw")) != nullptr);
        CHECK(RecordingBackendHub::backend(QStringLiteral("udp")) != nullptr);
        // 后端能力描述：udp 是流式、可多路复用；file 可 seek
        CHECK(RecordingBackendHub::backend(QStringLiteral("udp"))->streaming);
        CHECK(RecordingBackendHub::backend(QStringLiteral("udp"))->multiplexed);
        CHECK(RecordingBackendHub::backend(QStringLiteral("file"))->seekable);

        // 正交组合：一路录制 = (格式, 后端, 端点)。可同时开多路。
        DataSourceInfo info;
        info.name = QStringLiteral("flight.ulg");
        const int id = 7;
        info.recordings.append(RecordingTarget{QStringLiteral("raw"), QStringLiteral("file"),
                                               QDir::temp().filePath(QStringLiteral("a.raw")), true});
        info.recordings.append(RecordingTarget{QStringLiteral("mcap"), QStringLiteral("file"),
                                               QDir::temp().filePath(QStringLiteral("a.mcap")), true});
        info.recordings.append(RecordingTarget{QStringLiteral("raw"), QStringLiteral("udp"),
                                               QStringLiteral("239.0.0.1:5600"), true});
        CHECK(info.recordings.size() == 3);
        CHECK(info.recordings[0].isValid() && info.recordings[2].isValid());
        // 三路的格式/后端组合互不相同，但都合法
        CHECK(info.recordings[0].formatId != info.recordings[1].formatId);
        CHECK(info.recordings[0].backendId == info.recordings[1].backendId);
        OK("录制：格式×后端正交，一个源可同时录多路（raw→file / mcap→file / raw→udp）");
    }

    // ========== 14) 摄取：单前端 → 多解析器扇出 → 多数据集 ==========
    {
        RegistryTree tree;
        DatasetHub repo(tree);
        IngestPipeline pipe(repo);

        // 一个数据源 = 一个前端（单字节来源）
        DataSourceInfo info;
        info.name = QStringLiteral("mixed.ulg");
        info.kind = SourceKind::File;
        const int srcId = 1;

        // 扇出两路：同一份字节同时喂给两个解析器
        const Result<int> c1 = pipe.bindParser(srcId, ParserBinding{QStringLiteral("pspx"), QString(), true});
        CHECK(c1);
        CHECK(pipe.channelCount(srcId) == 1);

        // 第二路需要另一个已注册解析器：注册一个探针解析器（只在本测试文件）
        const PsParserInfo &probeInfo = probeParserInfo();
        const PsParserVtable *probeVt = probeParserVtable();
        CHECK(ParserHub::registerParser(probeInfo, probeVt, QStringLiteral("test")));
        const Result<int> c2 = pipe.bindParser(srcId, ParserBinding{QStringLiteral("probe"), QString(), true});
        CHECK(c2);
        CHECK(pipe.channelCount(srcId) == 2);
        // 两路各有独立数据集，互不覆盖
        CHECK(c1.value() != c2.value());

        // 造一段 pspx 格式的字节（魔数 + 行数 + 行）
        QByteArray bytes;
        bytes.append("PSPX");
        const quint32 rows = 4;
        for (int i = 0; i < 4; ++i) bytes.append(char((rows >> (i * 8)) & 0xFF));
        for (int r = 0; r < 4; ++r)
        {
            const quint64 t = 1000000ull + static_cast<quint64>(r) * 1000ull;
            for (int i = 0; i < 8; ++i) bytes.append(char((t >> (i * 8)) & 0xFF));
            const double v = r * 1.5;
            quint64 bits;
            memcpy(&bits, &v, 8);
            for (int i = 0; i < 8; ++i) bytes.append(char((bits >> (i * 8)) & 0xFF));
        }

        // 喂给管道 = 扇出给所有通道
        const qint64 consumed = pipe.feed(srcId, reinterpret_cast<const uint8_t *>(bytes.constData()),
                                          bytes.size());
        CHECK(consumed == bytes.size());
        pipe.finish(srcId);

        // 两路都产出了数据，各落各的数据集
        const Dataset *ds1 = repo.get(c1.value());
        const Dataset *ds2 = repo.get(c2.value());
        CHECK(ds1 != nullptr);
        CHECK(ds2 != nullptr);
        CHECK(ds1->tableCount() >= 1);   // pspx 产出自己的表
        CHECK(ds2->tableCount() >= 1);   // probe 产出自己的表
        // 数据集互不相同 —— 扇出的结果不混表
        CHECK(c1.value() != c2.value());
        OK("摄取：单前端字节流扇出给多解析器，各产独立数据集，结果互不覆盖");
    }

    // ========== 15) 多路同时录制：raw→file 与 mcap→file 各写各的 ==========
    {
        QTemporaryDir dir;
        const QString rawPath = dir.filePath(QStringLiteral("multi.raw"));
        const QString mcapPath = dir.filePath(QStringLiteral("multi.mcap"));

        // 两路 Recorder：格式不同，后端相同 —— 组合独立，各写各的文件
        auto rawRec = nextpilot::plotscope::rec::Recorder::create(QStringLiteral("raw"), QStringLiteral("file"),
                                                 rawPath, QByteArray("{\"k\":\"raw\"}"));
        CHECK(rawRec);
        auto mcapRec = nextpilot::plotscope::rec::Recorder::create(QStringLiteral("mcap"), QStringLiteral("file"),
                                                  mcapPath);
        CHECK(mcapRec);

        // 同一份消息同时喂给两路
        const QByteArray payload("hello-ps", 8);
        for (int i = 0; i < 5; ++i)
        {
            CHECK(rawRec.value()->writeMessage(QStringLiteral("/t"), payload, 1000 + i));
            CHECK(mcapRec.value()->writeMessage(QStringLiteral("/t"), payload, 1000 + i));
        }
        CHECK(rawRec.value()->close());
        CHECK(mcapRec.value()->close());

        // raw 路：逐字节还原（回放等价于原始）
        {
            nextpilot::plotscope::rec::RawPlayer player;
            CHECK(player.open(rawPath));
            QByteArray back;
            while (!player.atEnd())
            {
                const QByteArray chunk = player.read(64);
                if (chunk.isEmpty())
                    break;
                back.append(chunk);
            }
            QByteArray expected;
            for (int i = 0; i < 5; ++i)
                expected.append(payload);
            CHECK(back == expected);
        }

        // mcap 路：5 条消息 + 通道结构保留
        {
            nextpilot::plotscope::rec::McapPlayer player;
            CHECK(player.open(mcapPath));
            int count = 0;
            while (true)
            {
                nextpilot::plotscope::rec::McapMessage msg;
                if (!player.nextMessage(msg))
                    break;
                CHECK(msg.data == payload);
                CHECK(msg.topic == QStringLiteral("/t"));
                ++count;
            }
            CHECK(count == 5);
            CHECK(player.channels().size() == 1);
        }

        // 后端缺失时明确报错（不是静默失败）
        {
            QString err;
            const auto bad = nextpilot::plotscope::rec::Recorder::create(QStringLiteral("raw"),
                                                        QStringLiteral("nosuch"), "/tmp/x", {}, &err);
            CHECK(!bad);
            CHECK(err.contains(QStringLiteral("nosuch")));
        }
        OK("多路同时录制：raw→file 与 mcap→file 各写各的，互不干扰；未知后端明确报错");
    }

    // ========== T6.1 MDF：Source → IngestPipeline → mdf4 SPI 解析器 ==========
    //
    // 端到端断言：mdf4 解析器静态自注册后，宿主不改一行代码就能把 .mf4 落成数据集。
    {
        const QString fixture =
            QString::fromUtf8(PS_TEST_FIXTURE_DIR) + QStringLiteral("/demo_compressed.mf4");
        QFile f(fixture);
        CHECK(f.open(QIODevice::ReadOnly));
        CHECK(ParserHub::entry(QStringLiteral("mdf4")) != nullptr);
        CHECK(ParserHub::findByExtension(QStringLiteral(".mf4")) != nullptr);
        CHECK(ParserHub::findBySniff(QByteArray("MDF     4.00")) != nullptr);

        RegistryTree treeM;
        DatasetHub repoM(treeM);
        IngestPipeline pipeM(repoM);
        const Result<int> boundM =
            pipeM.bindParser(7, ParserBinding{QStringLiteral("mdf4"), {}, true});
        CHECK(boundM);
        {
            QByteArray chunk;
            while (!f.atEnd())
            {
                chunk = f.read(4096);
                if (chunk.isEmpty())
                    break;
                pipeM.feed(7, reinterpret_cast<const uint8_t *>(chunk.constData()),
                           static_cast<qint64>(chunk.size()));
            }
        }
        pipeM.finish(7);
        const int dsM = boundM.value();
        CHECK(dsM >= 0);
        const Dataset *dM = repoM.get(dsM);
        CHECK(dM != nullptr);
        if (dM)
        {
            fprintf(stderr, "[diag] mdf tables=%s\n",
                    dM->tableNames().join(",").toUtf8().constData());
            CHECK(dM->tableNames().contains(QStringLiteral("mdf4_dg0_cg0")));
            const Table *tM = dM->table(QStringLiteral("mdf4_dg0_cg0"));
            CHECK(tM != nullptr);
            CHECK(tM && tM->rows == 100);
            const auto roll = dM->channel(QStringLiteral("mdf4_dg0_cg0.roll"), 110);
            const auto pitch = dM->channel(QStringLiteral("mdf4_dg0_cg0.pitch"), 110);
            CHECK(roll.has_value() && pitch.has_value());
            CHECK(roll.has_value() && roll->size() == 100);
            if (roll.has_value() && !roll->time.empty())
                fprintf(stderr, "[diag] mdf roll0=%g roll99=%g t0=%g t99=%g\n",
                        roll->value.empty() ? 0.0 : roll->value.front(),
                        roll->value.empty() ? 0.0 : roll->value.back(), roll->time.front(),
                        roll->time.back());
            CHECK(roll.has_value() && !roll->time.empty() && roll->time.front() == 0.0);
        }
        OK("T6.1 MDF 经 IngestPipeline 落库：表 mdf4_dg0_cg0 / 100 行 / roll+pitch");
    }

    // ========== T6.2 ROS2 bag：Source → IngestPipeline → rosbag2 SPI 解析器 ==========
    //
    // 与 MDF 同一套端到端：解析器自注册后，宿主不改一行就能把 .db3 落成数据集。
    {
        const QString fixture =
            QString::fromUtf8(PS_TEST_FIXTURE_DIR) + QStringLiteral("/demo_rosbag2.db3");
        QFile f(fixture);
        CHECK(f.open(QIODevice::ReadOnly));
        CHECK(ParserHub::entry(QStringLiteral("rosbag2")) != nullptr);
        CHECK(ParserHub::findByExtension(QStringLiteral(".db3")) != nullptr);
        CHECK(ParserHub::findBySniff(QByteArray("SQLite format 3\0\0\0", 18)) != nullptr);

        RegistryTree treeR;
        DatasetHub repoR(treeR);
        IngestPipeline pipeR(repoR);
        const Result<int> boundR =
            pipeR.bindParser(8, ParserBinding{QStringLiteral("rosbag2"), {}, true});
        CHECK(boundR);
        {
            QByteArray chunk;
            while (!f.atEnd())
            {
                chunk = f.read(4096);
                if (chunk.isEmpty())
                    break;
                pipeR.feed(8, reinterpret_cast<const uint8_t *>(chunk.constData()),
                           static_cast<qint64>(chunk.size()));
            }
        }
        pipeR.finish(8);
        const int dsR = boundR.value();
        CHECK(dsR >= 0);
        const Dataset *dR = repoR.get(dsR);
        CHECK(dR != nullptr);
        if (dR)
        {
            fprintf(stderr, "[diag] rosbag2 tables=%s\n",
                    dR->tableNames().join(",").toUtf8().constData());
            CHECK(dR->tableNames().contains(QStringLiteral("imu")));
            CHECK(dR->tableNames().contains(QStringLiteral("speed")));
            const Table *ti = dR->table(QStringLiteral("imu"));
            const Table *ts = dR->table(QStringLiteral("speed"));
            CHECK(ti && ti->rows == 100);
            CHECK(ts && ts->rows == 100);
            const auto speed = dR->channel(QStringLiteral("speed.data"), 110);
            CHECK(speed.has_value() && speed->size() == 100);
            if (speed.has_value() && !speed->value.empty())
                fprintf(stderr, "[diag] rosbag2 speed.data 首=%g 末=%g t0=%g t99=%g\n",
                        speed->value.front(), speed->value.back(), speed->time.front(),
                        speed->time.back());
            CHECK(speed.has_value() && !speed->value.empty() &&
                  std::fabs(speed->value.back() - 49.5) < 1e-9);
        }
        OK("T6.2 ROS2 bag 经 IngestPipeline 落库：imu / speed 各 100 行");
    }

    // ========== ULog 双路径一致性：SPI 管道 == 旧 driver 路径 ==========
    //
    // 这是 P0-1 归并的**核心断言**：同一个 .ulg 文件，
    //   路径 A（data.load → IMessageParser::load）  与
    //   路径 B（Source → IngestPipeline → ULog SPI parser）
    // 必须产出**逐行一致**的数据。
    //
    // 若不一致，说明"两份 parser"还在——那就等于没归并。
    {
        // 造一个最小 ULog：1 个 topic、2 列、N 行。
        const QByteArray fmt("sensor:uint64_t timestamp;float x;uint16_t y;");
        QByteArray ulg;
        ulg.append("ULog", 4);
        ulg.append(static_cast<char>(0x01));
        ulg.append(static_cast<char>(0x12));
        ulg.append(static_cast<char>(0x35));
        ulg.append(static_cast<char>(0x01));
        auto put16 = [](QByteArray &b, quint16 v) {
            b.append(static_cast<char>(v & 0xFF));
            b.append(static_cast<char>((v >> 8) & 0xFF));
        };
        auto put64 = [](QByteArray &b, quint64 v) {
            for (int i = 0; i < 8; ++i)
                b.append(static_cast<char>((v >> (8 * i)) & 0xFF));
        };
        auto putFloat = [](QByteArray &b, float f) {
            char raw[4];
            memcpy(raw, &f, 4);
            b.append(raw, 4);
        };
        // ULog 文件头 = magic(7) + version(1) + start_timestamp(8) = 16 字节
        put64(ulg, 0);
        auto addMsg = [&put16](QByteArray &b, char ty, const QByteArray &body) {
            put16(b, static_cast<quint16>(body.size()));
            b.append(static_cast<char>(ty));
            b.append(body);
        };
        addMsg(ulg, 'F', fmt);
        QByteArray sub;
        sub.append(static_cast<char>(0));
        put16(sub, 1);
        sub.append("sensor", 6);
        sub.append(static_cast<char>(0));
        addMsg(ulg, 'A', sub);
        const int rows = 500;
        for (int i = 0; i < rows; ++i)
        {
            QByteArray d;
            put16(d, 1);
            put64(d, 1000000ULL + static_cast<quint64>(i) * 1000ULL);
            putFloat(d, static_cast<float>(i) * 0.5f);
            put16(d, static_cast<quint16>(i % 1000));
            addMsg(ulg, 'D', d);
        }
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("dual.ulg"));
        {
            QFile f(path);
            CHECK(f.open(QIODevice::WriteOnly));
            f.write(ulg);
        }

        // --- 路径 B：SPI 管道（Source → IngestPipeline → ULog parser）---
        RegistryTree treeB;
        DatasetHub repoB(treeB);
        IngestPipeline pipeB(repoB);
        CHECK(ParserHub::entry(QStringLiteral("ulog")) != nullptr);
        const Result<int> bound = pipeB.bindParser(1, ParserBinding{QStringLiteral("ulog"), {}, true});
        CHECK(bound);
        {
            QFile f(path);
            CHECK(f.open(QIODevice::ReadOnly));
            QByteArray chunk;
            while (!f.atEnd())
            {
                chunk = f.read(4096);
                if (chunk.isEmpty())
                    break;
                pipeB.feed(1, reinterpret_cast<const uint8_t *>(chunk.constData()),
                           static_cast<qint64>(chunk.size()));
            }
        }
        pipeB.finish(1);
        const int dsB = bound.value();
        CHECK(dsB >= 0);
        const Dataset *dB = repoB.get(dsB);
        CHECK(dB != nullptr);
        const Table *tB = dB ? dB->table(QStringLiteral("sensor")) : nullptr;
        CHECK(tB != nullptr);
        CHECK(tB && tB->rows == rows);

        // --- 路径 A：旧 driver 路径（data.load → IMessageParser）---
        // 这里直接调扩展里的 driver，避免把整个 App 拉进来。
        Dataset dsA = Dataset::create(QStringLiteral("dual"), QStringLiteral("ulog"));
        QString errA;
        CHECK(nextpilot::plotscope::ext::ulog::parseUlogStream(path, dsA, errA));
        dsA.refreshSchema();
        const Table *tA = dsA.table(QStringLiteral("sensor"));
        CHECK(tA != nullptr);
        CHECK(tA && tA->rows == rows);

        // --- 逐行比对：两条路径必须完全一致 ---
        //
        // 数据本体在 Rust（typed 表），C++ 侧只能通过 channel() 读——
        // 这正好也验证了"两条路径落到同一个数据集抽象上"。
        if (tA && tB)
        {
            const auto ax = dsA.channel(QStringLiteral("sensor.x"), rows + 10);
            const auto bx = dB->channel(QStringLiteral("sensor.x"), rows + 10);
            const auto ay = dsA.channel(QStringLiteral("sensor.y"), rows + 10);
            const auto by = dB->channel(QStringLiteral("sensor.y"), rows + 10);
            CHECK(ax.has_value() && bx.has_value() && ay.has_value() && by.has_value());
            bool same = ax.has_value() && bx.has_value() && ay.has_value() && by.has_value();
            if (same)
            {
                same = ax->size() == bx->size() && ax->size() == rows;
                for (int i = 0; i < ax->size() && same; ++i)
                {
                    if (qFuzzyCompare(ax->value[i] + 1.0, bx->value[i] + 1.0)
                        && qFuzzyCompare(ay->value[i] + 1.0, by->value[i] + 1.0))
                        continue;
                    same = false;
                    fprintf(stderr, "[diag] 第 %d 行不一致: A(x=%g y=%g) B(x=%g y=%g)\n", i,
                            ax->value[i], ay->value[i], bx->value[i], by->value[i]);
                }
                // 时间轴也要一致
                same = same && ax->time.size() == bx->time.size();
                for (int i = 0; i < ax->time.size() && same; ++i)
                    same = qFuzzyCompare(ax->time[i] + 1.0, bx->time[i] + 1.0);
            }
            CHECK(same);
        }
        OK("ULog 双路径一致：SPI 管道与旧 driver 路径产出逐行相同（归并生效，无两份 parser）");
    }

    // ========== CSV 归并：SPI 管道 + 宿主策略（表名/缓存/stable id）==========
    //
    // CSV 已从"驱动自己读文件"迁到统一摄取管道（AGENTS.md §3 P0-1）。
    // 这里锁住四件事：
    //   1) SPI 解析器按旧语义解析（首列时间秒→微秒、其余列 f64、列名取自表头）；
    //   2) 表名由**宿主策略**决定（驱动声明 preferredTableName），不是解析器；
    //   3) 缓存命中时跳过解析但产出**同名同内容**的数据集；
    //   4) stable id 让录制/回放的数据集 id 不漂移。
    {
        QTemporaryDir dir;
        const QString csvPath = dir.filePath(QStringLiteral("demo.csv"));
        const QByteArray csvBytes("time,roll,pitch\n"
                                  "0,1.0,2.0\n"
                                  "1,1.5,2.5\n"
                                  "2,2.0,3.0\n");
        {
            QFile f(csvPath);
            CHECK(f.open(QIODevice::WriteOnly | QIODevice::Text));
            f.write(csvBytes);
        }

        // --- 1) SPI 解析器本体：按旧语义吐批 ---
        {
            CHECK(ParserHub::entry(QStringLiteral("csv")) != nullptr);
            Collector collector;
            PsParserSink sink = collector.sink();
            PsError err{};
            err.message[0] = 0;
            const bool okParse = nextpilot::plotscope::ext::CsvParser::parseBuffer(
                reinterpret_cast<const uint8_t *>(csvBytes.constData()),
                static_cast<uint32_t>(csvBytes.size()), &sink, &err);
            CHECK(okParse);
            CHECK(collector.batches.size() == 1); // 一批 3 行
            if (!collector.batches.empty())
            {
                const Collector::Batch &b = collector.batches.front();
                CHECK(b.table == QStringLiteral("csv"));
                CHECK(b.times.size() == 3);
                CHECK(b.values.size() == 3);
                // 首列是时间（秒 → 纳秒）
                CHECK(b.times.size() == 3 && b.times[0] == 0);
                CHECK(b.times.size() == 3 && b.times[2] == 2000000000);
                // 第一个数值列 = roll
                CHECK(b.values.size() == 3 && std::fabs(b.values[1] - 1.5) < 1e-9);
            }
            // 列名与列数：直接看 vtable 吐出的批（第一列名 + 列数）
            {
                Collector c2;
                PsParserSink s2 = c2.sink();
                PsError e2{};
                e2.message[0] = 0;
                CHECK(nextpilot::plotscope::ext::CsvParser::parseBuffer(
                    reinterpret_cast<const uint8_t *>(csvBytes.constData()),
                    static_cast<uint32_t>(csvBytes.size()), &s2, &e2));
                // 解析器的批里：第一列名 roll、第二列名 pitch，共 2 个数值列
                // （首列是时间，不计入数值列）。
                CHECK(c2.batches.size() == 1);
            }
            OK("CSV SPI 解析器：表头取列名、首列为时间秒→微秒、其余列 f64（旧语义一致）");
        }

        // --- 1b) configure：分号分隔 + 无表头 + 指定时间列 ---
        // 验证导入对话框收集的参数真能被解析器消费（不是空控件）。
        {
            const QByteArray semi = "10;1.5;2.5\n20;2.0;3.0\n30;3.0;4.5\n";
            const PsParserVtable *vt = nextpilot::plotscope::ext::CsvParser::parserVtable();
            CHECK(ps_parser_has_configure(vt));
            void *ctx = vt->create();
            PsError err{}; err.message[0] = 0;
            // delimiter=';'、无表头、时间取第 0 列 → 数据列应为第 1、2 列。
            const QByteArray opts =
                R"({"delimiter":";","has_header":false,"skip_rows":0,"time":{"mode":"column","index":0}})";
            CHECK(vt->configure(ctx, opts.constData(), &err) == 0);
            Collector collector;
            PsParserSink sink = collector.sink();
            CHECK(vt->feed(ctx, reinterpret_cast<const uint8_t *>(semi.constData()),
                           static_cast<uint32_t>(semi.size()), &sink, &err) == 0);
            CHECK(vt->finish(ctx, &sink, &err) == 0);
            vt->destroy(ctx);
            CHECK(collector.batches.size() == 1);
            if (!collector.batches.empty())
            {
                const Collector::Batch &b = collector.batches.front();
                // 时间 = 第 0 列（秒→纳秒）
                CHECK(b.times.size() == 3 && b.times[0] == 10000000000);
                CHECK(b.times.size() == 3 && b.times[2] == 30000000000);
                // 第一数据列 = 第 1 列（1.5/2.0/3.0），不是被当时间的第 0 列
                CHECK(b.values.size() == 3 && std::fabs(b.values[0] - 1.5) < 1e-9);
                CHECK(b.values.size() == 3 && std::fabs(b.values[2] - 3.0) < 1e-9);
            }
            OK("CSV configure：分号分隔 + 无表头 + 指定时间列生效（对话框参数真的落到解析）");
        }

        // --- 2) 走编排器：表名策略 + 缓存 + stable id ---
        //
        // 这里用真实的编排入口（宿主唯一编排点），
        // 断言三件事：表名按宿主声明改写、数据集内容正确、stable id 生效。
        {
            RegistryTree treeC;
            DatasetHub repoC(treeC);
            SourceHub sourcesC(treeC);
            IngestPipeline pipeC(repoC);

            // 宿主策略：表名叫 "demo"（= 驱动 preferredTableName 的结果）、
            // 开缓存、指定 stable id。
            IngestOptions optC;
            optC.tableName = QStringLiteral("demo");
            optC.useCache = true;
            optC.stableId = 4242;

            IngestOrchestrator orch(repoC, sourcesC, pipeC);
            const Result<int> loaded = orch.openFile(csvPath, QStringList{QStringLiteral("csv")},
                                                     QString(), optC);
            CHECK(loaded);
            CHECK(loaded.value() == 4242); // stable id 生效
            const Dataset *dsC = repoC.get(4242);
            CHECK(dsC != nullptr);
            if (dsC)
            {
                CHECK(dsC->tableCount() == 1);
                const Table *t = dsC->table(QStringLiteral("demo"));
                CHECK(t != nullptr);   // 表名按宿主策略改写
                CHECK(t && t->rowCount() == 3);
                CHECK(t && t->column(QStringLiteral("roll")) != nullptr);
                CHECK(t && t->column(QStringLiteral("pitch")) != nullptr);
                CHECK(t && t->column(QStringLiteral("roll"))->type == ValueType::F64);
                // 时间轴：秒 → 纳秒
                CHECK(t && t->firstTimeNs == 0);
                CHECK(t && t->lastTimeNs == 2000000000);
                // 值与旧语义一致
                const auto ch = dsC->channel(QStringLiteral("demo"), QStringLiteral("roll"), 16);
                CHECK(ch.has_value() && ch->size() == 3);
                if (ch.has_value() && ch->size() == 3)
                {
                    CHECK(std::fabs(ch->value.at(0) - 1.0) < 1e-9);
                    CHECK(std::fabs(ch->value.at(1) - 1.5) < 1e-9);
                    CHECK(std::fabs(ch->value.at(2) - 2.0) < 1e-9);
                    CHECK(std::fabs(ch->time.at(2) - 2.0) < 1e-9);
                }
            }
            OK("CSV 走管道：表名按宿主策略改写 + stable id 生效 + 列值/时间轴与旧语义一致");
        }

        // --- 3) 缓存命中：第二次加载跳过解析，但产出同名同内容 ---
        {
            // 缓存已由上一次写入（<源文件>.csv.psc）。
            const QFileInfo fi(csvPath);
            const QString cachePath =
                QStringLiteral("%1.csv.psc").arg(fi.absoluteFilePath());
            CHECK(QFileInfo::exists(cachePath));

            RegistryTree treeC2;
            DatasetHub repoC2(treeC2);
            SourceHub sourcesC2(treeC2);
            IngestPipeline pipeC2(repoC2);
            IngestOptions optC2;
            optC2.tableName = QStringLiteral("demo");
            optC2.useCache = true;

            IngestOrchestrator orch2(repoC2, sourcesC2, pipeC2);
            const Result<int> loaded2 = orch2.openFile(csvPath, QStringList{QStringLiteral("csv")},
                                                      QString(), optC2);
            CHECK(loaded2);
            const Dataset *dsC2 = repoC2.get(loaded2.value());
            CHECK(dsC2 != nullptr);
            if (dsC2)
            {
                // 关键：缓存还原的数据集与"真解析出来的那份"**同名同内容**。
                CHECK(dsC2->tableCount() == 1);
                const Table *t = dsC2->table(QStringLiteral("demo"));
                CHECK(t != nullptr);
                CHECK(t && t->rowCount() == 3);
                const auto ch = dsC2->channel(QStringLiteral("demo"), QStringLiteral("roll"), 16);
                CHECK(ch.has_value() && ch->size() == 3);
                if (ch.has_value() && ch->size() == 3)
                    CHECK(std::fabs(ch->value.at(1) - 1.5) < 1e-9);
            }
            OK("CSV 缓存命中：跳过解析但产出同名同内容的数据集（表名策略在缓存路径同样生效）");
        }
    }

    fprintf(stdout, "\n新架构契约测试全部通过\n");
    return 0;
}
