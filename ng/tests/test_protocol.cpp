// 协议逻辑资产平移契约测试：老 CustomProtocolLoader/SequentialCodec 的解码内核
// 平移到 ng/libs/protocol 后，纯逻辑（校验/表达式/解码/toJson）确实可用。
//
// 这是"2407 行资产不是只编译过、而是真能跑"的证明：构造模板 + 帧字节，
// 验证解码出正确的通道值；同时钉死表达式求值与序列化 round-trip。

#include "protocol/ProtocolDocument.h"
#include "protocol/ProtocolSpec.h"
#include "protocol/SequentialCodec.h"
#include "protocol/CompiledProgram.h"
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
        {"name": "sync", "count": 1, "type": "u8", "emit": "none", "expect": 170},
        {"name": "a", "count": 1, "type": "u16"},
        {"name": "b", "count": 1, "type": "u16", "scale": 0.1, "unit": "V"}
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
            if (!fissues.isEmpty())
                fprintf(stderr, "fixture %s: %s\n", fn.toUtf8().constData(),
                        fissues.join(QStringLiteral(" | ")).toUtf8().constData());
            CHECK(fissues.isEmpty());
            ProtocolDocument fd;
            CHECK(parseDocument(fr, fd, err));
        }
        OK("fixture：顺序方言模板 v2 键名通过 validate/parse");
    }
#endif

    // --- 3) 表达式求值（typed：整型/浮点各自结算）---
    EvalCtx ctx;
    {
        TypedVec x(ValueType::U8);
        x.push(TypedValue::fromU(3, ValueType::U8));
        TypedVec y(ValueType::F32);
        y.push(TypedValue::fromF(4.0, ValueType::F32));
        ctx.values["x"] = x;
        ctx.values["y"] = y;
    }
    auto numI = [](std::int64_t v) {
        auto p = std::make_shared<Expr>();
        p->kind = Expr::Num; p->ival = v; p->isInt = true;
        return p;
    };
    auto refX = std::make_shared<Expr>();
    refX->kind = Expr::Ref; refX->name = "x";
    auto binOp = [](char op, ExprPtr a, ExprPtr b) {
        auto p = std::make_shared<Expr>();
        p->kind = Expr::Bin; p->op = static_cast<int>(op); p->a = std::move(a); p->b = std::move(b);
        return p;
    };
    {
        // 整型：x + 4 = 7，结果仍是整型（不经过 double）
        ExprPtr e = binOp('+', refX, numI(4));
        TypedValue out;
        CHECK(evalExpr(e, ctx, out, err));
        CHECK(!isFloatType(out.type));
        CHECK(out.asI64() == 7);
        // 整型除法是整除，不是浮点除
        ExprPtr d = binOp('/', refX, numI(2));
        TypedValue dv;
        CHECK(evalExpr(d, ctx, dv, err));
        CHECK(dv.asI64() == 1);
        // 整型与浮点混算必须显式转换，否则报错
        auto refY = std::make_shared<Expr>();
        refY->kind = Expr::Ref; refY->name = "y";
        ExprPtr mix = binOp('+', refX, refY);
        TypedValue mv;
        CHECK(!evalExpr(mix, ctx, mv, err));
        CHECK(!err.empty());
    }
    OK("表达式系统：整型 x+4=7、整型除法整除、整型+浮点未显式转换时明确报错");

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
    // a 是 u16，通道值列保持原生 u16（不是 double）
    CHECK(ca.v.type == ValueType::U16);
    CHECK(ca.v.at(0).asU64() == 100u && ca.v.at(1).asU64() == 200u);
    // b 有 scale 0.1，物理量升到 f64
    CHECK(cb.v.type == ValueType::F64);
    CHECK(cb.v.at(0).asDouble() > 4.99 && cb.v.at(0).asDouble() < 5.01);
    CHECK(cb.v.at(1).asDouble() > 9.99 && cb.v.at(1).asDouble() < 10.01);
    // 时间轴是整数 ticks，两帧连续
    CHECK(result.ticksPerSecond == 1000000);
    CHECK(ca.t.size() == 2 && ca.t[0] == 0 && ca.t[1] == 1000000);
    CHECK(ca.unit.empty());
    CHECK(cb.unit == "V");
    OK("顺序解码：两帧 → a/b 两条通道，值保持原生类型、scale 升 f64、时间为整数 ticks");

    // --- 5) decodeSequentialJson：便捷入口也能跑 ---
    ProtocolDecodeResult r2 = decodeSequentialJson(frames, tplJson);
    CHECK(r2.ok);
    CHECK(r2.channels.size() == 2);
    OK("便捷解码：JSON 模板入口 validate+parse+decode 闭环");

    // --- 6) SPI 解析器端到端：模板 + 字节 → PsFrameBatch ---
    {
        struct Sink {
            QStringList columns;
            QVector<int> types;
            std::vector<double> values;
            std::vector<uint8_t> u8;
            int rows = 0;
            static int32_t onBatch(void *ctx, const PsFrameBatch *b) {
                auto *s = static_cast<Sink *>(ctx);
                s->rows = static_cast<int>(b->rows);
                for (uint32_t c = 0; c < b->columns; ++c) {
                    s->columns.append(QString::fromUtf8(b->column_data[c].name));
                    s->types.append(static_cast<int>(b->column_data[c].type));
                    if (b->column_data[c].type == PS_VALUE_F64 && b->column_data[c].values)
                        s->values.assign(static_cast<const double *>(b->column_data[c].values),
                                         static_cast<const double *>(b->column_data[c].values) + b->rows);
                    else if (b->column_data[c].type == PS_VALUE_U8 && b->column_data[c].values)
                        s->u8.assign(static_cast<const uint8_t *>(b->column_data[c].values),
                                     static_cast<const uint8_t *>(b->column_data[c].values) + b->rows);
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
        CHECK(collector.values.size() == 2); // b 列（f64）2 个值
        // 两条列各自带原生类型：a=u8，b=f64
        CHECK(collector.types.size() == 2);
        CHECK(collector.types.contains(static_cast<int>(PS_VALUE_U16)));
        CHECK(collector.types.contains(static_cast<int>(PS_VALUE_F64)));
        OK("SPI 解析器：自定义协议吐出带原生类型的 PsFrameBatch（a=u16, b=f64，2 行 2 列）");

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
            {"name": "crc", "count": 1, "type": "u8",
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
            {"name": "crc", "count": 1, "type": "u8",
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
          "segments":[{"name":"a","count":1,"type":"u8"}]})";
        CHECK(validate(QJsonDocument::fromJson(beTpl).object()).isEmpty());
        OK("端序别名 be 通过 validate");
    }

    // --- 8) 编译层：段表常量折叠、count 走字节码、一次编译多次解码 ---
    {
        CompiledProgram prog;
        std::string cerr;
        CHECK(compileProgram(doc, prog, cerr));
        CHECK(prog.segs.size() == 3);
        // sync 段 count=1 是常量：编译期直接给出元素数与字节数，热路径不再求值
        CHECK(prog.segs[0].countIsConst && prog.segs[0].constCount == 1);
        CHECK(prog.segs[0].constBytes == 1);
        // a 是 u16：字节宽由类型推算
        CHECK(prog.segs[1].constBytes == 2);
        // b 有 scale：编译期标记为非恒等换算
        CHECK(!prog.segs[2].identityPhys);
        // expect 170 在编译期折叠成常量，不必每帧求值
        CHECK(prog.segs[0].hasExpect && prog.segs[0].expectIsConst);
        CHECK(prog.segs[0].expectConst.asI64() == 170);

        // 同一份编译产物解码两次，结果一致（解码无残留状态）
        ProtocolDecodeResult r1 = decodeSequentialCompiled(frames, prog);
        ProtocolDecodeResult r2 = decodeSequentialCompiled(frames, prog);
        CHECK(r1.ok && r2.ok);
        CHECK(r1.channels.size() == r2.channels.size());
        CHECK(r1.channels.at(0).v.size() == r2.channels.at(0).v.size());

        // count 是表达式：走字节码而不是每帧解释表达式树
        const QByteArray varTpl = R"({
          "name":"var","layout":"sequential","endian":"little","maxsize":32,
          "segments":[
            {"name":"n","count":1,"type":"u8"},
            {"name":"body","count":"n","type":"u8"},
            {"name":"tail","count":1,"type":"u8"}
          ]
        })";
        ProtocolDocument vdoc;
        CHECK(parseDocument(QJsonDocument::fromJson(varTpl).object(), vdoc, cerr));
        CompiledProgram vprog;
        CHECK(compileProgram(vdoc, vprog, cerr));
        CHECK(!vprog.segs[1].countIsConst); // count 依赖 n，必须运行期求值
        QByteArray varFrames;
        varFrames.append(char(2)); varFrames.append(char(10)); varFrames.append(char(11)); varFrames.append(char(99));
        varFrames.append(char(3)); varFrames.append(char(20)); varFrames.append(char(21)); varFrames.append(char(22)); varFrames.append(char(98));
        ProtocolDecodeResult vr = decodeSequentialCompiled(varFrames, vprog);
        CHECK(vr.ok);
        // n + body[0..2] + tail：body 长度随 n 变化，通道列允许不齐
        CHECK(vr.channels.size() == 5);
        int nCol = -1, body2 = -1, tailCol = -1;
        for (int i = 0; i < vr.channels.size(); ++i)
        {
            const std::string &nm = vr.channels.at(i).name;
            if (nm == "frame.n") nCol = i;
            if (nm == "frame.body[2]") body2 = i;
            if (nm == "frame.tail") tailCol = i;
        }
        CHECK(nCol >= 0 && body2 >= 0 && tailCol >= 0);
        CHECK(vr.channels.at(nCol).v.size() == 2);
        CHECK(vr.channels.at(tailCol).v.size() == 2);
        CHECK(vr.channels.at(body2).v.size() == 1); // 第二帧 n=3 才有 body[2]
        OK("编译层：常量折叠、表达式 count 走字节码、一次编译多次解码结果稳定");
    }

    fprintf(stdout, "\n协议逻辑资产平移测试全部通过\n");
    return 0;
}
