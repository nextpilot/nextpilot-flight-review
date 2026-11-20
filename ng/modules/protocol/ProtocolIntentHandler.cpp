#include "ProtocolIntentHandler.h"
#include "ErrorCodes.h"

#include "ProtocolTemplateStore.h"

#include "protocol/CompiledProgram.h"
#include "protocol/ProtocolDocument.h"
#include "protocol/ProtocolSpec.h"
#include "protocol/SequentialCodec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QVariantMap>

namespace nextpilot::plotscope::host
{
namespace
{
using nextpilot::plotscope::foundation::Error;
using nextpilot::plotscope::intent::IntentResult;
using nextpilot::plotscope::foundation::Result;
namespace err = nextpilot::plotscope::foundation::err;

Result<IntentResult> ok(const QVariantMap &payload)
{
    IntentResult r;
    r.payload = payload;
    return r;
}

Error bad(const QString &msg)
{
    return nextpilot::plotscope::foundation::fail(err::Protocol, msg);
}

// "AA 55 0C" / "AA550C" → 字节。与编辑器 parseHex 同一口径（空格/逗号/竖线可分隔）。
bool parseHexBytes(const QString &text, QByteArray *out, QString *err)
{
    QString clean;
    for (QChar c : text)
    {
        if (c.isSpace() || c == QLatin1Char(',') || c == QLatin1Char('|'))
            continue;
        clean.append(c);
    }
    if (clean.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        clean = clean.mid(2);
    clean.remove(QStringLiteral("0x"), Qt::CaseInsensitive);
    if (clean.isEmpty())
    {
        *err = QStringLiteral("frame 为空");
        return false;
    }
    if (clean.size() % 2 != 0)
    {
        *err = QStringLiteral("frame 十六进制位数必须是偶数：%1").arg(text);
        return false;
    }
    QByteArray bytes;
    for (int i = 0; i < clean.size(); i += 2)
    {
        bool okByte = false;
        const int b = clean.mid(i, 2).toInt(&okByte, 16);
        if (!okByte)
        {
            *err = QStringLiteral("frame 含非十六进制字符：%1").arg(clean.mid(i, 2));
            return false;
        }
        bytes.append(static_cast<char>(b));
    }
    *out = bytes;
    return true;
}

QJsonObject errorToJson(const nextpilot::plotscope::protocol::DecodeError &e)
{
    QJsonObject o;
    o.insert(QStringLiteral("recordIndex"), static_cast<double>(e.recordIndex));
    o.insert(QStringLiteral("offset"), static_cast<double>(e.recordOffset));
    o.insert(QStringLiteral("segment"), QString::fromStdString(e.segment));
    o.insert(QStringLiteral("detail"), QString::fromStdString(e.detail));
    o.insert(QStringLiteral("warning"), e.warning);
    return o;
}
} // namespace

ProtocolIntentHandler::ProtocolIntentHandler(QObject *parent) : QObject(parent) {}

QStringList ProtocolIntentHandler::supportedOps()
{
    return {QStringLiteral("protocol.validate"), QStringLiteral("protocol.preview"),
            QStringLiteral("protocol.templates.list"), QStringLiteral("protocol.templates.save")};
}

bool ProtocolIntentHandler::canHandle(const nextpilot::plotscope::intent::Intent &intent) const
{
    return supportedOps().contains(intent.op);
}

Result<IntentResult> ProtocolIntentHandler::execute(
    const nextpilot::plotscope::intent::Intent &intent)
{
    if (intent.op == QStringLiteral("protocol.validate"))
        return validateOp(intent.params);
    if (intent.op == QStringLiteral("protocol.preview"))
        return previewOp(intent.params);
    if (intent.op == QStringLiteral("protocol.templates.list"))
        return listOp(intent.params);
    if (intent.op == QStringLiteral("protocol.templates.save"))
        return saveOp(intent.params);
    return bad(QStringLiteral("协议模块不支持意图：%1").arg(intent.op));
}

bool ProtocolIntentHandler::loadTemplate(const QVariantMap &params, QByteArray *out,
                                         QString *err) const
{
    const QString inlineJson = params.value(QStringLiteral("template")).toString();
    if (!inlineJson.trimmed().isEmpty())
    {
        *out = inlineJson.toUtf8();
        return true;
    }
    const QString path = params.value(QStringLiteral("templatePath")).toString();
    if (path.trimmed().isEmpty())
    {
        *err = QStringLiteral("缺少 template 或 templatePath");
        return false;
    }
    return ProtocolTemplateStore::read(path, out, err);
}

Result<IntentResult> ProtocolIntentHandler::validateOp(const QVariantMap &params)
{
    QByteArray json;
    QString err;
    if (!loadTemplate(params, &json, &err))
        return bad(err);

    QJsonParseError perr;
    const QJsonObject root = QJsonDocument::fromJson(json, &perr).object();
    if (perr.error != QJsonParseError::NoError)
    {
        QVariantMap payload;
        payload.insert(QStringLiteral("ok"), false);
        payload.insert(QStringLiteral("stage"), QStringLiteral("json"));
        payload.insert(QStringLiteral("issues"),
                       QStringList{QStringLiteral("JSON 解析失败：%1").arg(perr.errorString())});
        return ok(payload);
    }

    // 分阶段返回：用户改模板时最需要知道的是"卡在哪一层"——
    // 结构不合规（spec）还是能过 spec 但编译不了（compile）。
    QStringList issues = nextpilot::plotscope::protocol::validate(root);
    QString stage = QStringLiteral("spec");
    if (issues.isEmpty())
    {
        nextpilot::plotscope::protocol::ProtocolDocument doc;
        std::string perr2;
        if (!nextpilot::plotscope::protocol::parseDocument(root, doc, perr2))
        {
            issues << QString::fromStdString(perr2);
            stage = QStringLiteral("parse");
        }
        else
        {
            nextpilot::plotscope::protocol::CompiledProgram prog;
            std::string cerr;
            if (!nextpilot::plotscope::protocol::compileProgram(doc, prog, cerr))
            {
                issues << QString::fromStdString(cerr);
                stage = QStringLiteral("compile");
            }
        }
    }

    QVariantMap payload;
    payload.insert(QStringLiteral("ok"), issues.isEmpty());
    payload.insert(QStringLiteral("stage"), issues.isEmpty() ? QStringLiteral("ok") : stage);
    payload.insert(QStringLiteral("issues"), issues);
    payload.insert(QStringLiteral("name"), root.value(QStringLiteral("name")).toString());
    payload.insert(QStringLiteral("mode"), root.value(QStringLiteral("mode")).toString());
    const QJsonArray segs = root.value(QStringLiteral("segments")).toArray();
    payload.insert(QStringLiteral("segmentCount"), segs.size());
    return ok(payload);
}

Result<IntentResult> ProtocolIntentHandler::previewOp(const QVariantMap &params)
{
    QByteArray json;
    QString err;
    if (!loadTemplate(params, &json, &err))
        return bad(err);

    QByteArray frame;
    QString frameErr;
    // 也接受 base64，脚本侧常常直接持有二进制。
    const QString b64 = params.value(QStringLiteral("frameBase64")).toString();
    if (!b64.trimmed().isEmpty())
        frame = QByteArray::fromBase64(b64.toUtf8());
    else if (!parseHexBytes(params.value(QStringLiteral("frame")).toString(), &frame, &frameErr))
    {
        // 没给 frame 时退化成"只校验模板"，让调用方一次调用拿到两件事。
        if (params.value(QStringLiteral("frame")).toString().trimmed().isEmpty())
            return validateOp(params);
        return bad(frameErr);
    }

    QJsonParseError perr;
    const QJsonObject root = QJsonDocument::fromJson(json, &perr).object();
    if (perr.error != QJsonParseError::NoError)
        return bad(QStringLiteral("模板 JSON 解析失败：%1").arg(perr.errorString()));

    const QStringList issues = nextpilot::plotscope::protocol::validate(root);
    if (!issues.isEmpty())
        return bad(QStringLiteral("模板校验失败：%1").arg(issues.join(QStringLiteral("；"))));

    nextpilot::plotscope::protocol::ProtocolDocument doc;
    std::string derr;
    if (!nextpilot::plotscope::protocol::parseDocument(root, doc, derr))
        return bad(QStringLiteral("模板解析失败：%1").arg(QString::fromStdString(derr)));

    const auto preview = nextpilot::plotscope::protocol::previewSequentialFrame(frame, doc);

    QVariantMap payload;
    payload.insert(QStringLiteral("ok"), preview.ok);
    payload.insert(QStringLiteral("lines"), preview.lines);
    payload.insert(QStringLiteral("error"), preview.error);
    payload.insert(QStringLiteral("frameBytes"), frame.size());
    return ok(payload);
}

Result<IntentResult> ProtocolIntentHandler::listOp(const QVariantMap &)
{
    QString err;
    const QVector<QJsonObject> items = ProtocolTemplateStore::list(&err);

    QJsonArray arr;
    for (const QJsonObject &o : items)
        arr.append(o);

    QVariantMap payload;
    payload.insert(QStringLiteral("dir"), ProtocolTemplateStore::directory());
    payload.insert(QStringLiteral("suffix"), ProtocolTemplateStore::suffix());
    payload.insert(QStringLiteral("items"), arr.toVariantList());
    payload.insert(QStringLiteral("count"), items.size());
    if (!err.isEmpty())
        payload.insert(QStringLiteral("error"), err);
    return ok(payload);
}

Result<IntentResult> ProtocolIntentHandler::saveOp(const QVariantMap &params)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    if (name.trimmed().isEmpty())
        return bad(QStringLiteral("缺少 name（模板名，不含 .psproto）"));

    QByteArray json;
    QString err;
    if (!loadTemplate(params, &json, &err))
        return bad(err);

    QString savedPath;
    if (!ProtocolTemplateStore::save(name, json, &savedPath, &err))
        return bad(err);

    QVariantMap payload;
    payload.insert(QStringLiteral("ok"), true);
    payload.insert(QStringLiteral("path"), savedPath);
    payload.insert(QStringLiteral("name"), name.trimmed());
    return ok(payload);
}

} // namespace nextpilot::plotscope::host
