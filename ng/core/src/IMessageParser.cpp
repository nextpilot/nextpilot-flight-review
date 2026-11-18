#include "IMessageParser.h"

namespace ng::core
{

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

} // namespace ng::core
