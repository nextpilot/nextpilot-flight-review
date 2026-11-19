#include "TransformEvaluator.h"

#include "LuaEngine.h"
#include "Transform.h"

extern "C" {
#include <ps_kernel.h>
}

#include <QJsonDocument>

using namespace nextpilot::plotscope::core;

namespace nextpilot::plotscope::script
{

namespace
{
// 内置算子 id "builtin.scale" -> Rust filter id "scale"；"expression" -> "expression"。
QString rustFilterId(const TransformDescriptor &d)
{
    const QString prefix = QStringLiteral("builtin.");
    if (d.id.startsWith(prefix))
        return d.id.mid(prefix.size());
    if (d.kind == TransformKind::Expression)
        return QStringLiteral("expression");
    return d.id;
}

// builtin/expression 走 Rust：调用纯函数 ps_filter_apply_series。
Result<QVector<double>> evalRust(const TransformDescriptor &d, const TransformRequest &req,
                                 const QVector<double> &t, const QVector<double> &v,
                                 uint32_t /*cap*/)
{
    const QString fid = rustFilterId(d);
    QJsonObject params = req.params;
    // 表达式：expr 文本放进 params.expr（Rust expression 算子从那读）。
    if (d.kind == TransformKind::Expression && !req.expression.isEmpty())
        params.insert(QStringLiteral("expr"), req.expression);

    const QByteArray fidUtf8 = fid.toUtf8();
    const QByteArray paramsJson = QJsonDocument(params).toJson(QJsonDocument::Compact);
    const quint32 n = static_cast<quint32>(qMin(t.size(), v.size()));
    QVector<double> outT(n), outV(n);
    quint32 outN = 0;
    char errBuf[512] = {0};

    // 先探测真实长度（cap=0 -> rc=6，out_n 给出实际长度）。
    double dummy = 0.0;
    quint32 probeN = 0;
    int rc = ps_filter_apply_series(fidUtf8.constData(), paramsJson.constData(),
                                     t.isEmpty() ? nullptr : t.constData(),
                                     v.isEmpty() ? nullptr : v.constData(), n,
                                     &dummy, &dummy, 0, &probeN, errBuf, sizeof(errBuf));
    if (rc != 0 && rc != 6)
        return fail(QStringLiteral("erust"), QString::fromUtf8(errBuf));

    const quint32 cap = qMin(probeN, n);
    if (cap == 0)
        return Result<QVector<double>>(QVector<double>());

    outT.resize(cap);
    outV.resize(cap);
    rc = ps_filter_apply_series(fidUtf8.constData(), paramsJson.constData(),
                                 t.isEmpty() ? nullptr : t.constData(),
                                 v.isEmpty() ? nullptr : v.constData(), n,
                                 outT.data(), outV.data(), cap, &outN, errBuf, sizeof(errBuf));
    if (rc != 0 && rc != 6)
        return fail(QStringLiteral("erust"), QString::fromUtf8(errBuf));
    outV.resize(qMin<quint32>(outN, cap));
    return Result<QVector<double>>(outV);
}

Result<QVector<double>> evalLua(const TransformDescriptor &d, const TransformRequest &req,
                                const QVector<double> &t, const QVector<double> &v,
                                uint32_t /*cap*/)
{
    QByteArray source = req.luaSource.toUtf8();
    if (source.isEmpty() && !req.scriptId.isEmpty())
    {
        // scriptId 引用的脚本源码由调用方通过 extra 注入（在线调试/编辑时直接给 luaSource）。
        return fail(QStringLiteral("enoscript"),
                    QStringLiteral("Lua 脚本引用 %1 未提供源码，请在请求里带 luaSource").arg(req.scriptId));
    }
    if (source.isEmpty())
        return fail(QStringLiteral("enolua"), QStringLiteral("Lua 变换需要代码或脚本源码"));

    LuaTransform lt(source, req.params, QStringLiteral("=transform.%1").arg(d.id));
    if (!lt.valid())
        return fail(QStringLiteral("elua"), lt.error());

    QVector<double> out;
    QString err;
    if (!lt.evalSeries(t, v, &out, &err))
        return fail(QStringLiteral("elua"), err);
    return Result<QVector<double>>(out);
}

} // namespace

QJsonObject TransformEvaluator::makeRequest(const QString &descriptorId,
                                            const QString &expression,
                                            const QString &luaSource,
                                            const QString &scriptId,
                                            const QJsonObject &params)
{
    TransformRequest req;
    req.descriptorId = descriptorId;
    const TransformDescriptor d = TransformRegistry::get(descriptorId);
    req.kind = d.kind;
    req.expression = expression;
    req.luaSource = luaSource;
    req.scriptId = scriptId;
    req.params = params;
    return req.toJson();
}

Result<double> TransformEvaluator::evalScalar(const QJsonObject &requestJson, uint32_t cap,
                                              double x, double t, int index)
{
    const TransformRequest req = TransformRequest::fromJson(requestJson);
    const TransformDescriptor d = TransformRegistry::get(req.descriptorId);
    if (d.id.isEmpty())
        return fail(QStringLiteral("enodesc"),
                    QStringLiteral("未知变换 descriptor: %1").arg(req.descriptorId));
    if (!d.supports(static_cast<TransformCapability>(cap)))
        return fail(QStringLiteral("ecap"),
                    QStringLiteral("变换「%1」不支持该场景（能力：%2，需要：%3）。选错变换已被挡住。")
                        .arg(d.label, capabilityNames(d.capabilities), capabilityNames(cap)));

    QVector<double> tv{x}, vv{x};
    Result<QVector<double>> r = d.kind == TransformKind::Lua
        ? evalLua(d, req, tv, vv, cap)
        : evalRust(d, req, tv, vv, cap);
    if (!r)
        return fail(r.error().code, r.error().message);
    const QVector<double> &out = r.value();
    return Result<double>(out.isEmpty() ? 0.0 : out.first());
}

Result<QVector<double>> TransformEvaluator::evalSeries(const QJsonObject &requestJson, uint32_t cap,
                                                       const QVector<double> &t,
                                                       const QVector<double> &v)
{
    const TransformRequest req = TransformRequest::fromJson(requestJson);
    const TransformDescriptor d = TransformRegistry::get(req.descriptorId);
    if (d.id.isEmpty())
        return fail(QStringLiteral("enodesc"),
                    QStringLiteral("未知变换 descriptor: %1").arg(req.descriptorId));
    if (!d.supports(static_cast<TransformCapability>(cap)))
        return fail(QStringLiteral("ecap"),
                    QStringLiteral("变换「%1」不支持该场景（能力：%2）。")
                        .arg(d.label, capabilityNames(d.capabilities)));

    return d.kind == TransformKind::Lua
        ? evalLua(d, req, t, v, cap)
        : evalRust(d, req, t, v, cap);
}

} // namespace nextpilot::plotscope::script
