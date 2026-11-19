// 协议逻辑资产平移契约测试：老 CustomProtocolLoader/SequentialCodec 的解码内核
// 平移到 ng/libs/protocol 后，纯逻辑（校验/表达式/解码/toJson）确实可用。
//
// 这是"2407 行资产不是只编译过、而是真能跑"的证明：构造模板 + 帧字节，
// 验证解码出正确的通道值；同时钉死表达式求值与序列化 round-trip。

#include "protocol/ProtocolDocument.h"
#include "protocol/ProtocolSpec.h"
#include "protocol/SequentialCodec.h"
#include "CustomProtocolParser.h"

#include "ps_parser_spi.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace nextpilot::plotscope::protocol;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
            std::exit(1);                                                                \
        }                                                                                \
    } while (0)

#define OK(msg) fprintf(stdout, "[ok] %s\n", msg)

// 简单顺序协议：sync(u8=0xAA) + a(u16) + b(u16, scale=0.1)。无 CRC/series。
static QByteArray makeTemplate()
{
    return R"({
      "name": "simple",
      "layout": "sequential",
      "endian": "little",
      "maxsize": 16,
      "segments": [
        {"name": "sync", "size": 1, "type": "u8", "role": "sync", "value": 170},
        {"name": "a", "size": 1, "type": "u16"},
        {"name": "b", "size": 1, "type": "u16", "scale": 0.1, "unit": "V"}
      ]
    })";
}

// 帧：AA | a(u16 LE) | b(u16 LE)
static void appendFrame(QByteArray &bytes, quint16 a, quint16 b)
{
    bytes.append(char(0xAA));
    bytes.append(char(a & 0xFF));
    bytes.append(char((a >> 8) & 0xFF));
    bytes.append(char(b & 0xFF));
    bytes.append(char((b >> 8) & 0xFF));
}

int main(int argc, char *argv[])
{
    Q_UNUSED(argc);
    Q_UNUSED(argv);

    const QByteArray tplJson = makeTemplate();

    // --- 1) validate：合法模板无问题 ---
    const QJsonObject root = QJsonDocument::fromJson(tplJson).object();
    const QStringList issues = validate(root);
    CHECK(issues.isEmpty());
    OK("协议校验：合法顺序模板通过 validate");

    // --- 2) parseDocument / toJson round-trip ---
    ProtocolDocument doc;
    std::string err;
    CHECK(parseDocument(root, doc, err));
    CHECK(doc.name == "simple");
    CHECK(doc.segments.size() == 3);
    const QJsonObject roundTrip = toJson(doc);
    ProtocolDocument doc2;
    CHECK(parseDocument(roundTrip, doc2, err));
    CHECK(doc2.segments.size() == 3);
    CHECK(toJson(doc2) == roundTrip);
    OK("协议文档：parseDocument/toJson round-trip 稳定");

    // --- 2b) v2 键名：count/emit/expect，validate 通过、回写对称、aux 不出通道 ---
    {
        const QByteArray v2 = R"({
          "name": "v2", "layout": "sequential", "endian": "little", "maxsize": 16,
          "segments": [
            {"name": "sync", "count": 1, "type": "u8", "expect": 170, "emit": "none"},
            {"name": "a", "count": 1, "type": "u16"},
            {"name": "b", "count": 1, "type": "u16", "scale": 0.1, "emit": "aux"}
          ]
        })";
        const QJsonObject v2Root = QJsonDocument::fromJson(v2).object();
        CHECK(validate(v2Root).isEmpty());
        ProtocolDocument v2Doc;
        CHECK(parseDocument(v2Root, v2Doc, err));
        const QJsonObject v2Out = toJson(v2Doc);
        CHECK(v2Out.value("segments").toArray().at(0).toObject().contains("count"));
        CHECK(!v2Out.value("segments").toArray().at(0).toObject().contains("size"));
        ProtocolDocument v2Doc2;
        CHECK(parseDocument(v2Out, v2Doc2, err));
        CHECK(toJson(v2Doc2) == v2Out);

        QByteArray v2Frames;
        appendFrame(v2Frames, 100, 50);
        appendFrame(v2Frames, 200, 100);
        ProtocolDecodeResult v2Res = decodeSequential(v2Frames, v2Doc);
        CHECK(v2Res.ok);
        // b 为 aux，不出通道；只剩 a。
        CHECK(v2Res.channels.size() == 1);
        CHECK(v2Res.channels.at(0).name == "frame.a");
        OK("v2 键名：count/emit/expect 校验通过、回写对称、aux 段不出通道");
    }

    // --- 2c) 仓库 fixture：两个顺序方言模板迁移到 v2 键名后仍通过 validate/parse ---
#ifdef NG_TEST_FIXTURE_DIR
    {
        const QStringList fixtures = {
            QStringLiteral("demo_sequential.psproto"), QStringLiteral("demo_nmea.psproto")};
        for (const QString &fn : fixtures)
        {
            QFile f(QDir(QLatin1String(NG_TEST_FIXTURE_DIR)).filePath(fn));
            if (!f.exists())
                continue;
            CHECK(f.open(QIODevice::ReadOnly));
            const QJsonObject fr = QJsonDocument::fromJson(f.readAll()).object();
            const QStringList fissues = validate(fr);
            CHECK(fissues.isEmpty());
            ProtocolDocument fd;
            CHECK(parseDocument(fr, fd, err));
        }
        OK("fixture：顺序方言模板 v2 键名通过 validate/parse");
    }
#endif

    // --- 3) 表达式求值 ---
    EvalCtx ctx;
    ctx.values["x"] = {3.0};
    ctx.values["y"] = {4.0};
    ExprPtr e;
    // 构造 x*2+1 = 7
    auto num = [](double v) { return std::make_shared<Expr>(Expr{Expr::Num, v, "", "", 0, nullptr, nullptr, nullptr}); };
    auto refX = std::make_shared<Expr>(Expr{Expr::Ref, 0, "x", "", 0, nullptr, nullptr, nullptr});
    e = std::make_shared<Expr>(Expr{Expr::Bin, 0, "", "", '+', refX, num(4.0), nullptr});
    double out = 0;
    CHECK(evalExpr(e, ctx, out, err));
    CHECK(out == 7.0);
    OK("表达式系统：x + 4 = 7 求值正确");

    // --- 4) decodeSequential：两帧 → 两条通道，值正确 ---
    QByteArray frames;
    appendFrame(frames, 100, 50);  // b=50 → scaled 5.0
    appendFrame(frames, 200, 100); // b=100 → scaled 10.0
    ProtocolDecodeResult result = decodeSequential(frames, doc);
    CHECK(result.ok);
    CHECK(result.channels.size() == 2);
    // 通道 a：100, 200；通道 b：5.0, 10.0（scale 0.1）
    int channelA = -1, channelB = -1;
    for (int i = 0; i < result.channels.size(); ++i)
    {
        const std::string &n = result.channels.at(i).name;
        if (n == "frame.a") channelA = i;
        if (n == "frame.b") channelB = i;
    }
    CHECK(channelA >= 0 && channelB >= 0);
    const auto &ca = result.channels.at(channelA);
    const auto &cb = result.channels.at(channelB);
    CHECK(ca.v.size() == 2 && cb.v.size() == 2);
    CHECK(ca.v[0] == 100.0 && ca.v[1] == 200.0);
    CHECK(cb.v[0] > 4.99 && cb.v[0] < 5.01);
    CHECK(cb.v[1] > 9.99 && cb.v[1] < 10.01);
    CHECK(ca.unit.empty());
    CHECK(cb.unit == "V");
    OK("顺序解码：两帧 → a/b 两条通道，值/单位/scale 正确");

    // --- 5) decodeSequentialJson：便捷入口也能跑 ---
    ProtocolDecodeResult r2 = decodeSequentialJson(frames, tplJson);
    CHECK(r2.ok);
    CHECK(r2.channels.size() == 2);
    OK("便捷解码：JSON 模板入口 validate+parse+decode 闭环");

    // --- 6) SPI 解析器端到端：模板 + 字节 → PsFrameBatch ---
    {
        struct Sink {
            QStringList columns;
            std::vector<double> values;
            int rows = 0;
            static int32_t onBatch(void *ctx, const PsFrameBatch *b) {
                auto *s = static_cast<Sink *>(ctx);
                s->rows = static_cast<int>(b->rows);
                for (uint32_t c = 0; c < b->columns; ++c) {
                    s->columns.append(QString::fromUtf8(b->column_data[c].name));
                    if (b->column_data[c].type == PS_VALUE_F64 && b->column_data[c].values)
                        s->values.assign(static_cast<const double *>(b->column_data[c].values),
                                         static_cast<const double *>(b->column_data[c].values) + b->rows);
                }
                return 0;
            }
            PsParserSink sink() {
                PsParserSink s{};
                s.ctx = this;
                s.frame_batch = &Sink::onBatch;
                s.progress = nullptr;
                return s;
            }
        } collector;

        PsParserSink sink = collector.sink();
        PsError perr{};
        bool ok = nextpilot::plotscope::ext::CustomProtocolParser::parseBuffer(
            tplJson, reinterpret_cast<const uint8_t *>(frames.constData()),
            static_cast<uint32_t>(frames.size()), &sink, &perr);
        CHECK(ok);
        CHECK(collector.rows == 2);
        CHECK(collector.columns.size() == 2);
        CHECK(collector.columns.contains(QStringLiteral("frame.a")));
        CHECK(collector.columns.contains(QStringLiteral("frame.b")));
        CHECK(collector.values.size() == 2); // 最后一列 b 的 2 个值
        OK("SPI 解析器：自定义协议经 configure+feed+finish 吐出 PsFrameBatch（2 行 2 列）");

        // 未 configure（缺模板）：明确失败。
        {
            PsParserSink s2 = collector.sink();
            PsError e2{};
            const PsParserVtable *vt = nextpilot::plotscope::ext::CustomProtocolParser::parserVtable();
            void *ctx = vt->create();
            vt->feed(ctx, reinterpret_cast<const uint8_t *>(frames.constData()),
                     static_cast<uint32_t>(frames.size()), &s2, &e2);
            CHECK(vt->finish(ctx, &s2, &e2) != 0); // 未 configure → finish 报错
            vt->destroy(ctx);
            OK("SPI 解析器：未配置模板时 finish 明确失败");
        }
    }

    // --- 7) 非法模板：校验失败明确报错 ---
    ProtocolDecodeResult bad = decodeSequentialJson(frames, QByteArray("{}"));
    CHECK(!bad.ok);
    CHECK(!bad.error.isEmpty());
    OK("非法模板：明确报错，不静默产出");

    // --- 7b) 畸形 CRC 参数：非法十六进制不得抛异常终止进程 ---
    {
        const QByteArray badPoly = R"({
          "name": "bad_crc", "layout": "sequential", "endian": "little", "maxsize": 16,
          "segments": [
            {"name": "crc", "size": 1, "type": "u8",
             "checks": [{"op": "crc16", "poly": "0xZZ", "over": ["crc:crc"]}]}
          ]
        })";
        ProtocolDocument dBad;
        std::string eBad;
        CHECK(!parseDocument(QJsonDocument::fromJson(badPoly).object(), dBad, eBad));
        CHECK(!eBad.empty());
        OK("畸形 CRC 参数：parseDocument 返回错误，进程不崩溃");

        // 合法 0x 前缀仍能正常解析
        const QByteArray okPoly = R"({
          "name": "ok_crc", "layout": "sequential", "endian": "little", "maxsize": 16,
          "segments": [
            {"name": "crc", "size": 1, "type": "u8",
             "checks": [{"op": "crc16", "poly": "0x1021", "init": "0xFFFF", "over": ["crc:crc"]}]}
          ]
        })";
        ProtocolDocument dOk;
        std::string eOk;
        CHECK(parseDocument(QJsonDocument::fromJson(okPoly).object(), dOk, eOk));
        OK("合法 0x 前缀 CRC 参数：解析通过");
    }

    // --- 7c) 端序别名 be/le 被校验接受（解析层归一到 big/little） ---
    {
        const QByteArray beTpl = R"({"name":"e","layout":"sequential","endian":"be",
          "segments":[{"name":"a","size":1,"type":"u8"}]})";
        CHECK(validate(QJsonDocument::fromJson(beTpl).object()).isEmpty());
        OK("端序别名 be 通过 validate");
    }

    fprintf(stdout, "\n协议逻辑资产平移测试全部通过\n");
    return 0;
}
