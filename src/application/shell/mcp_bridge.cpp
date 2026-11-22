#include "mcp_bridge.h"

#include "App.h"
#include "Intent.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QVariant>

using namespace nextpilot::plotscope::app;
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::intent;

namespace nextpilot::plotscope::mcp
{

QJsonObject errorResponse(const QJsonValue &id, int code, const QString &message)
{
    QJsonObject resp;
    resp[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    resp[QStringLiteral("id")] = id;
    QJsonObject err;
    err[QStringLiteral("code")] = code;
    err[QStringLiteral("message")] = message;
    resp[QStringLiteral("error")] = err;
    return resp;
}

namespace
{

QJsonObject resultResponse(const QJsonValue &id, const QJsonObject &result)
{
    QJsonObject resp;
    resp[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    resp[QStringLiteral("id")] = id;
    resp[QStringLiteral("result")] = result;
    return resp;
}

// ai.tools.list 的参数描述（name/type/required/description）→ MCP inputSchema。
// 类型映射：string→string、int→integer、bool→boolean、number→double、其余→object。
QJsonObject buildInputSchema(const QVariantList &parameters)
{
    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");
    QJsonObject props;
    QJsonArray required;
    for (const QVariant &pv : parameters)
    {
        const QVariantMap p = pv.toMap();
        const QString name = p.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;
        const QString type = p.value(QStringLiteral("type"), QStringLiteral("string")).toString();
        QString jsonType;
        if (type == QLatin1String("string") || type == QLatin1String("path"))
            jsonType = QStringLiteral("string");
        else if (type == QLatin1String("int") || type == QLatin1String("integer"))
            jsonType = QStringLiteral("integer");
        else if (type == QLatin1String("bool") || type == QLatin1String("boolean"))
            jsonType = QStringLiteral("boolean");
        else if (type == QLatin1String("number") || type == QLatin1String("double") || type == QLatin1String("float"))
            jsonType = QStringLiteral("number");
        else
            jsonType = QStringLiteral("object");
        QJsonObject prop;
        prop[QStringLiteral("type")] = jsonType;
        const QString desc = p.value(QStringLiteral("description")).toString();
        if (!desc.isEmpty())
            prop[QStringLiteral("description")] = desc;
        props[name] = prop;
        if (p.value(QStringLiteral("required"), false).toBool())
            required.append(name);
    }
    schema[QStringLiteral("properties")] = props;
    if (!required.isEmpty())
        schema[QStringLiteral("required")] = required;
    return schema;
}

QJsonObject executeIntent(app::App &application, const QString &op, const QVariantMap &params,
                          Error *outError)
{
    Intent intent;
    intent.op = op;
    intent.params = params;
    const auto res = application.host().execute(intent);
    if (!res)
    {
        if (outError)
            *outError = res.error();
        return {};
    }
    QJsonObject payload = QJsonObject::fromVariantMap(res.value().payload.toMap());
    if (payload.isEmpty() && res.value().payload.isValid())
    {
        // 非表 payload（字符串/数值）包一层，保证 MCP content 恒为 JSON。
        payload[QStringLiteral("value")] = QJsonValue::fromVariant(res.value().payload);
    }
    return payload;
}

} // namespace

QJsonObject handleRequest(const QJsonObject &req, app::App &application)
{
    // notification（无 id）：处理副作用但不回包。本桥只读+执行，副作用型
    // notification 一律忽略。
    const QString method = req.value(QStringLiteral("method")).toString();
    const bool isNotification = !req.contains(QStringLiteral("id"));
    const QJsonValue id = req.value(QStringLiteral("id"));

    if (isNotification)
        return {};

    if (method == QLatin1String("initialize"))
    {
        QJsonObject info;
        info[QStringLiteral("name")] = QStringLiteral("plotscope");
        info[QStringLiteral("version")] = QStringLiteral("0.1.0");
        QJsonObject caps;
        caps[QStringLiteral("tools")] = QJsonObject{ { QStringLiteral("listChanged"), false } };
        QJsonObject result;
        result[QStringLiteral("protocolVersion")] = QStringLiteral("2024-11-05");
        result[QStringLiteral("capabilities")] = caps;
        result[QStringLiteral("serverInfo")] = info;
        return resultResponse(id, result);
    }

    if (method == QLatin1String("ping"))
        return resultResponse(id, QJsonObject{});

    if (method == QLatin1String("tools/list"))
    {
        Error err;
        const QJsonObject payload = executeIntent(application, QStringLiteral("ai.tools.list"), {}, &err);
        if (payload.isEmpty())
            return errorResponse(id, -32603, err.message);
        QJsonArray tools;
        const QVariantList catalog = payload.value(QStringLiteral("tools")).toVariant().toList();
        for (const QVariant &tv : catalog)
        {
            const QVariantMap t = tv.toMap();
            QJsonObject tool;
            tool[QStringLiteral("name")] = t.value(QStringLiteral("name")).toString();
            tool[QStringLiteral("description")] = t.value(QStringLiteral("description")).toString();
            tool[QStringLiteral("inputSchema")] =
                buildInputSchema(t.value(QStringLiteral("parameters")).toList());
            tools.append(tool);
        }
        return resultResponse(id, QJsonObject{ { QStringLiteral("tools"), tools } });
    }

    if (method == QLatin1String("tools/call"))
    {
        const QJsonObject params = req.value(QStringLiteral("params")).toObject();
        const QString name = params.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            return errorResponse(id, -32602, QStringLiteral("tools/call 需要 name 参数"));
        QVariantMap callParams;
        callParams[QStringLiteral("tool")] = name;
        // MCP 参数打包进 args（ai.tool.call 接受 QVariantMap 或 JSON 字符串两种来路）。
        callParams[QStringLiteral("args")] = params.value(QStringLiteral("arguments")).toObject().toVariantMap();

        Error err;
        const QJsonObject payload = executeIntent(application, QStringLiteral("ai.tool.call"), callParams, &err);
        if (payload.isEmpty())
        {
            // 工具执行失败：MCP 语义是 result.isError 而非协议错误（代理能读到原因）。
            QJsonObject content;
            content[QStringLiteral("type")] = QStringLiteral("text");
            content[QStringLiteral("text")] =
                QStringLiteral("[%1] %2").arg(err.code, err.message);
            QJsonObject result;
            result[QStringLiteral("isError")] = true;
            result[QStringLiteral("content")] = QJsonArray{ content };
            return resultResponse(id, result);
        }

        QJsonObject content;
        content[QStringLiteral("type")] = QStringLiteral("text");
        content[QStringLiteral("text")] =
            QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
        QJsonObject result;
        result[QStringLiteral("isError")] = false;
        result[QStringLiteral("content")] = QJsonArray{ content };
        return resultResponse(id, result);
    }

    return errorResponse(id, -32601, QStringLiteral("method 未实现: %1").arg(method));
}

} // namespace nextpilot::plotscope::mcp
