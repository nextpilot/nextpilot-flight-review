#include "MavlinkMessageParser.h"

#include "message.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <cstring>

namespace
{

struct MavlinkFieldSpec
{
    QString name;
    QString type;
    int arrayLength = 0;
    int wireOffset = 0;
    QString units;
    QString enumName;
};

struct MavlinkMessageSpec
{
    int id = -1;
    QString name;
    int crcExtra = 0;
    int wireLength = 0;
    QVector<MavlinkFieldSpec> fields;
};

QStringList metadataCandidates()
{
    QStringList paths;
#ifdef PS_MAVLINK_METADATA_PATH
    paths << QStringLiteral(PS_MAVLINK_METADATA_PATH);
#endif
    if (QCoreApplication::instance())
        paths << QCoreApplication::applicationDirPath() + QStringLiteral("/mavlink_metadata.json");
    paths << QDir::currentPath() + QStringLiteral("/mavlink_metadata.json");
    return paths;
}

struct MavlinkEnumSpec
{
    bool bitmask = false;
    QHash<qint64, QString> values;
};

class MavlinkMetadata
{
public:
    MavlinkMetadata()
    {
        for (const QString &path : candidates()) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly))
                load(file.readAll());
            if (!m_messages.isEmpty())
                return;
        }
    }

    const MavlinkMessageSpec *message(int id) const
    {
        auto it = m_messages.constFind(id);
        return it == m_messages.constEnd() ? nullptr : &it.value();
    }

    QString enumLabel(const QString &enumName, qint64 value) const
    {
        auto it = m_enums.constFind(enumName);
        if (it == m_enums.constEnd())
            return QString();
        return it->values.value(value);
    }

private:
    static QStringList candidates() { return metadataCandidates(); }

    void load(const QByteArray &json)
    {
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
            return;

        const QJsonArray messages = doc.object().value(QStringLiteral("messages")).toArray();
        for (const QJsonValue &value : messages) {
            const QJsonObject object = value.toObject();
            MavlinkMessageSpec spec;
            spec.id = object.value(QStringLiteral("id")).toInt(-1);
            spec.name = object.value(QStringLiteral("name")).toString();
            spec.crcExtra = object.value(QStringLiteral("crcExtra")).toInt();
            spec.wireLength = object.value(QStringLiteral("wireLength")).toInt();

            const QJsonArray fields = object.value(QStringLiteral("fields")).toArray();
            for (const QJsonValue &fieldValue : fields) {
                const QJsonObject fieldObject = fieldValue.toObject();
                MavlinkFieldSpec field;
                field.name = fieldObject.value(QStringLiteral("name")).toString();
                field.type = fieldObject.value(QStringLiteral("type")).toString();
                field.arrayLength = fieldObject.value(QStringLiteral("arrayLength")).toInt();
                field.wireOffset = fieldObject.value(QStringLiteral("wireOffset")).toInt(-1);
                field.units = fieldObject.value(QStringLiteral("units")).toString();
                field.enumName = fieldObject.value(QStringLiteral("enum")).toString();
                spec.fields.append(field);
            }
            if (spec.id >= 0)
                m_messages.insert(spec.id, spec);
        }

        const QJsonArray enums = doc.object().value(QStringLiteral("enums")).toArray();
        for (const QJsonValue &value : enums) {
            const QJsonObject object = value.toObject();
            MavlinkEnumSpec spec;
            spec.bitmask = object.value(QStringLiteral("bitmask")).toBool();
            const QJsonArray entries = object.value(QStringLiteral("entries")).toArray();
            for (const QJsonValue &entryValue : entries) {
                const QJsonObject entry = entryValue.toObject();
                spec.values.insert(entry.value(QStringLiteral("value")).toVariant().toLongLong(),
                                   entry.value(QStringLiteral("name")).toString());
            }
            m_enums.insert(object.value(QStringLiteral("name")).toString(), spec);
        }
    }

    QHash<int, MavlinkMessageSpec> m_messages;
    QHash<QString, MavlinkEnumSpec> m_enums;
};

const MavlinkMetadata &metadata()
{
    static MavlinkMetadata instance;
    return instance;
}

int typeSize(const QString &type)
{
    if (type == QLatin1String("char") || type == QLatin1String("uint8_t") ||
        type == QLatin1String("int8_t"))
        return 1;
    if (type == QLatin1String("uint16_t") || type == QLatin1String("int16_t"))
        return 2;
    if (type == QLatin1String("uint32_t") || type == QLatin1String("int32_t") ||
        type == QLatin1String("float"))
        return 4;
    if (type == QLatin1String("uint64_t") || type == QLatin1String("int64_t") ||
        type == QLatin1String("double"))
        return 8;
    return 1;
}

struct UnitScale
{
    double scale = 1.0;
    QString unit;
};

UnitScale scaleFor(const QString &unit)
{
    if (unit == QLatin1String("degE7")) return {1e-7, QStringLiteral("deg")};
    if (unit == QLatin1String("degE5")) return {1e-5, QStringLiteral("deg")};
    if (unit == QLatin1String("cdeg")) return {1e-2, QStringLiteral("deg")};
    if (unit == QLatin1String("mm")) return {1e-3, QStringLiteral("m")};
    if (unit == QLatin1String("cm")) return {1e-2, QStringLiteral("m")};
    if (unit == QLatin1String("dm")) return {1e-1, QStringLiteral("m")};
    if (unit == QLatin1String("cm/s")) return {1e-2, QStringLiteral("m/s")};
    if (unit == QLatin1String("mm/s")) return {1e-3, QStringLiteral("m/s")};
    if (unit == QLatin1String("us")) return {1e-6, QStringLiteral("s")};
    if (unit == QLatin1String("ms")) return {1e-3, QStringLiteral("s")};
    if (unit == QLatin1String("cHz")) return {1e-2, QStringLiteral("Hz")};
    if (unit == QLatin1String("dHz")) return {1e-1, QStringLiteral("Hz")};
    if (unit == QLatin1String("mV")) return {1e-3, QStringLiteral("V")};
    if (unit == QLatin1String("cV")) return {1e-2, QStringLiteral("V")};
    if (unit == QLatin1String("mA")) return {1e-3, QStringLiteral("A")};
    if (unit == QLatin1String("cA")) return {1e-2, QStringLiteral("A")};
    return {1.0, unit};
}

template<typename T>
T readValue(const uint8_t *payload, int offset)
{
    T value{};
    std::memcpy(&value, payload + offset, sizeof(T));
    return value;
}

QVariant readScalar(const QString &type, const uint8_t *payload, int offset)
{
    if (type == QLatin1String("uint8_t"))
        return QVariant::fromValue<quint64>(readValue<uint8_t>(payload, offset));
    if (type == QLatin1String("int8_t"))
        return QVariant::fromValue<qint64>(readValue<int8_t>(payload, offset));
    if (type == QLatin1String("uint16_t"))
        return QVariant::fromValue<quint64>(readValue<uint16_t>(payload, offset));
    if (type == QLatin1String("int16_t"))
        return QVariant::fromValue<qint64>(readValue<int16_t>(payload, offset));
    if (type == QLatin1String("uint32_t"))
        return QVariant::fromValue<quint64>(readValue<uint32_t>(payload, offset));
    if (type == QLatin1String("int32_t"))
        return QVariant::fromValue<qint64>(readValue<int32_t>(payload, offset));
    if (type == QLatin1String("uint64_t"))
        return QVariant::fromValue<quint64>(readValue<uint64_t>(payload, offset));
    if (type == QLatin1String("int64_t"))
        return QVariant::fromValue<qint64>(readValue<int64_t>(payload, offset));
    if (type == QLatin1String("float"))
        return QVariant::fromValue<double>(readValue<float>(payload, offset));
    if (type == QLatin1String("double"))
        return QVariant::fromValue<double>(readValue<double>(payload, offset));
    return {};
}

QVariant scaledValue(const QVariant &raw, const UnitScale &scale)
{
    if (scale.scale == 1.0)
        return raw;
    bool ok = false;
    const double number = raw.toDouble(&ok);
    return ok ? QVariant::fromValue(number * scale.scale) : raw;
}

void appendHeaderFields(PsDecodedFrame &out, const mavlink::mavlink_message_t &msg)
{
    auto add = [&out](const QString &name, QVariant value) {
        PsDecodedField field;
        field.name = name;
        field.raw = value;
        field.value = value;
        field.size = 1;
        out.fields.append(field);
    };
    add(QStringLiteral("magic"), QVariant::fromValue<quint64>(msg.magic));
    add(QStringLiteral("sysid"), QVariant::fromValue<quint64>(msg.sysid));
    add(QStringLiteral("compid"), QVariant::fromValue<quint64>(msg.compid));
    add(QStringLiteral("seq"), QVariant::fromValue<quint64>(msg.seq));
    add(QStringLiteral("msgid"), QVariant::fromValue<quint64>(msg.msgid));
    add(QStringLiteral("len"), QVariant::fromValue<quint64>(msg.len));
}

void appendPayloadFields(PsDecodedFrame &out, const mavlink::mavlink_message_t &msg,
                         const MavlinkMessageSpec &spec, int payloadStart)
{
    const uint8_t *payload = reinterpret_cast<const uint8_t *>(_MAV_PAYLOAD(&msg));
    for (const MavlinkFieldSpec &field : spec.fields) {
        if (field.wireOffset < 0)
            continue;

        if (field.type == QLatin1String("char") && field.arrayLength > 0) {
            QString text;
            for (int i = 0; i < field.arrayLength; ++i) {
                const char c = static_cast<char>(payload[field.wireOffset + i]);
                if (c == '\0')
                    break;
                text.append(c);
            }
            PsDecodedField decoded;
            decoded.name = field.name;
            decoded.raw = QByteArray(reinterpret_cast<const char *>(payload + field.wireOffset),
                                     field.arrayLength);
            decoded.value = text;
            decoded.offset = payloadStart + field.wireOffset;
            decoded.size = field.arrayLength;
            out.fields.append(decoded);
            continue;
        }

        const UnitScale scale = scaleFor(field.units);
        const int count = qMax(1, field.arrayLength);
        for (int index = 0; index < count; ++index) {
            const int offset = field.wireOffset + index * typeSize(field.type);
            PsDecodedField decoded;
            decoded.name = field.arrayLength > 0
                               ? QStringLiteral("%1[%2]").arg(field.name).arg(index)
                               : field.name;
            decoded.raw = readScalar(field.type, payload, offset);
            decoded.value = scaledValue(decoded.raw, scale);
            decoded.unit = scale.unit;
            decoded.enumName = field.enumName;
            decoded.offset = payloadStart + offset;
            decoded.size = typeSize(field.type);
            if (!field.enumName.isEmpty()) {
                const qint64 value = decoded.raw.toLongLong();
                decoded.enumLabel = metadata().enumLabel(field.enumName, value);
            }
            out.fields.append(decoded);
        }
    }
}

int firstMagic(const QByteArray &data)
{
    int index = -1;
    for (int i = 0; i < data.size(); ++i) {
        const char c = data.at(i);
        if (c == static_cast<char>(0xFD) || c == static_cast<char>(0xFE)) {
            index = i;
            break;
        }
    }
    return index;
}

} // namespace

QString MavlinkMessageParser::protocolId() const
{
    return QStringLiteral("mavlink");
}

bool MavlinkMessageParser::sniff(const QByteArray &data) const
{
    return firstMagic(data) >= 0;
}

bool MavlinkMessageParser::parse(const QByteArray &data, PsDecodedFrame &out) const
{
    const int start = firstMagic(data);
    if (start < 0)
        return false;

    mavlink::mavlink_message_t message{};
    mavlink::mavlink_status_t outputStatus{};
    int end = -1;
    for (int i = start; i < data.size(); ++i) {
        const uint8_t result = mavlink::mavlink_frame_char_buffer(
            &m_rx, &m_status, static_cast<uint8_t>(data.at(i)), &message, &outputStatus);
        if (result == mavlink::MAVLINK_FRAMING_OK) {
            end = i;
            break;
        }
        if (result == mavlink::MAVLINK_FRAMING_BAD_CRC)
            return false;
        if (i - start > MAVLINK_MAX_PACKET_LEN + 8)
            break;
    }
    if (end < 0)
        return false;

    out.protocol = protocolId();
    out.frameKey = QString::number(message.msgid);
    const MavlinkMessageSpec *spec = metadata().message(message.msgid);
    out.frameName = spec && !spec->name.isEmpty()
                        ? spec->name
                        : QStringLiteral("MSG_%1").arg(message.msgid);
    out.rawBytes = data.mid(start, end - start + 1);
    out.checksumOk = true;
    out.unknown = spec == nullptr;
    out.fields.clear();
    appendHeaderFields(out, message);

    if (spec && !spec->fields.isEmpty()) {
        const int payloadStart = message.magic == 0xFE ? 6 : 10;
        appendPayloadFields(out, message, *spec, payloadStart);
    } else if (message.len > 0) {
        out.unknown = true;
    }
    return true;
}

namespace mavlink
{

const mavlink_msg_entry_t *mavlink_get_msg_entry(uint32_t msgid)
{
    static QHash<uint32_t, mavlink_msg_entry_t> entries;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        // Built from the generated metadata sidecar so this translation unit
        // does not need to include every generated message class.
        const QJsonArray messages = [] {
            for (const QString &path : metadataCandidates()) {
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly))
                    continue;
                const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                if (doc.isObject())
                    return doc.object().value(QStringLiteral("messages")).toArray();
            }
            return QJsonArray();
        }();

        for (const QJsonValue &value : messages) {
            const QJsonObject object = value.toObject();
            mavlink_msg_entry_t entry{};
            entry.msgid = static_cast<uint32_t>(object.value(QStringLiteral("id")).toInt());
            entry.crc_extra = static_cast<uint8_t>(object.value(QStringLiteral("crcExtra")).toInt());
            const int length = object.value(QStringLiteral("wireLength")).toInt();
            entry.min_msg_len = static_cast<uint8_t>(length);
            entry.max_msg_len = static_cast<uint8_t>(qMin(length, 255));
            entries.insert(entry.msgid, entry);
        }
    }

    auto it = entries.constFind(msgid);
    return it == entries.constEnd() ? nullptr : &it.value();
}

} // namespace mavlink
