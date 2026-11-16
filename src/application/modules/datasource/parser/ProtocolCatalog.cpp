#include "datasource/ProtocolCatalog.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace
{

QString messageKey(const QString &protocol, int messageId)
{
    return protocol + QLatin1Char(':') + QString::number(messageId);
}

QString nameKey(const QString &protocol, const QString &name)
{
    return protocol + QStringLiteral(":name:") + name;
}

PsFieldDescriptor readField(const QJsonObject &object)
{
    PsFieldDescriptor field;
    field.name = object.value(QStringLiteral("name")).toString();
    field.type = object.value(QStringLiteral("type")).toString();
    field.description = object.value(QStringLiteral("description")).toString();
    field.enumId = object.value(QStringLiteral("enumID")).toString();

    const QJsonObject dataType = object.value(QStringLiteral("dataType")).toObject();
    field.dataType = dataType.value(QStringLiteral("name")).toString();
    field.length = dataType.value(QStringLiteral("length")).toInt();
    field.arrayLength = object.value(QStringLiteral("arrayLength")).toInt(0);
    return field;
}

} // namespace

bool ProtocolCatalog::load(const QString &path, const QString &protocol, QString *error)
{
    m_messages.clear();
    m_byName.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot open %1").arg(path);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = parseError.errorString();
        return false;
    }

    const QJsonArray messages = document.object().value(QStringLiteral("messages")).toArray();
    for (const QJsonValue &value : messages) {
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        const int messageId = object.value(QStringLiteral("messageID")).toInt(-1);
        const QString variant = object.value(QStringLiteral("latestMsgDefCrc")).toString();
        const QJsonObject fieldsObject = object.value(QStringLiteral("fields")).toObject();
        if (name.isEmpty() || messageId < 0 || fieldsObject.isEmpty())
            continue;

        for (const QString &variant : fieldsObject.keys()) {
            const QJsonArray fields = fieldsObject.value(variant).toArray();
            if (fields.isEmpty())
                continue;

            PsMessageDescriptor descriptor;
            descriptor.protocol = protocol;
            descriptor.name = name;
            descriptor.messageId = messageId;
            descriptor.variant = variant;
            for (const QJsonValue &fieldValue : fields)
                descriptor.fields.append(readField(fieldValue.toObject()));

            m_messages[messageKey(protocol, messageId)].append(descriptor);
            m_byName[nameKey(protocol, descriptor.name)].append(descriptor);
        }
    }
    return true;
}

const PsMessageDescriptor *ProtocolCatalog::find(const QString &protocol, int messageId,
                                                 const QString &variant) const
{
    const auto it = m_messages.constFind(messageKey(protocol, messageId));
    if (it == m_messages.constEnd())
        return nullptr;

    const PsMessageDescriptor *fallback = nullptr;
    for (const PsMessageDescriptor &descriptor : it.value()) {
        if (!fallback)
            fallback = &descriptor;
        if (variant.isEmpty() || descriptor.variant == variant)
            return &descriptor;
    }
    return fallback;
}

const PsMessageDescriptor *ProtocolCatalog::findByName(const QString &protocol, const QString &name,
                                                       const QString &variant) const
{
    const auto it = m_byName.constFind(nameKey(protocol, name));
    if (it == m_byName.constEnd())
        return nullptr;

    const PsMessageDescriptor *fallback = nullptr;
    for (const PsMessageDescriptor &descriptor : it.value()) {
        if (!fallback)
            fallback = &descriptor;
        if (variant.isEmpty() || descriptor.variant == variant)
            return &descriptor;
    }
    return fallback;
}
