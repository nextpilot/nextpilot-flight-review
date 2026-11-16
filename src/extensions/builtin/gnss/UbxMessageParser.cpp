#include "UbxMessageParser.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <cstring>

namespace
{

QString hexByte(int value)
{
    return QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
}

QString keyFor(int classId, int messageId)
{
    return QStringLiteral("%1:%2").arg(hexByte(classId), hexByte(messageId));
}

int typeSize(const QString &type, int declared)
{
    if (declared > 0)
        return declared;
    if (type == QLatin1String("r4"))
        return 4;
    if (type == QLatin1String("r8"))
        return 8;
    if (type.startsWith(QLatin1Char('u')) || type.startsWith(QLatin1Char('i'))) {
        bool ok = false;
        const int size = type.mid(1).toInt(&ok);
        return ok ? size : 0;
    }
    return 0;
}

bool readUInt(const QByteArray &data, int offset, int size, quint64 &value)
{
    if (size < 1 || size > 8 || offset < 0 || offset + size > data.size())
        return false;
    value = 0;
    for (int i = 0; i < size; ++i)
        value |= static_cast<quint64>(static_cast<unsigned char>(data.at(offset + i))) << (8 * i);
    return true;
}

bool readValue(const QByteArray &data, int offset, const UbxMessageParser::FieldDef &field,
               QVariant &value)
{
    const QString &type = field.type;
    const int size = typeSize(type, field.size);
    if (type == QLatin1String("str")) {
        const int length = size > 0 ? size : data.size() - offset;
        if (offset < 0 || offset > data.size() || length <= 0)
            return false;
        value = QString::fromUtf8(data.mid(offset, qMin(length, data.size() - offset)));
        return true;
    }
    if (type == QLatin1String("bytes")) {
        const int length = size > 0 ? size : data.size() - offset;
        if (offset < 0 || offset > data.size() || length <= 0)
            return false;
        value = data.mid(offset, qMin(length, data.size() - offset));
        return true;
    }
    if (type == QLatin1String("r4")) {
        if (offset < 0 || offset + 4 > data.size())
            return false;
        const quint32 bits = static_cast<quint32>(data.at(offset) & 0xFF) |
                             (static_cast<quint32>(data.at(offset + 1) & 0xFF) << 8) |
                             (static_cast<quint32>(data.at(offset + 2) & 0xFF) << 16) |
                             (static_cast<quint32>(data.at(offset + 3) & 0xFF) << 24);
        float number = 0.0f;
        std::memcpy(&number, &bits, sizeof(number));
        value = number;
        return true;
    }
    if (type == QLatin1String("r8")) {
        if (offset < 0 || offset + 8 > data.size())
            return false;
        quint64 bits = 0;
        for (int i = 0; i < 8; ++i)
            bits |= static_cast<quint64>(static_cast<unsigned char>(data.at(offset + i))) << (8 * i);
        double number = 0.0;
        std::memcpy(&number, &bits, sizeof(number));
        value = number;
        return true;
    }

    if (size < 1 || size > 8 || offset < 0 || offset + size > data.size())
        return false;
    quint64 raw = 0;
    if (!readUInt(data, offset, size, raw))
        return false;

    const bool signedType = type.startsWith(QLatin1Char('i'));
    if (signedType) {
        qint64 signedValue = static_cast<qint64>(raw);
        if (size < 8) {
            const quint64 signBit = quint64(1) << (size * 8 - 1);
            if (raw & signBit)
                signedValue = static_cast<qint64>(raw | ~((quint64(1) << (size * 8)) - 1));
        }
        value = (size <= 4) ? QVariant(static_cast<int>(signedValue))
                            : QVariant(static_cast<qint64>(signedValue));
    } else {
        value = (size <= 4) ? QVariant(static_cast<unsigned int>(raw))
                            : QVariant(static_cast<quint64>(raw));
    }
    return true;
}

QString enumLabel(const UbxMessageParser::FieldDef &field, const QVariant &value,
                  const QMap<QString, QVariant> &values, const QString &prefix)
{
    if (field.enumValues.isEmpty())
        return QString();
    const QString key = QString::number(value.toLongLong());
    const QVariant entry = field.enumValues.value(key);
    if (entry.canConvert<QVariantMap>()) {
        const QString byKey = prefix + field.enumBy;
        const QVariant byValue = values.value(byKey);
        const QVariantMap nested = entry.toMap();
        return nested.value(QString::number(byValue.toLongLong())).toString();
    }
    return entry.toString();
}

void decodeFields(const QVector<UbxMessageParser::FieldDef> &definitions, const QByteArray &payload,
                  int base, const QString &prefix, const QString &timeField, PsDecodedFrame &out,
                  QMap<QString, QVariant> &values)
{
    for (const UbxMessageParser::FieldDef &definition : definitions) {
        const int offset = base + definition.offset;
        if (definition.type == QLatin1String("group")) {
            int count = definition.count;
            if (count < 0 && !definition.countField.isEmpty())
                count = values.value(prefix + definition.countField).toInt();
            if (count < 0 && definition.stride > 0)
                count = (payload.size() + base - offset) / definition.stride;
            count = qBound(0, count, 4096);
            for (int i = 0; i < count; ++i) {
                const QString childPrefix =
                    prefix + definition.name + QStringLiteral("_%1.").arg(i + 1, 2, 10, QLatin1Char('0'));
                decodeFields(definition.children, payload, offset + i * definition.stride, childPrefix,
                             timeField, out, values);
            }
            continue;
        }

        QVariant value;
        if (definition.type == QLatin1String("bit")) {
            quint64 raw = 0;
            if (!readUInt(payload, offset, definition.size, raw))
                continue;
            const int width = qBound(1, definition.bitWidth, 63);
            value = static_cast<quint64>((raw >> definition.bitOffset) & ((quint64(1) << width) - 1));
        } else if (!readValue(payload, offset, definition, value)) {
            continue;
        }

        if (definition.scale != 1.0)
            value = value.toDouble() * definition.scale;

        PsDecodedField decoded;
        decoded.name = prefix + definition.name;
        decoded.raw = payload.mid(offset, qMax(0, typeSize(definition.type, definition.size)));
        decoded.value = value;
        decoded.unit = definition.unit;
        decoded.description = definition.description;
        decoded.enumName = definition.enumValues.isEmpty() ? QString() : definition.name;
        decoded.enumLabel = enumLabel(definition, value, values, prefix);
        decoded.offset = offset + 6;
        decoded.size = typeSize(definition.type, definition.size);
        out.fields.append(decoded);

        values.insert(prefix + definition.name, value);
        if (definition.name == timeField && out.timestamp == 0.0)
            out.timestamp = value.toDouble();
    }
}

bool variantMatches(const UbxMessageParser::VariantRule &rule, const QByteArray &payload)
{
    if (!rule.present)
        return false;
    if (rule.length >= 0 && payload.size() != rule.length)
        return false;
    if (rule.lengthBase >= 0 && rule.lengthCountOffset >= 0 && rule.lengthStride > 0) {
        quint64 count = 0;
        if (!readUInt(payload, rule.lengthCountOffset, 1, count))
            return false;
        if (payload.size() != rule.lengthBase + static_cast<int>(count) * rule.lengthStride)
            return false;
    }
    if (rule.offset >= 0 && !rule.type.isEmpty()) {
        quint64 raw = 0;
        const int size = typeSize(rule.type, 0);
        if (!readUInt(payload, rule.offset, size, raw))
            return false;
        if (rule.equals.isValid() && raw != static_cast<quint64>(rule.equals.toULongLong()))
            return false;
        if (rule.notEquals.isValid() && raw == static_cast<quint64>(rule.notEquals.toULongLong()))
            return false;
    }
    return true;
}

const UbxMessageParser::MessageDef *selectMessage(
    const QVector<UbxMessageParser::MessageDef> &definitions, const QByteArray &payload)
{
    for (const UbxMessageParser::MessageDef &definition : definitions)
        if (definition.variant.present && variantMatches(definition.variant, payload))
            return &definition;
    for (const UbxMessageParser::MessageDef &definition : definitions)
        if (!definition.variant.present)
            return &definition;
    return definitions.isEmpty() ? nullptr : &definitions.first();
}

QString typeForObject(const QJsonObject &object)
{
    return object.value(QStringLiteral("type")).toString();
}

UbxMessageParser::FieldDef readField(const QJsonObject &object)
{
    UbxMessageParser::FieldDef field;
    field.name = object.value(QStringLiteral("name")).toString();
    field.type = typeForObject(object);
    field.unit = object.value(QStringLiteral("unit")).toString();
    field.description = object.value(QStringLiteral("description")).toString();
    field.countField = object.value(QStringLiteral("countField")).toString();
    field.offset = object.value(QStringLiteral("offset")).toInt(0);
    field.size = object.value(QStringLiteral("size")).toInt(0);
    field.count = object.value(QStringLiteral("count")).toInt(-1);
    field.stride = object.value(QStringLiteral("stride")).toInt(0);
    field.bitOffset = object.value(QStringLiteral("bitOffset")).toInt(-1);
    field.bitWidth = object.value(QStringLiteral("bitWidth")).toInt(0);
    field.scale = object.value(QStringLiteral("scale")).toDouble(1.0);

    const QJsonObject enumObject = object.value(QStringLiteral("enum")).toObject();
    field.enumBy = enumObject.value(QStringLiteral("by")).toString();
    for (const QString &key : enumObject.value(QStringLiteral("values")).toObject().keys()) {
        const QJsonValue value = enumObject.value(QStringLiteral("values")).toObject().value(key);
        if (value.isObject()) {
            QVariantMap nested;
            const QJsonObject nestedObject = value.toObject();
            for (const QString &nestedKey : nestedObject.keys())
                nested.insert(nestedKey, nestedObject.value(nestedKey).toString());
            field.enumValues.insert(key, nested);
        } else {
            field.enumValues.insert(key, value.toString());
        }
    }

    for (const QJsonValue &child : object.value(QStringLiteral("fields")).toArray())
        field.children.append(readField(child.toObject()));
    return field;
}

QVector<UbxMessageParser::FieldDef> readFields(const QJsonArray &array)
{
    QVector<UbxMessageParser::FieldDef> fields;
    for (const QJsonValue &value : array)
        fields.append(readField(value.toObject()));
    return fields;
}

UbxMessageParser::VariantRule readVariant(const QJsonObject &object)
{
    UbxMessageParser::VariantRule rule;
    if (!object.contains(QStringLiteral("variant")))
        return rule;
    const QJsonObject variant = object.value(QStringLiteral("variant")).toObject();
    rule.present = true;
    rule.offset = variant.value(QStringLiteral("offset")).toInt(-1);
    rule.type = variant.value(QStringLiteral("type")).toString();
    rule.length = variant.value(QStringLiteral("length")).toInt(-1);
    rule.lengthBase = variant.value(QStringLiteral("length_base")).toInt(-1);
    rule.lengthCountOffset = variant.value(QStringLiteral("length_count_offset")).toInt(-1);
    rule.lengthStride = variant.value(QStringLiteral("length_stride")).toInt(-1);
    rule.equals = variant.contains(QStringLiteral("equals"))
                      ? variant.value(QStringLiteral("equals")).toVariant()
                      : QVariant();
    rule.notEquals = variant.contains(QStringLiteral("not_equals"))
                         ? variant.value(QStringLiteral("not_equals")).toVariant()
                         : QVariant();
    return rule;
}

} // namespace

UbxMessageParser::UbxMessageParser()
{
    loadDatabase();
}

UbxMessageParser::UbxMessageParser(const QString &databasePath)
{
    QFile file(databasePath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    for (const QJsonValue &value : document.object().value(QStringLiteral("messages")).toArray()) {
        const QJsonObject object = value.toObject();
        const int classId = object.value(QStringLiteral("class")).toInt(-1);
        const int messageId = object.value(QStringLiteral("id")).toInt(-1);
        if (classId < 0 || messageId < 0)
            continue;
        MessageDef definition;
        definition.name = object.value(QStringLiteral("name")).toString();
        definition.timeField = object.value(QStringLiteral("timeField")).toString();
        definition.fields = readFields(object.value(QStringLiteral("fields")).toArray());
        definition.variant = readVariant(object);
        if (!definition.fields.isEmpty())
            m_messages[keyFor(classId, messageId)].append(definition);
    }
}

void UbxMessageParser::loadDatabase()
{
    if (!m_messages.isEmpty())
        return;
    const QString appDir = QCoreApplication::instance() ? QCoreApplication::applicationDirPath()
                                                        : QString();
    const QStringList candidates = {
#ifdef PS_GNSS_DB_DIR
        QStringLiteral(PS_GNSS_DB_DIR) + QStringLiteral("/ubx_database.json"),
#endif
        appDir + QStringLiteral("/ubx_database.json"),
        appDir + QStringLiteral("/../ubx_database.json"),
        QDir::currentPath() + QStringLiteral("/ubx_database.json"),
        QDir::currentPath() + QStringLiteral("/src/extensions/builtin/gnss/ubx_database.json")};
    for (const QString &path : candidates) {
        if (!QFile::exists(path))
            continue;
        UbxMessageParser parser(path);
        m_messages = parser.m_messages;
        if (!m_messages.isEmpty())
            return;
    }
}

bool UbxMessageParser::sniffData(const QByteArray &data)
{
    return data.size() >= 2 && static_cast<unsigned char>(data.at(0)) == 0xB5 &&
           static_cast<unsigned char>(data.at(1)) == 0x62;
}

QString UbxMessageParser::protocolId() const
{
    return QStringLiteral("ubx");
}

bool UbxMessageParser::sniff(const QByteArray &data) const
{
    return sniffData(data);
}

bool UbxMessageParser::parse(const QByteArray &data, PsDecodedFrame &out) const
{
    if (!sniff(data) || data.size() < 8)
        return false;

    const int payloadLength = static_cast<unsigned char>(data.at(4)) +
                              (static_cast<unsigned char>(data.at(5)) << 8);
    const int frameLength = 6 + payloadLength + 2;
    if (data.size() < frameLength)
        return false;

    QByteArray frame = data.left(frameLength);
    unsigned char ckA = 0;
    unsigned char ckB = 0;
    for (int i = 2; i < 6 + payloadLength; ++i) {
        ckA = static_cast<unsigned char>(ckA + static_cast<unsigned char>(frame.at(i)));
        ckB = static_cast<unsigned char>(ckB + ckA);
    }
    const unsigned char expectedA = static_cast<unsigned char>(frame.at(frameLength - 2));
    const unsigned char expectedB = static_cast<unsigned char>(frame.at(frameLength - 1));

    const int classId = static_cast<unsigned char>(frame.at(2));
    const int messageId = static_cast<unsigned char>(frame.at(3));

    out.protocol = protocolId();
    out.frameKey = keyFor(classId, messageId);
    out.frameName = out.frameKey;
    out.rawBytes = frame;
    out.checksumOk = ckA == expectedA && ckB == expectedB;
    out.unknown = false;
    out.fields.clear();
    out.timestamp = 0.0;

    PsDecodedField field;
    field.name = QStringLiteral("classId");
    field.value = classId;
    field.offset = 2;
    field.size = 1;
    out.fields.append(field);

    field.name = QStringLiteral("messageId");
    field.value = messageId;
    field.offset = 3;
    out.fields.append(field);

    field.name = QStringLiteral("length");
    field.value = payloadLength;
    field.offset = 4;
    field.size = 2;
    out.fields.append(field);

    field.name = QStringLiteral("payload");
    field.value = frame.mid(6, payloadLength);
    field.offset = 6;
    field.size = payloadLength;
    out.fields.append(field);

    field.name = QStringLiteral("checksum");
    field.value = QStringLiteral("%1 %2").arg(hexByte(ckA), hexByte(expectedB));
    field.raw = QStringLiteral("%1 %2").arg(hexByte(expectedA), hexByte(expectedB));
    field.offset = frameLength - 2;
    field.size = 2;
    out.fields.append(field);

    const auto schemas = m_messages.constFind(out.frameKey);
    if (schemas == m_messages.constEnd()) {
        out.unknown = true;
        return true;
    }

    const QByteArray payload = frame.mid(6, payloadLength);
    const MessageDef *definition = selectMessage(schemas.value(), payload);
    if (!definition) {
        out.unknown = true;
        return true;
    }
    out.frameName = definition->name;

    QMap<QString, QVariant> values;
    decodeFields(definition->fields, payload, 0, QString(), definition->timeField, out, values);
    return true;
}
