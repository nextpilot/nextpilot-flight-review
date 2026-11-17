#include "Intent.h"

#include <QJsonArray>
#include <QJsonValue>

namespace ng::core
{

void Job::append(Intent intent)
{
    m_intents.push_back(std::move(intent));
}

QJsonObject Job::toJson() const
{
    QJsonArray arr;
    for (const Intent &intent : m_intents)
    {
        QJsonObject item;
        item[QStringLiteral("op")] = intent.op;
        QJsonObject params;
        for (auto it = intent.params.constBegin(); it != intent.params.constEnd(); ++it)
            params[it.key()] = QJsonValue::fromVariant(it.value());
        item[QStringLiteral("params")] = params;
        arr.append(item);
    }
    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("intents")] = arr;
    return root;
}

Job Job::fromJson(const QJsonObject &obj, Result<void> *outError)
{
    Job job;
    const QJsonArray arr = obj.value(QStringLiteral("intents")).toArray();
    for (const QJsonValue &v : arr)
    {
        const QJsonObject item = v.toObject();
        Intent intent;
        intent.op = item.value(QStringLiteral("op")).toString();
        if (intent.op.isEmpty())
        {
            if (outError)
                *outError = fail(QStringLiteral("eintent"), QStringLiteral("意图缺少 op"));
            return job;
        }
        const QJsonObject params = item.value(QStringLiteral("params")).toObject();
        for (auto it = params.constBegin(); it != params.constEnd(); ++it)
            intent.params[it.key()] = it.value().toVariant();
        job.append(std::move(intent));
    }
    if (outError)
        *outError = Result<void>();
    return job;
}

} // namespace ng::core
