#include "UbxMessageParser.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QVariant>

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

int typeSize(const QString &type)
{
    if (type == QLatin1String("u1") || type == QLatin1String("i1"))
        return 1;
    if (type == QLatin1String("u2") || type == QLatin1String("i2"))
        return 2;
    if (type == QLatin1String("u4") || type == QLatin1String("i4") ||
        type == QLatin1String("r4"))
        return 4;
    if (type == QLatin1String("r8"))
        return 8;
    return 0;
}

bool readNumber(const QByteArray &payload, int offset, const QString &type, double &value)
{
    const int size = type == QLatin1String("u1") || type == QLatin1String("i1")
                         ? 1
                         : (type == QLatin1String("u2") || type == QLatin1String("i2")
                                ? 2
                                : (type == QLatin1String("u4") || type == QLatin1String("i4") ||
                                           type == QLatin1String("r4")
                                       ? 4
                                       : (type == QLatin1String("r8") ? 8 : 0)));
    if (size == 0 || offset < 0 || offset + size > payload.size())
        return false;

    const unsigned char *p = reinterpret_cast<const unsigned char *>(payload.constData()) + offset;
    if (type == QLatin1String("u1")) {
        value = *p;
    } else if (type == QLatin1String("i1")) {
        value = static_cast<qint8>(*p);
    } else if (type == QLatin1String("u2")) {
        value = static_cast<quint16>(p[0] | (p[1] << 8));
    } else if (type == QLatin1String("i2")) {
        value = static_cast<qint16>(p[0] | (p[1] << 8));
    } else if (type == QLatin1String("u4")) {
        value = static_cast<quint32>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
    } else if (type == QLatin1String("i4")) {
        value = static_cast<qint32>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
    } else if (type == QLatin1String("r4")) {
        const quint32 bits = static_cast<quint32>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
        float number = 0.0f;
        std::memcpy(&number, &bits, sizeof(number));
        value = number;
    } else if (type == QLatin1String("r8")) {
        quint64 bits = 0;
        for (int i = 0; i < 8; ++i)
            bits |= static_cast<quint64>(p[i]) << (8 * i);
        double number = 0.0;
        std::memcpy(&number, &bits, sizeof(number));
        value = number;
    }
    return true;
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
        QVector<FieldDef> fields;
        for (const QJsonValue &fieldValue : object.value(QStringLiteral("fields")).toArray()) {
            const QJsonObject fieldObject = fieldValue.toObject();
            FieldDef field;
            field.name = fieldObject.value(QStringLiteral("name")).toString();
            field.type = fieldObject.value(QStringLiteral("type")).toString();
            field.unit = fieldObject.value(QStringLiteral("unit")).toString();
            field.description = fieldObject.value(QStringLiteral("description")).toString();
            field.offset = fieldObject.value(QStringLiteral("offset")).toInt(-1);
            field.scale = fieldObject.value(QStringLiteral("scale")).toDouble(1.0);
            if (!field.name.isEmpty() && field.offset >= 0)
                fields.append(field);
        }
        if (!fields.isEmpty()) {
            m_messages.insert(keyFor(classId, messageId), fields);
            m_names.insert(keyFor(classId, messageId),
                           object.value(QStringLiteral("name")).toString());
        }
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
        m_names = parser.m_names;
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
    field.value = QStringLiteral("%1 %2").arg(hexByte(ckA), hexByte(ckB));
    field.raw = QStringLiteral("%1 %2").arg(hexByte(expectedA), hexByte(expectedB));
    field.offset = frameLength - 2;
    field.size = 2;
    out.fields.append(field);

    const auto schema = m_messages.constFind(out.frameKey);
    if (schema == m_messages.constEnd()) {
        out.unknown = true;
        return true;
    }
    out.frameName = m_names.value(out.frameKey, out.frameKey);

    const QByteArray payload = frame.mid(6, payloadLength);
    for (const FieldDef &definition : schema.value()) {
        double number = 0.0;
        if (!readNumber(payload, definition.offset, definition.type, number))
            continue;
        number *= definition.scale;
        PsDecodedField decoded;
        decoded.name = definition.name;
        decoded.raw = payload.mid(definition.offset, typeSize(definition.type));
        decoded.value = number;
        decoded.unit = definition.unit;
        decoded.description = definition.description;
        decoded.offset = 6 + definition.offset;
        decoded.size = typeSize(definition.type);
        out.fields.append(decoded);
        if ((definition.name == QLatin1String("i_tow") ||
             definition.name == QLatin1String("iTOW")) &&
            out.timestamp == 0.0)
            out.timestamp = number;
    }
    return true;
}
