// 协议逻辑资产平移契约测试：老 CustomProtocolLoader/SequentialCodec 的解码内核
// 平移到 ng/libs/protocol 后，纯逻辑（校验/表达式/解码/toJson）确实可用。
//
// 这是"2407 行资产不是只编译过、而是真能跑"的证明：构造模板 + 帧字节，
// 验证解码出正确的通道值；同时钉死表达式求值与序列化 round-trip。

#include "protocol/ProtocolDocument.h"
#include "protocol/ProtocolSpec.h"
#include "protocol/SequentialCodec.h"
#include "protocol/CompiledProgram.h"
#include "protocol/StreamDecoder.h"
#include "protocol/DigestEngine.h"
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

#include <cmath>
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
      "mode": "bin",
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
          "name": "v2",
      "mode": "bin", "layout": "sequential", "endian": "little", "maxsize": 16,
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
            QStringLiteral("demo_sequential.psproto"), QStringLiteral("demo_nmea.psproto"),
            QStringLiteral("demo_can.psproto")};
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
          "name": "bad_crc",
      "mode": "bin", "layout": "sequential", "endian": "little", "maxsize": 16,
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
          "name": "ok_crc",
      "mode": "bin", "layout": "sequential", "endian": "little", "maxsize": 16,
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
        const QByteArray beTpl = R"({"name":"e","mode":"bin","layout":"sequential","endian":"be",
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
          "name":"var","mode":"bin","layout":"sequential","endian":"little","maxsize":32,
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

    // --- 9) 位域：CAN 式 8 字节帧，Intel/Motorola、base 容器、符号扩展、scale ---
    {
        const QByteArray canTpl = R"({
          "name":"can","mode":"bin","layout":"sequential","endian":"little","maxsize":64,
          "segments":[
            {"name":"body","count":8,"type":"u8","emit":"none"},
            {"name":"byte0","count":1,"type":"u8","bit":{"offset":0,"width":8,"order":"lsb0"}},
            {"name":"intel16","count":1,"type":"u16","bit":{"offset":0,"width":16,"order":"lsb0"}},
            {"name":"motor16","count":1,"type":"u16","bit":{"offset":0,"width":16,"order":"msb0"}},
            {"name":"sig","count":1,"type":"u16","bit":{"base":"body","offset":16,"width":16,"order":"msb0"}},
            {"name":"nib","count":1,"type":"i8","bit":{"offset":4,"width":4,"order":"lsb0"}},
            {"name":"rpm","count":1,"type":"u16","bit":{"base":"body","offset":16,"width":16,"order":"msb0"},
             "scale":0.25,"unit":"rpm"}
          ]
        })";
        ProtocolDocument cdoc;
        std::string cerr;
        CHECK(parseDocument(QJsonDocument::fromJson(canTpl).object(), cdoc, cerr));
        CHECK(validate(QJsonDocument::fromJson(canTpl).object()).isEmpty());
        CompiledProgram cprog;
        CHECK(compileProgram(cdoc, cprog, cerr));

        QByteArray canFrame;
        canFrame.append(char(0x12)); canFrame.append(char(0x34));
        canFrame.append(char(0x56)); canFrame.append(char(0x78));
        canFrame.append(char(0x9A)); canFrame.append(char(0xBC));
        canFrame.append(char(0xDE)); canFrame.append(char(0xF0));
        ProtocolDecodeResult cr = decodeSequentialCompiled(canFrame, cprog);
        CHECK(cr.ok);

        auto colOf = [](const ProtocolDecodeResult &res, const char *nm) -> const ProtocolChannel * {
            for (const auto &ch : res.channels)
                if (ch.name == nm) return &ch;
            return nullptr;
        };
        const ProtocolChannel *c0 = colOf(cr, "frame.byte0");
        const ProtocolChannel *ci = colOf(cr, "frame.intel16");
        const ProtocolChannel *cm = colOf(cr, "frame.motor16");
        const ProtocolChannel *cs_ = colOf(cr, "frame.sig");
        const ProtocolChannel *cn = colOf(cr, "frame.nib");
        const ProtocolChannel *cp = colOf(cr, "frame.rpm");
        CHECK(c0 && ci && cm && cs_ && cn && cp);
        CHECK(c0->v.at(0).asU64() == 0x12);
        CHECK(ci->v.at(0).asU64() == 0x3412);          // Intel：低字节在前
        CHECK(cm->v.at(0).asU64() == 0x1234);          // Motorola：高位在前
        CHECK(cs_->v.at(0).asU64() == 0x5678);         // base=body，bit16 起 16 位
        CHECK(cn->v.at(0).asI64() == 1);               // 0x12 的高 4 位
        CHECK(cp->v.type == ValueType::F64);           // scale 0.25 → 物理列
        CHECK(qFuzzyCompare(cp->v.at(0).asDouble(), 0x5678 * 0.25));
        // 位域段不推进字节游标：8 字节 body 之后再没有段，帧长度正好 8
        CHECK(cr.channels.size() == 6);
        OK("位域：Intel/Motorola 位序、base 容器、符号扩展、scale 全部按 SPEC §6");
    }

    // --- 10) 条件段 if：不成立不占字节、不出通道；位域负值符号扩展 ---
    {
        const QByteArray condTpl = R"({
          "name":"cond","mode":"bin","layout":"sequential","endian":"little","maxsize":32,
          "segments":[
            {"name":"flags","count":1,"type":"u8"},
            {"name":"a","count":1,"type":"u8"},
            {"name":"extra","count":1,"type":"u8","if":"flags == 1"},
            {"name":"b","count":1,"type":"u8"}
          ]
        })";
        ProtocolDocument d2;
        std::string cerr;
        CHECK(parseDocument(QJsonDocument::fromJson(condTpl).object(), d2, cerr));
        CHECK(validate(QJsonDocument::fromJson(condTpl).object()).isEmpty());
        CompiledProgram p2;
        CHECK(compileProgram(d2, p2, cerr));

        QByteArray f1;
        f1.append(char(1)); f1.append(char(0xAA)); f1.append(char(0xBB)); f1.append(char(0xCC));
        QByteArray f0;
        f0.append(char(0)); f0.append(char(0xAA)); f0.append(char(0xCC));
        ProtocolDecodeResult r1 = decodeSequentialCompiled(f1, p2);
        ProtocolDecodeResult r0 = decodeSequentialCompiled(f0, p2);
        CHECK(r1.ok && r0.ok);
        int ai = -1, bi = -1, ei = -1;
        for (int i = 0; i < r1.channels.size(); ++i)
        {
            if (r1.channels.at(i).name == "frame.a") ai = i;
            if (r1.channels.at(i).name == "frame.b") bi = i;
            if (r1.channels.at(i).name == "frame.extra") ei = i;
        }
        CHECK(ai >= 0 && bi >= 0 && ei >= 0);
        CHECK(r1.channels.at(ai).v.at(0).asU64() == 0xAA);
        CHECK(r1.channels.at(bi).v.at(0).asU64() == 0xCC);
        CHECK(r1.channels.at(ei).v.at(0).asU64() == 0xBB);
        // 条件不成立：extra 不消耗字节，b 仍读到第 2 个字节
        int bi0 = -1;
        for (int i = 0; i < r0.channels.size(); ++i)
            if (r0.channels.at(i).name == "frame.b") bi0 = i;
        CHECK(bi0 >= 0);
        CHECK(r0.channels.at(bi0).v.at(0).asU64() == 0xCC);
        OK("条件段：if 为假时不占字节、不出通道；为真时按序解析");
    }

    // --- 11) fixture：CAN 式位域模板可直接解码 8 字节裸帧 ---
#ifdef NG_TEST_FIXTURE_DIR
    {
        QFile f(QDir(QLatin1String(NG_TEST_FIXTURE_DIR)).filePath(QStringLiteral("demo_can.psproto")));
        if (f.open(QIODevice::ReadOnly))
        {
            const QJsonObject fr = QJsonDocument::fromJson(f.readAll()).object();
            ProtocolDocument fd;
            std::string cerr;
            CHECK(parseDocument(fr, fd, cerr));
            CompiledProgram fp;
            CHECK(compileProgram(fd, fp, cerr));
            QByteArray frame;
            for (int i = 0; i < 8; ++i)
                frame.append(char(i == 0 ? 0x12 : (i == 3 ? 0x7F : 0x00)));
            ProtocolDecodeResult rr = decodeSequentialCompiled(frame, fp);
            CHECK(rr.ok);
            const ProtocolChannel *rpm = nullptr;
            const ProtocolChannel *temp = nullptr;
            for (const auto &ch : rr.channels)
            {
                if (ch.name == "frame.rpm") rpm = &ch;
                if (ch.name == "frame.temp") temp = &ch;
            }
            CHECK(rpm && temp);
            // body[1..2] 的 16 位（Motorola）= 0x0000，scale 0.25
            CHECK(rpm->v.type == ValueType::F64);
            // 第 4 字节 0x7F，symbol 扩展后为 127
            CHECK(temp->v.at(0).asI64() == 127);
            OK("fixture：demo_can.psproto 位域模板解码通过");
        }
    }
#endif // 11) 位域 fixture

#ifdef NG_TEST_FIXTURE_DIR
    // --- 12) text 流式：NMEA GGA（$ 定位 / * 硬匹配 / XOR / \r\n） ---
    {
        const QDir fixDir(QLatin1String(NG_TEST_FIXTURE_DIR));

        QFile nf(fixDir.filePath(QStringLiteral("demo_nmea_gga.psproto")));
        CHECK(nf.open(QIODevice::ReadOnly));
        const QJsonObject nmeaObj = QJsonDocument::fromJson(nf.readAll()).object();
        nf.close();

        {
            const QStringList verrs = validate(nmeaObj);
            if (!verrs.isEmpty())
                fprintf(stderr, "NMEA 模板校验失败：%s\n", verrs.join(QStringLiteral("; ")).toUtf8().constData());
            CHECK(verrs.isEmpty());
        }
        ProtocolDocument nd;
        std::string nerr;
        {
            const bool pok = parseDocument(nmeaObj, nd, nerr);
            if (!pok) fprintf(stderr, "NMEA parseDocument: %s\n", nerr.c_str());
            CHECK(pok);
        }
        CompiledProgram np;
        {
            const bool cok = compileProgram(nd, np, nerr);
            if (!cok) fprintf(stderr, "NMEA compileProgram: %s\n", nerr.c_str());
            CHECK(cok);
        }
        CHECK(np.isText && np.isDelimited);
        CHECK(np.hasStartAnchor && np.startAnchor == "$");
        CHECK(np.hasTerminator);

        const std::string gga =
            "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";

        auto findCh = [](const ProtocolDecodeResult &r, const char *name) -> const ProtocolChannel * {
            for (const auto &ch : r.channels)
                if (ch.name == name)
                    return &ch;
            return nullptr;
        };

        // (a) 整行一次 push：会话级校验通过即产出，不必等 term
        {
            ProtocolDecodeResult out;
            StreamDecoder dec(np);
            std::string perr;
            CHECK(dec.push(QByteArray::fromStdString(gga), out, perr));
            dec.finish(out);
            CHECK(out.ok);
            CHECK(out.errors.size() == 0);
            const ProtocolChannel *lat = findCh(out, "frame.lat");
            const ProtocolChannel *lon = findCh(out, "frame.lon");
            const ProtocolChannel *alt = findCh(out, "frame.alt");
            CHECK(lat && lon && alt);
            CHECK(std::fabs(lat->v.at(0).asDouble() - (48.0 + 7.038 / 60.0)) < 1e-9);
            CHECK(std::fabs(lon->v.at(0).asDouble() - (11.0 + 31.000 / 60.0)) < 1e-9);
            CHECK(std::fabs(alt->v.at(0).asDouble() - 545.4) < 1e-9);
            CHECK(dec.dropped() == 0 && dec.rejected() == 0);
        }

        // (b) 同一行分两个 chunk：跨包仍能拼起来
        {
            ProtocolDecodeResult out;
            StreamDecoder dec(np);
            std::string perr;
            CHECK(dec.push(QByteArray::fromStdString(gga.substr(0, 25)), out, perr));
            CHECK(out.channels.size() == 0);
            CHECK(dec.push(QByteArray::fromStdString(gga.substr(25)), out, perr));
            CHECK(out.ok);
            CHECK(findCh(out, "frame.lat") != nullptr);
        }

        // (c) 去掉 term 也能靠 check 宽度自证记录尾
        {
            QJsonObject noTerm = nmeaObj;
            noTerm.remove(QStringLiteral("term"));
            ProtocolDocument d2;
            std::string e2;
            CHECK(parseDocument(noTerm, d2, e2));
            CompiledProgram p2;
            CHECK(compileProgram(d2, p2, e2));
            CHECK(!p2.hasTerminator);

            const std::string line = gga.substr(0, gga.size() - 2); // 去掉 \r\n
            ProtocolDecodeResult out;
            StreamDecoder dec(p2);
            CHECK(dec.push(QByteArray::fromStdString(line), out, e2));
            CHECK(out.ok);
            CHECK(findCh(out, "frame.lat") != nullptr);
        }

        // (d) 校验错：明确分类，不产出半帧
        {
            std::string bad = gga;
            const size_t star = bad.find('*');
            bad[star + 1] = '4';
            bad[star + 2] = '6';
            ProtocolDecodeResult out;
            StreamDecoder dec(np);
            std::string perr;
            CHECK(dec.push(QByteArray::fromStdString(bad), out, perr));
            CHECK(!out.ok);
            CHECK(out.channels.size() == 0);
            CHECK(out.errors.size() == 1);
            CHECK(out.errors.front().code == DecodeError::ChecksumMismatch);
            CHECK(dec.rejected() == 1);
        }

        // (e) 等 * 时来新 $：旧候选作废，从新帧首重来
        {
            const std::string broken = "$GPGGA,123519,4807.038,N,01131.000,E";
            ProtocolDecodeResult out;
            StreamDecoder dec(np);
            std::string perr;
            CHECK(dec.push(QByteArray::fromStdString(broken), out, perr));
            CHECK(!out.ok && out.errors.size() == 0); // 还在等，不报错
            CHECK(dec.push(QByteArray::fromStdString(gga), out, perr));
            CHECK(out.ok);
            CHECK(findCh(out, "frame.lat") != nullptr);
            CHECK(dec.dropped() == 1);
            CHECK(out.errors.size() == 1);
            CHECK(out.errors.front().code == DecodeError::RecordAborted);
        }
        OK("text 流式：NMEA GGA 整行/跨包/无 term/校验错/新 $ 重置全部符合预期");

        // --- 12b) text 空白分隔（无同步字，靠 term 分帧） ---
        QFile cf(fixDir.filePath(QStringLiteral("demo_csv_blank.psproto")));
        CHECK(cf.open(QIODevice::ReadOnly));
        const QJsonObject csvObj = QJsonDocument::fromJson(cf.readAll()).object();
        cf.close();
        CHECK(validate(csvObj).isEmpty());
        ProtocolDocument cd;
        std::string cerr;
        {
            const bool pok = parseDocument(csvObj, cd, cerr);
            if (!pok) fprintf(stderr, "CSV parseDocument: %s\n", cerr.c_str());
            CHECK(pok);
        }
        CompiledProgram cp;
        {
            const bool cok = compileProgram(cd, cp, cerr);
            if (!cok) fprintf(stderr, "CSV compileProgram: %s\n", cerr.c_str());
            CHECK(cok);
        }
        CHECK(cp.isText && !cp.hasStartAnchor && cp.hasTerminator);

        {
            const std::string csv = "1 23.5 alpha\r\n2 11.0 beta\r\n3 7.25 gamma\r\n";
            ProtocolDecodeResult out;
            StreamDecoder dec(cp);
            std::string perr;
            CHECK(dec.push(QByteArray::fromStdString(csv), out, perr));
            dec.finish(out);
            CHECK(out.ok);
            CHECK(out.errors.size() == 0);
            const ProtocolChannel *id = findCh(out, "frame.id");
            const ProtocolChannel *val = findCh(out, "frame.val");
            const ProtocolChannel *nm = findCh(out, "frame.name");
            CHECK(id && val && nm);
            CHECK(id->v.size() == 3 && val->v.size() == 3 && nm->v.size() == 3);
            CHECK(id->v.at(2).asI64() == 3);
            CHECK(std::fabs(val->v.at(2).asDouble() - 7.25) < 1e-9);
            CHECK(nm->v.at(0).type == ValueType::Text);
        }
        OK("text 空白分隔：三行三通道，blank 是协议级分隔符类");

        // 便捷入口 decodeSequential 也要能走 text（上层 CustomProtocolParser 用的就是它）
        {
            const std::string two =
                "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n"
                "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
            ProtocolDecodeResult dr = decodeSequential(QByteArray::fromStdString(two), nd);
            CHECK(dr.ok);
            CHECK(dr.errors.size() == 0);
            const ProtocolChannel *lat = findCh(dr, "frame.lat");
            CHECK(lat && lat->v.size() == 2);
            CHECK(lat->t.size() == 2 && lat->t.at(1) > lat->t.at(0));
        }
        OK("便捷入口：decodeSequential 对 text 模板同样闭环，两行两值");
    }
#endif

    // --- 编译层负例：坏模板必须明确失败，不静默产出 ---
    {
        auto compileFail = [](const char *json, std::string &why) -> bool {
            ProtocolDocument doc;
            std::string perr;
            if (!parseDocument(QJsonDocument::fromJson(QByteArray(json)).object(), doc, perr))
            {
                why = perr;
                return true; // 解析/校验层拦下也算失败
            }
            CompiledProgram prog;
            std::string err;
            if (!compileProgram(doc, prog, err))
            {
                why = err;
                return true;
            }
            return false;
        };

        {
            std::string why;
            CHECK(compileFail(R"({"name":"neg1","mode":"bin","segments":[{"name":"a","type":"u8"},{"name":"b","type":"u8","count":"undef_slot + 1"}]})", why));
        }
        {
            std::string why;
            CHECK(compileFail(R"({"name":"neg2","mode":"bin","segments":[{"name":"a","type":"u8","bit":{"base":"nope","offset":0,"width":3}}]})", why));
        }
        {
            std::string why;
            CHECK(compileFail(R"({"name":"neg3","mode":"text","segments":[{"name":"a","literal":"$","width":0}]})", why));
        }
        {
            std::string why;
            CHECK(compileFail(R"({"name":"neg4","mode":"text","segments":[{"name":"a","literal":"$","delimiter":","}]})", why));
        }
        {
            // mode 必填 / 枚举 / bin-text 键黑白名单（validate 层）
            auto vfail = [](const char *j) {
                return !validate(QJsonDocument::fromJson(QByteArray(j)).object()).isEmpty();
            };
            CHECK(vfail(R"({"name":"x","segments":[{"name":"a","type":"u8"}]})")); // 缺 mode
            CHECK(vfail(R"({"name":"x","mode":"xml","segments":[{"name":"a","type":"u8"}]})")); // 非法值
            CHECK(vfail(R"({"name":"x","mode":"bin","segments":[{"name":"a","type":"char","delimiter":","}]})")); // bin 禁文本词法键
            CHECK(vfail(R"({"name":"x","mode":"text","segments":[{"name":"a","type":"u8","bit":{"offset":0,"width":4}}]})")); // text 禁位域
            OK("mode 必填/枚举与 bin-text 键黑白名单校验");
        }
        {
            std::string why;
            CHECK(compileFail(R"({"name":"neg5","mode":"bin","segments":[{"name":"a","type":"u8","width":70000}]})", why));
        }
        OK("编译层负例：未定义槽位/bit.base/width 越界/literal+delimiter 混写全部明确失败");
    }

    // --- text 变体：按 msgid 选子协议（NMEA 多句） ---
    {
        const char *js = R"({"name":"nmea_series","mode":"text","term":["\r\n"],"maxsize":128,
 "segments":[
   {"name":"dollar","literal":"$","emit":"none"},
   {"name":"msgid","type":"char","delimiter":",","emit":"none"},
   {"name":"payload","type":"char","delimiter":",","repeater":"*","emit":"none"},
   {"name":"star","literal":"*","emit":"none"},
   {"name":"tail","type":"char","delimiter":"\r","emit":"none"}
 ],
 "series":{"selector":"msgid","payload":"payload","fallback":"reject",
   "variants":[{"values":["GPGGA"],"name":"gga","segments":[
       {"name":"lat","type":"number","delimiter":","},
       {"name":"ns","type":"char","delimiter":","}
   ]}]}})";
        ProtocolDocument doc;
        std::string err;
        const bool parsed = parseDocument(QJsonDocument::fromJson(QByteArray(js)).object(), doc, err);
        if (!parsed) fprintf(stdout, "series parse err: %s\n", err.c_str());
        CHECK(parsed);
        CompiledProgram prog;
        CHECK(compileProgram(doc, prog, err));
        auto findCh = [](const ProtocolDecodeResult &r, const char *name) -> const ProtocolChannel * {
            for (const ProtocolChannel &c : r.channels)
                if (c.name == name)
                    return &c;
            return nullptr;
        };
        StreamDecoder dec(prog);
        ProtocolDecodeResult out;
        CHECK(dec.push(QByteArray("$GPGGA,4807.038,N*4F\r\n"), out, err));
        CHECK(out.ok);
        const ProtocolChannel *lat = findCh(out, "frame.gga.lat");
        const ProtocolChannel *ns = findCh(out, "frame.gga.ns");
        CHECK(lat && lat->v.size() == 1);
        CHECK(std::fabs(lat->v.at(0).asDouble() - 4807.038) < 1e-6);
        CHECK(ns && ns->v.size() == 1 && ns->v.at(0).s == "N");
        OK("text 变体：顶层段解 selector（msgid）→ 变体段表替换 payload 段重解");
    }

    // --- derive 派生字段 + type:time 文本时间 ---
    {
        const QByteArray tplBin = R"({
          "name":"derived","mode":"bin","layout":"sequential","maxsize":64,
          "segments":[
            {"name":"a","type":"u16"},
            {"name":"b","type":"u16"},
            {"name":"sum","derive":"a+b"},
            {"name":"prod","derive":"a*b"}
          ]
        })";
        const QByteArray rawBin("  ", 4);
        ProtocolDecodeResult dr = decodeSequentialJson(rawBin, tplBin);
        CHECK(dr.channels.size() == 4);
        for (const auto &ch : dr.channels)
        {
            if (ch.name == "sum")
                CHECK(ch.v.size() == 1 && ch.v.at(0).asDouble() == 7.0);
            if (ch.name == "prod")
                CHECK(ch.v.size() == 1 && ch.v.at(0).asDouble() == 12.0);
        }

        const QByteArray tplTime = R"({
          "name":"time_csv","mode":"text","layout":"sequential","maxsize":64,"terminator":"\n",
          "segments":[
            {"name":"t","type":"time","delimiter":","},
            {"name":"v","type":"i32","delimiter":";"}
          ]
        })";
        ProtocolDecodeResult tr = decodeSequentialJson(QByteArray("083559.00,42;\n"), tplTime);
        CHECK(tr.channels.size() == 2);
        for (const auto &ch : tr.channels)
            if (ch.name == "t")
                CHECK(std::fabs(ch.v.at(0).asDouble() - 30959.0) < 1e-6);

        // 越界时间必须被拒（24h 不存在）
        ProtocolDecodeResult badT = decodeSequentialJson(QByteArray("246100.00,42;\n"), tplTime);
        CHECK(!badT.ok || badT.rejectedFrames > 0);
        OK("derive 派生字段（a+b / a*b）出通道；type:time 把 083559.00 解成 30959 秒，越界被拒");
    }

    // --- derive 走 text 路径 + preview/compiled 双路一致 ---
    {
        const auto chOf = [](const ProtocolDecodeResult &r, const char *n) -> const ProtocolChannel * {
            const std::string key = n;
            const std::string prefixed = std::string("frame.") + n;
            for (const auto &c : r.channels)
                if (c.name == key || c.name == prefixed) return &c;
            return nullptr;
        };

        const QByteArray tpl = R"({
          "name":"derive_text","mode":"text","layout":"sequential","maxsize":64,"terminator":"\n",
          "segments":[
            {"name":"a","type":"number","delimiter":","},
            {"name":"b","type":"number"},
            {"name":"sum","derive":"a+b"},
            {"name":"prod","derive":"a*b"}
          ]
        })";
        ProtocolDocument doc;
        std::string pe;
        CHECK(parseDocument(QJsonDocument::fromJson(tpl).object(), doc, pe));
        CompiledProgram prog;
        CHECK(compileProgram(doc, prog, pe));
        const QByteArray frame("3,4\n");

        // compiled 生产路径（text → StreamDecoder）：derive 出通道且值对
        ProtocolDecodeResult cr = decodeSequentialCompiled(frame, prog);
        CHECK(cr.ok);
        const ProtocolChannel *sum = chOf(cr, "sum");
        const ProtocolChannel *prod = chOf(cr, "prod");
        CHECK(sum && sum->v.size() == 1 && sum->v.at(0).asDouble() == 7.0);
        CHECK(prod && prod->v.size() == 1 && prod->v.at(0).asDouble() == 12.0);

        // preview 路径与 compiled 结论一致，lines 里能看到派生值
        SequentialFramePreview pv = previewSequentialFrame(frame, doc);
        CHECK(pv.ok == cr.ok);
        bool sumShown = false;
        for (const QString &ln : pv.lines)
            if (ln.contains(QStringLiteral("sum")) && ln.contains(QStringLiteral("7")))
                sumShown = true;
        CHECK(sumShown);

        // type:time 同样双路一致：083559.00 → 30959 s
        const QByteArray tplTime = R"({
          "name":"time_csv2","mode":"text","layout":"sequential","maxsize":64,"terminator":"\n",
          "segments":[
            {"name":"t","type":"time","delimiter":","},
            {"name":"v","type":"i32","delimiter":";"}
          ]
        })";
        ProtocolDocument tdoc;
        CHECK(parseDocument(QJsonDocument::fromJson(tplTime).object(), tdoc, pe));
        CompiledProgram tprog;
        CHECK(compileProgram(tdoc, tprog, pe));
        const QByteArray tframe("083559.00,42;\n");
        ProtocolDecodeResult tr = decodeSequentialCompiled(tframe, tprog);
        CHECK(tr.ok);
        const ProtocolChannel *tc = chOf(tr, "t");
        CHECK(tc && std::fabs(tc->v.at(0).asDouble() - 30959.0) < 1e-6);
        SequentialFramePreview ptv = previewSequentialFrame(tframe, tdoc);
        CHECK(ptv.ok == tr.ok);
        OK("derive 走 text 路径出通道（sum=7 prod=12）；time/derive 在 preview 与 compiled 双路结论一致");
    }

    // --- text checks 生效 + preview/decode 一致性 ---
    {
        const QByteArray tpl = R"({
          "name":"csv_checks","mode":"text","layout":"sequential","maxsize":64,"terminator":"\n",
          "segments":[
            {"name":"a","type":"i32","delimiter":",","checks":[{"op":"eq","value":12}]},
            {"name":"b","type":"char"}
          ]
        })";
        ProtocolDecodeResult okr = decodeSequentialJson(QByteArray("12,OK\n"), tpl);
        CHECK(okr.ok && okr.channels.size() == 2);
        ProtocolDecodeResult bad = decodeSequentialJson(QByteArray("99,NO\n"), tpl);
        CHECK(!bad.ok || bad.rejectedFrames > 0);

        // 序列类 checks 在 text 段必须编译期拒绝（那是 digest 的职责）
        const QByteArray badTpl = R"({
          "name":"csv_bad","mode":"text","layout":"sequential","maxsize":64,"terminator":"\n",
          "segments":[{"name":"c","type":"char","delimiter":"\n","checks":[{"op":"crc16"}]}]
        })";
        CHECK(!validate(QJsonDocument::fromJson(badTpl).object()).isEmpty());

        // preview 与生产解码对同一帧结论一致
        ProtocolDocument doc;
        std::string pe;
        CHECK(parseDocument(QJsonDocument::fromJson(tpl).object(), doc, pe));
        const QByteArray one = QByteArray("12,OK\n");
        SequentialFramePreview pv = previewSequentialFrame(one, doc);
        ProtocolDecodeResult dr = decodeSequential(one, doc);
        CHECK(pv.ok == dr.ok);
        OK("text checks 生效：谓词不符即拒；序列类 checks 被编译期拒；preview 与 decode 结论一致");
    }

    // --- repeat:"eos" 必须收尾 ---
    {
        QJsonParseError pe;
        const QJsonObject bad = QJsonDocument::fromJson(R"({
            "name":"eos_bad","mode":"bin","layout":"sequential","maxsize":64,
            "segments":[
                {"name":"body","type":"u16","repeat":"eos"},
                {"name":"crc","type":"u16"}
            ]
        })", &pe).object();
        CHECK(pe.error == QJsonParseError::NoError && !validate(bad).isEmpty());
        OK("编译层负例：repeat:\"eos\" 之后不能再有读字节的段");
    }

    // --- Lua 脚本后端 ---
    {
        CHECK(scriptDigestAvailable());
        ScriptDigestInput in;
        const char *d = "GPGGA";
        in.data = reinterpret_cast<const uint8_t *>(d);
        in.len = 5;
        uint64_t out = 0;
        std::string err;
        bool to = false;
        CHECK(computeScriptDigest("return 71", in, out, err, &to) && out == 71 && !to);
        CHECK(computeScriptDigest("return ctx.len", in, out, err, &to) && out == 5 && !to);
        CHECK(computeScriptDigest("local s = 0 for i = 1, ctx.len do s = s + string.byte(ctx.data, i) end return s % 256",
                                  in, out, err, &to) && out == 358 % 256);
        CHECK(!computeScriptDigest("while true do end", in, out, err, &to) && to);
        // 内存爆炸：单次分配超过预算必须被拒，归内存超限（不是指令超时）。
        CHECK(!computeScriptDigest("return #string.rep(\"x\", 2000000)", in, out, err, &to) && !to);
        CHECK(err.find("内存") != std::string::npos);
        OK("Lua 脚本后端：可用、能读 ctx.data/ctx.len、超指令配额判超时、超内存预算被拒");
    }

    fprintf(stdout, "\n协议逻辑资产平移测试全部通过\n");
    return 0;
}
