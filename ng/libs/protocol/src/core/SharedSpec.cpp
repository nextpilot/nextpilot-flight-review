#include "protocol/ProtocolSpec.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace nextpilot::plotscope::protocol
{

namespace
{

// 表达式保留字：数学函数与常量、逻辑词、self。引用校验时豁免。
const QSet<QString> &reservedIdents()
{
    static const QSet<QString> kSet = {
        QStringLiteral("self"), QStringLiteral("pi"), QStringLiteral("e"),
        QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("and"),
        QStringLiteral("or"), QStringLiteral("not"),
        QStringLiteral("sin"), QStringLiteral("cos"), QStringLiteral("tan"),
        QStringLiteral("asin"), QStringLiteral("acos"), QStringLiteral("atan"),
        QStringLiteral("sqrt"), QStringLiteral("log"), QStringLiteral("ln"),
        QStringLiteral("exp"), QStringLiteral("abs"), QStringLiteral("floor"),
        QStringLiteral("ceil"), QStringLiteral("round"), QStringLiteral("min"),
        QStringLiteral("max"), QStringLiteral("pow"), QStringLiteral("mod")};
    return kSet;
}

} // namespace

QStringList exprIdentifiers(const QString &expr)
{
    QStringList out;
    static const QRegularExpression re(QStringLiteral("[A-Za-z_][A-Za-z0-9_]*"));
    auto it = re.globalMatch(expr);
    while (it.hasNext())
    {
        const QString id = it.next().captured();
        if (!out.contains(id))
            out << id;
    }
    return out;
}

void checkExprRefs(const QString &expr, const QString &owner, const SpecContext &ctx, int selfIndex,
                   QStringList &errs)
{
    if (expr.trimmed().isEmpty())
    {
        errs << QStringLiteral("%1 的表达式为空").arg(owner);
        return;
    }
    const QSet<QString> reserved = reservedIdents();
    // 十六进制字面量里的 a-f 不应被当成标识符：先剥掉 0x... 片段再扫。
    QString stripped = expr;
    static const QRegularExpression hexRe(QStringLiteral("0[xX][0-9a-fA-F]+"));
    stripped.replace(hexRe, QStringLiteral("0"));
    for (const QString &id : exprIdentifiers(stripped))
    {
        if (reserved.contains(id))
            continue;
        const auto it = ctx.order.find(id);
        if (it == ctx.order.end())
        {
            errs << QStringLiteral("%1 的表达式引用了未知符号：%2").arg(owner, id);
            continue;
        }
        if (selfIndex >= 0 && it.value() >= selfIndex)
            errs << QStringLiteral("%1 的表达式只能引用前序段，%2 在后").arg(owner, id);
    }
}

void validateShared(const QJsonObject &root, SpecContext &ctx)
{
    QStringList &errs = ctx.errs;

    // term：帧尾候选集，写了就硬校验。
    const QJsonValue tv = root.contains(QStringLiteral("term")) ? root.value(QStringLiteral("term"))
                                                                : root.value(QStringLiteral("terminator"));
    if (!tv.isUndefined() && !tv.isNull())
    {
        QStringList list;
        if (tv.isArray())
        {
            for (const QJsonValue &v : tv.toArray())
                list << v.toString();
        }
        else
            list << tv.toString();
        if (list.isEmpty())
            errs << QStringLiteral("term 不能为空数组");
        for (const QString &d : list)
            if (d.isEmpty())
                errs << QStringLiteral("term 含空串");
    }

    // timestamp
    const QJsonObject ts = root.value(QStringLiteral("timestamp")).toObject();
    if (!ts.isEmpty())
    {
        const QString expr = ts.value(QStringLiteral("expr")).toString().trimmed();
        if (expr.isEmpty())
            errs << QStringLiteral("timestamp.expr 不能为空");
        else
            checkExprRefs(expr, QStringLiteral("timestamp"), ctx, -1, errs);
        const QString unit = ts.value(QStringLiteral("unit")).toString(QStringLiteral("s"));
        if (unit != QLatin1String("s") && unit != QLatin1String("ms") && unit != QLatin1String("us") &&
            unit != QLatin1String("ns"))
            errs << QStringLiteral("timestamp.unit 只能是 s/ms/us/ns");
        const QString origin = ts.value(QStringLiteral("origin")).toString(QStringLiteral("first_frame"));
        if (origin != QLatin1String("first_frame") && origin != QLatin1String("absolute"))
            errs << QStringLiteral("timestamp.origin 只能是 first_frame/absolute");
        const QString fb = ts.value(QStringLiteral("fallback")).toString(QStringLiteral("reject"));
        if (fb != QLatin1String("reject") && fb != QLatin1String("index"))
            errs << QStringLiteral("timestamp.fallback 只能是 reject/index");
    }

    // series
    const QJsonObject series = root.value(QStringLiteral("series")).toObject();
    if (!series.isEmpty())
    {
        const QString selector = series.value(QStringLiteral("selector")).toString().trimmed();
        if (selector.isEmpty())
            errs << QStringLiteral("series.selector 不能为空");
        else
            checkExprRefs(selector, QStringLiteral("series.selector"), ctx, -1, errs);

        const QString payload = series.value(QStringLiteral("payload")).toString().trimmed();
        if (payload.isEmpty())
            errs << QStringLiteral("series.payload 不能为空");
        else if (!ctx.names.contains(payload))
            errs << QStringLiteral("series.payload 引用了未知段：%1").arg(payload);
        else if (ctx.segTypes.value(payload) != QLatin1String("u8"))
            errs << QStringLiteral("series.payload 段 %1 必须是 u8").arg(payload);

        const QString fallback = series.value(QStringLiteral("fallback")).toString(QStringLiteral("reject"));
        if (fallback != QLatin1String("reject") && fallback != QLatin1String("raw"))
            errs << QStringLiteral("series.fallback 只能是 reject 或 raw");

        const QJsonArray vars = series.value(QStringLiteral("variants")).toArray();
        if (vars.isEmpty())
            errs << QStringLiteral("series 至少需要一个变体");

        QSet<QString> varNames;
        QSet<QString> varValues;
        int valueCount = 0;
        auto addVariantKey = [&](const QJsonValue &x) {
            varValues.insert(x.isString() ? QStringLiteral("s:") + x.toString()
                                          : QStringLiteral("n:") + QString::number(x.toDouble(), 'g', 17));
            ++valueCount;
        };
        const int payloadBytes = ctx.fixedBytes.value(payload, -1);
        for (const QJsonValue &vv : vars)
        {
            const QJsonObject var = vv.toObject();
            const QString vn = var.value(QStringLiteral("name")).toString().trimmed();
            if (vn.isEmpty())
                errs << QStringLiteral("变体缺少 name");
            else if (varNames.contains(vn))
                errs << QStringLiteral("变体名重复：%1").arg(vn);
            varNames.insert(vn);
            if (var.contains(QStringLiteral("values")))
            {
                for (const QJsonValue &x : var.value(QStringLiteral("values")).toArray())
                    addVariantKey(x);
            }
            else if (var.contains(QStringLiteral("value")))
                addVariantKey(var.value(QStringLiteral("value")));
            else
                errs << QStringLiteral("变体 %1 缺少 value/values").arg(vn);
        }
        if (valueCount > 0 && varValues.size() != valueCount)
            errs << QStringLiteral("变体 value/values 有重复");
    }

    // types 别名
    const QJsonObject types = root.value(QStringLiteral("types")).toObject();
    for (auto it = types.begin(); it != types.end(); ++it)
    {
        if (!QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$")).match(it.key()).hasMatch())
            errs << QStringLiteral("types 别名不是标识符：%1").arg(it.key());
        const QJsonObject alias = it.value().toObject();
        TypeInfo ti;
        std::string perr;
        if (!parseType(alias.value(QStringLiteral("type")).toString().toStdString(), ti, perr))
            errs << QStringLiteral("types.%1 的 type 未知：%2").arg(it.key(), perr.c_str());
    }

    // 固定段总字节不能超过 maxsize
    if (!ctx.hasVariable)
    {
        int total = 0;
        for (auto it = ctx.fixedBytes.begin(); it != ctx.fixedBytes.end(); ++it)
            total += it.value();
        if (total > ctx.maxsize)
            errs << QStringLiteral("固定段总字节 %1 超过 maxsize %2").arg(total).arg(ctx.maxsize);
    }
}

} // namespace nextpilot::plotscope::protocol
