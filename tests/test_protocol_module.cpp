// 断言处理：Release(NDEBUG) 下 assert 失效，统一用 CHECK。
#include "ProtocolIntentHandler.h"
#include "protocol/ProtocolTemplateStore.h"

#include "Intent.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVariantMap>

#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            return 1;                                                            \
        }                                                                        \
    } while (0)

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::host;
using namespace nextpilot::plotscope::protocol;

namespace
{

Intent make(const QString &op, const QVariantMap &params = {})
{
    Intent i;
    i.op = op;
    i.params = params;
    return i;
}

QVariantMap payloadOf(ProtocolIntentHandler &h, const Intent &i)
{
    const auto r = h.execute(i);
    if (!r.ok())
        return QVariantMap{{QStringLiteral("__error"), r.error().message}};
    return r.value().payload.toMap();
}

const char *kBinTpl = R"({"name":"mod_test","mode":"bin","layout":"sequential","maxsize":16,
  "segments":[{"name":"a","type":"u8"},{"name":"b","type":"u16"}]})";

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // 模板库落在临时目录，测试不污染用户配置。
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    qputenv("XDG_CONFIG_HOME", tmp.path().toUtf8());

    ProtocolIntentHandler h;

    // 1) op 认领：四个 op 之外不认领（避免与别的模块抢意图）。
    for (const QString &op : ProtocolIntentHandler::supportedOps())
        CHECK(h.canHandle(make(op)));
    CHECK(!h.canHandle(make(QStringLiteral("data.load"))));
    CHECK(!h.canHandle(make(QStringLiteral("protocol.unknown"))));
    fprintf(stdout, "[ok] 协议模块认领 4 个 op，不越界认领其它意图\n");

    // 2) validate：合法模板 → ok/stage=ok；非法模板 → ok=false 且带 issues 与阶段。
    {
        const auto good = payloadOf(h, make(QStringLiteral("protocol.validate"),
                                            {{QStringLiteral("template"), QString::fromUtf8(kBinTpl)}}));
        CHECK(good.value(QStringLiteral("ok")).toBool());
        CHECK(good.value(QStringLiteral("stage")).toString() == QStringLiteral("ok"));
        CHECK(good.value(QStringLiteral("mode")).toString() == QStringLiteral("bin"));
        CHECK(good.value(QStringLiteral("segmentCount")).toInt() == 2);

        // 缺 mode：必须在 spec 阶段就被拒，而不是留到 parse/compile。
        const auto badMode = payloadOf(
            h, make(QStringLiteral("protocol.validate"),
                    {{QStringLiteral("template"),
                      QStringLiteral(R"({"name":"x","layout":"sequential","segments":[{"name":"a","type":"u8"}]})")}}));
        CHECK(!badMode.value(QStringLiteral("ok")).toBool());
        CHECK(badMode.value(QStringLiteral("stage")).toString() == QStringLiteral("spec"));
        CHECK(!badMode.value(QStringLiteral("issues")).toStringList().isEmpty());

        // 断 JSON：stage=json，不能抛异常、不能空错误。
        const auto badJson = payloadOf(
            h, make(QStringLiteral("protocol.validate"),
                    {{QStringLiteral("template"), QStringLiteral("{\"name\":")}}));
        CHECK(!badJson.value(QStringLiteral("ok")).toBool());
        CHECK(badJson.value(QStringLiteral("stage")).toString() == QStringLiteral("json"));

        // 缺 template 与 templatePath：必须明确失败，不许静默成功。
        const auto none = h.execute(make(QStringLiteral("protocol.validate")));
        CHECK(!none.ok());
        CHECK(!none.error().message.isEmpty());
    }
    fprintf(stdout, "[ok] validate：合法/缺 mode/坏 JSON/缺参数 四路都有明确阶段与原因\n");

    // 3) preview：hex 单帧解出字段行；不匹配时给出原因；缺 frame 时退化为校验。
    {
        const auto pv = payloadOf(h, make(QStringLiteral("protocol.preview"),
                                          {{QStringLiteral("template"), QString::fromUtf8(kBinTpl)},
                                           {QStringLiteral("frame"), QStringLiteral("11 2233")}}));
        CHECK(pv.value(QStringLiteral("ok")).toBool());
        CHECK(pv.value(QStringLiteral("lines")).toStringList().size() >= 2);
        CHECK(pv.value(QStringLiteral("frameBytes")).toInt() == 3);

        // base64 入口与 hex 等价（脚本侧常用）。
        const auto pv64 = payloadOf(
            h, make(QStringLiteral("protocol.preview"),
                    {{QStringLiteral("template"), QString::fromUtf8(kBinTpl)},
                     {QStringLiteral("frameBase64"), QStringLiteral("ESIz")}}));
        CHECK(pv64.value(QStringLiteral("ok")).toBool());

        // 位数奇数：明确报错，不是静默截断。
        const auto badHex = h.execute(make(QStringLiteral("protocol.preview"),
                                           {{QStringLiteral("template"), QString::fromUtf8(kBinTpl)},
                                            {QStringLiteral("frame"), QStringLiteral("ABC")}}));
        CHECK(!badHex.ok());

        // 缺 frame：退化成 validate，返回 ok/stage 而不是报"缺参数"。
        const auto noFrame = payloadOf(h, make(QStringLiteral("protocol.preview"),
                                               {{QStringLiteral("template"), QString::fromUtf8(kBinTpl)}}));
        CHECK(noFrame.contains(QStringLiteral("stage")));
    }
    fprintf(stdout, "[ok] preview：hex/base64 双入口、奇数位报错、缺帧退化为校验\n");

    // 4) templates.save + list：先校验后落盘；非法模板不落地；列表能读回。
    {
        const auto saved = payloadOf(h, make(QStringLiteral("protocol.templates.save"),
                                             {{QStringLiteral("name"), QStringLiteral("mod-roundtrip")},
                                              {QStringLiteral("template"), QString::fromUtf8(kBinTpl)}}));
        CHECK(saved.value(QStringLiteral("ok")).toBool());
        const QString path = saved.value(QStringLiteral("path")).toString();
        CHECK(QFile::exists(path));
        CHECK(path.endsWith(QStringLiteral(".psproto")));

        const auto listed = payloadOf(h, make(QStringLiteral("protocol.templates.list")));
        CHECK(listed.value(QStringLiteral("count")).toInt() >= 1);
        bool found = false;
        for (const QVariant &v : listed.value(QStringLiteral("items")).toList())
        {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("name")).toString() == QStringLiteral("mod-roundtrip"))
                found = true;
        }
        CHECK(found);

        // 非法模板：save 必须拒，且不得在目录里留下文件。
        const auto bad = h.execute(
            make(QStringLiteral("protocol.templates.save"),
                 {{QStringLiteral("name"), QStringLiteral("mod-bad")},
                  {QStringLiteral("template"),
                   QStringLiteral(R"({"name":"x","segments":[{"name":"a","type":"u8"}]})")}}));
        CHECK(!bad.ok());
        CHECK(!QFile::exists(QDir(ProtocolTemplateStore::directory())
                                 .absoluteFilePath(QStringLiteral("mod-bad.psproto"))));

        // 名里带路径分隔符：拒绝，不许写到目录外。
        const auto traversal = h.execute(
            make(QStringLiteral("protocol.templates.save"),
                 {{QStringLiteral("name"), QStringLiteral("../../evil")},
                  {QStringLiteral("template"), QString::fromUtf8(kBinTpl)}}));
        CHECK(!traversal.ok());

        // 从模板库路径读回并校验（templatePath 入口，而不是内联）。
        const auto byPath = payloadOf(
            h, make(QStringLiteral("protocol.validate"), {{QStringLiteral("templatePath"), path}}));
        CHECK(byPath.value(QStringLiteral("ok")).toBool());
    }
    fprintf(stdout, "[ok] templates：save 原子落地 + list 读回 + 非法/越界名被拒\n");

    fprintf(stdout, "[ok] 协议模块意图面验收通过\n");
    return 0;
}
