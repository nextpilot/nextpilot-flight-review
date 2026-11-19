#include "IMessageParser.h"

#include <QJsonArray>

namespace nextpilot::plotscope::core
{

QVector<nextpilot::plotscope::uischema::ParamSpec> IMessageParser::paramSpecs() const
{
    using nextpilot::plotscope::uischema::ParamSpec;
    QVector<ParamSpec> specs;
    const QJsonArray fields = configSchema().value(QStringLiteral("fields")).toArray();
    for (const QJsonValue &v : fields)
    {
        const QJsonObject f = v.toObject();
        ParamSpec spec;
        spec.key = f.value(QStringLiteral("key")).toString();
        if (spec.key.isEmpty())
            continue;
        spec.type = nextpilot::plotscope::uischema::paramTypeFromName(
            f.value(QStringLiteral("type")).toString());
        const QJsonValue def = f.value(QStringLiteral("default"));
        if (def.isBool())
            spec.defaultValue = def.toBool();
        else if (def.isDouble())
            spec.defaultValue = def.toDouble();
        else
            spec.defaultValue = def.toString();
        if (f.contains(QStringLiteral("min")) && f.contains(QStringLiteral("max")))
        {
            spec.min = f.value(QStringLiteral("min")).toDouble();
            spec.max = f.value(QStringLiteral("max")).toDouble();
            spec.hasRange = true;
        }
        if (f.contains(QStringLiteral("filter")))
            spec.fileFilter = f.value(QStringLiteral("filter")).toString();
        for (const QJsonValue &ov : f.value(QStringLiteral("options")).toArray())
        {
            const QJsonObject o = ov.toObject();
            const QString value = o.value(QStringLiteral("value")).toString();
            spec.options.append({value, o.value(QStringLiteral("label")).toString(value)});
        }
        specs.append(spec);
    }
    return specs;
}

bool IMessageParser::configure(const QJsonObject &params, QString *err)
{
    Q_UNUSED(err);
    // 唯一入口：把参数快照存进实例。派生类可从 m_params 读，但不得回退到全局。
    m_params = params;
    return true;
}

QJsonValue IMessageParser::param(const QString &key, const QJsonValue &fallback) const
{
    // 支持点分嵌套键（"time.mode" → m_params["time"]["mode"]）。
    if (key.isEmpty())
        return fallback;
    const QStringList parts = key.split(QLatin1Char('.'));
    QJsonValue cur(m_params);
    for (const QString &part : parts)
    {
        if (!cur.isObject())
            return fallback;
        cur = cur.toObject().value(part);
        if (cur.isUndefined())
            return fallback;
    }
    return cur;
}

} // namespace nextpilot::plotscope::core
