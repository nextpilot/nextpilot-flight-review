#include "UlogMessageParser.h"

#include <QVariant>
#include <QStringList>

#include <cstring>

namespace {

constexpr int kMagicSize = 7;
const QByteArray kMagic = QByteArray("ULog\x01\x12\x35", 7);

bool isRecordType(char type)
{
    return type == 'F' || type == 'A' || type == 'D' || type == 'P' || type == 'Q' ||
           type == 'R' || type == 'I' || type == 'M' || type == 'L' || type == 'S' ||
           type == 'C' || type == 'B' || type == 'W';
}

quint16 le16(const QByteArray &data, int offset)
{
    return static_cast<quint16>(static_cast<unsigned char>(data.at(offset))) |
           (static_cast<quint16>(static_cast<unsigned char>(data.at(offset + 1))) << 8);
}

quint64 le64(const QByteArray &data, int offset)
{
    quint64 value = 0;
    for (int i = 0; i < 8; ++i)
        value |= static_cast<quint64>(static_cast<unsigned char>(data.at(offset + i))) << (8 * i);
    return value;
}

int typeSize(const QString &base)
{
    if (base == QLatin1String("int8_t") || base == QLatin1String("uint8_t") ||
        base == QLatin1String("bool") || base == QLatin1String("char"))
        return 1;
    if (base == QLatin1String("int16_t") || base == QLatin1String("uint16_t"))
        return 2;
    if (base == QLatin1String("int32_t") || base == QLatin1String("uint32_t") ||
        base == QLatin1String("float"))
        return 4;
    if (base == QLatin1String("int64_t") || base == QLatin1String("uint64_t") ||
        base == QLatin1String("double"))
        return 8;
    return 0;
}

bool readPrim(const QByteArray &data, int offset, const QString &base, QVariant &value)
{
    if (offset < 0 || offset + typeSize(base) > data.size() || typeSize(base) == 0)
        return false;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(data.constData() + offset);
    if (base == QLatin1String("int8_t")) value = static_cast<int>(static_cast<qint8>(p[0]));
    else if (base == QLatin1String("uint8_t") || base == QLatin1String("bool"))
        value = static_cast<int>(p[0]);
    else if (base == QLatin1String("char")) value = static_cast<int>(p[0]);
    else if (base == QLatin1String("int16_t"))
        value = static_cast<int>(static_cast<qint16>(p[0] | (p[1] << 8)));
    else if (base == QLatin1String("uint16_t"))
        value = static_cast<int>(p[0] | (p[1] << 8));
    else if (base == QLatin1String("int32_t"))
        value = static_cast<int>(static_cast<qint32>(static_cast<quint32>(p[0]) |
                                                     (quint32(p[1]) << 8) | (quint32(p[2]) << 16) |
                                                     (quint32(p[3]) << 24)));
    else if (base == QLatin1String("uint32_t"))
        value = static_cast<quint32>(p[0]) | (quint32(p[1]) << 8) |
                (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
    else if (base == QLatin1String("int64_t"))
    {
        qint64 v = 0;
        std::memcpy(&v, p, 8);
        value = v;
    }
    else if (base == QLatin1String("uint64_t"))
    {
        quint64 v = 0;
        std::memcpy(&v, p, 8);
        value = v;
    }
    else if (base == QLatin1String("float"))
    {
        float v = 0;
        std::memcpy(&v, p, 4);
        value = v;
    }
    else if (base == QLatin1String("double"))
    {
        double v = 0;
        std::memcpy(&v, p, 8);
        value = v;
    }
    else
        return false;
    return true;
}

QString readText(const QByteArray &data, int offset, int length)
{
    if (offset < 0 || offset >= data.size())
        return {};
    const int end = qMin(data.size(), offset + length);
    int n = 0;
    while (offset + n < end && data.at(offset + n) != '\0')
        ++n;
    return QString::fromUtf8(data.constData() + offset, n);
}

QString fieldValueText(const QVariant &value)
{
    return value.toString();
}

} // namespace

bool UlogMessageParser::sniff(const QByteArray &data) const
{
    if (data.startsWith(kMagic))
        return true;
    if (data.size() < 3)
        return false;
    const quint16 size = le16(data, 0);
    return size > 0 && size + 3 <= data.size() && isRecordType(static_cast<char>(data.at(2)));
}

void UlogMessageParser::parseFormat(const QString &definition) const
{
    const int colon = definition.indexOf(QLatin1Char(':'));
    if (colon <= 0)
        return;
    Format format;
    format.name = definition.left(colon).trimmed();
    if (format.name.isEmpty())
        return;

    const QString rest = definition.mid(colon + 1);
    for (const QString &part : rest.split(QLatin1Char(';'), Qt::SkipEmptyParts))
    {
        const QStringList tokens = part.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (tokens.size() < 2)
            continue;
        QString type = tokens.at(0);
        QString name = tokens.at(1);
        int count = 1;
        const int typeBracket = type.indexOf(QLatin1Char('['));
        if (typeBracket >= 0)
        {
            const int close = type.indexOf(QLatin1Char(']'), typeBracket);
            if (close > typeBracket)
            {
                count = type.mid(typeBracket + 1, close - typeBracket - 1).toInt();
                type = type.left(typeBracket);
            }
        }
        const int nameBracket = name.indexOf(QLatin1Char('['));
        if (nameBracket >= 0)
        {
            const int close = name.indexOf(QLatin1Char(']'), nameBracket);
            if (close > nameBracket)
            {
                count = name.mid(nameBracket + 1, close - nameBracket - 1).toInt();
                name = name.left(nameBracket);
            }
        }
        if (count <= 0)
            count = 1;
        const int size = typeSize(type);
        if (size <= 0)
            continue;
        Field field;
        field.name = name;
        field.type = type;
        field.offset = format.payloadSize;
        field.size = size;
        field.count = count;
        field.text = type == QLatin1String("char");
        format.fields.append(field);
        format.payloadSize += size * count;
    }
    m_formatsByName.insert(format.name, format);
}

bool UlogMessageParser::appendData(const QByteArray &body, PsDecodedFrame &out) const
{
    if (body.size() < 2)
        return false;
    const quint32 msgId = le16(body, 0);
    out.frameKey = QStringLiteral("D:%1").arg(msgId);
    auto formatIt = m_formatByMsgId.constFind(msgId);
    if (formatIt == m_formatByMsgId.constEnd())
    {
        out.frameName = QStringLiteral("DATA");
        out.fields.append(PsDecodedField{QStringLiteral("msg_id"), msgId, msgId, {}, {}, {}, {},
                                         -1, 0});
        PsDecodedField payload;
        payload.name = QStringLiteral("payload");
        payload.raw = body.mid(2);
        payload.value = payload.raw;
        payload.offset = 5;
        payload.size = body.size() - 2;
        out.fields.append(payload);
        return true;
    }

    const QString formatName = formatIt.value();
    auto fit = m_formatsByName.constFind(formatName);
    if (fit == m_formatsByName.constEnd())
        return false;
    const Format &format = fit.value();
    out.frameName = format.name;

    const QByteArray payload = body.mid(2);
    for (const Field &field : format.fields)
    {
        if (field.text)
        {
            PsDecodedField decoded;
            decoded.name = field.name;
            decoded.raw = payload.mid(field.offset, field.size * field.count);
            decoded.value = readText(payload, field.offset, field.size * field.count);
            decoded.offset = 5 + field.offset;
            decoded.size = field.size * field.count;
            out.fields.append(decoded);
            continue;
        }
        for (int i = 0; i < field.count; ++i)
        {
            const int offset = field.offset + i * field.size;
            QVariant value;
            if (!readPrim(payload, offset, field.type, value))
                continue;
            PsDecodedField decoded;
            decoded.name = field.count > 1
                               ? QStringLiteral("%1[%2]").arg(field.name).arg(i)
                               : field.name;
            decoded.raw = payload.mid(offset, field.size);
            decoded.value = value;
            decoded.offset = 5 + offset;
            decoded.size = field.size;
            if (field.name == QLatin1String("timestamp") && field.type == QLatin1String("uint64_t"))
            {
                decoded.value = value.toDouble() / 1e6;
                decoded.unit = QStringLiteral("s");
                out.timestamp = decoded.value.toDouble();
            }
            out.fields.append(decoded);
        }
    }
    return true;
}

bool UlogMessageParser::parse(const QByteArray &data, PsDecodedFrame &out) const
{
    int start = -1;
    if (data.startsWith(kMagic) && data.size() >= 16)
        start = 16;
    else
    {
        int cursor = 0;
        while (cursor + 3 <= data.size())
        {
            const quint16 size = le16(data, cursor);
            if (size > 0 && cursor + 3 + size <= data.size() &&
                isRecordType(static_cast<char>(data.at(cursor + 2))))
            {
                start = cursor;
                break;
            }
            ++cursor;
        }
    }
    if (start < 0)
        return false;

    const quint16 size = le16(data, start);
    const char type = static_cast<char>(data.at(start + 2));
    const QByteArray body = data.mid(start + 3, size);
    if (body.size() != size)
        return false;

    out = PsDecodedFrame{};
    out.protocol = protocolId();
    out.frameKey = QString::fromLatin1(&type, 1);
    out.rawBytes = data.mid(start, 3 + size);
    out.checksumOk = true;
    out.fields.append(PsDecodedField{QStringLiteral("size"), size, size, {}, {}, {}, {}, -1, 3});
    out.fields.append(PsDecodedField{QStringLiteral("type"), QString::fromLatin1(&type, 1),
                                     QString::fromLatin1(&type, 1), {}, {}, {}, {}, -1, 1});

    if (type == 'F')
    {
        const QString definition = QString::fromUtf8(body);
        out.frameName = QStringLiteral("FORMAT");
        out.frameKey = QStringLiteral("F");
        parseFormat(definition);
        out.fields.append(PsDecodedField{QStringLiteral("definition"), definition, definition,
                                         {}, {}, {}, {}, -1, 0});
    }
    else if (type == 'A')
    {
        if (size < 3)
            return true;
        const quint8 multiId = static_cast<quint8>(body.at(0));
        const quint32 msgId = le16(body, 1);
        const QString name = QString::fromUtf8(body.mid(3).constData(), body.size() - 3);
        m_formatByMsgId.insert(msgId, name);
        out.frameName = QStringLiteral("ADD_LOGGED_MSG");
        out.frameKey = QStringLiteral("A:%1").arg(msgId);
        out.fields.append(PsDecodedField{QStringLiteral("multi_id"), multiId, multiId, {}, {}, {}, {}, -1, 1});
        out.fields.append(PsDecodedField{QStringLiteral("msg_id"), msgId, msgId, {}, {}, {}, {}, -1, 2});
        out.fields.append(PsDecodedField{QStringLiteral("format"), name, name, {}, {}, {}, {}, -1, 0});
    }
    else if (type == 'D')
    {
        if (!appendData(body, out))
            return false;
    }
    else if (type == 'P' || type == 'Q')
    {
        if (size >= 3)
        {
            const quint8 keyLen = static_cast<quint8>(body.at(2));
            if (3 + keyLen < body.size())
            {
                const QString key = QString::fromUtf8(body.constData() + 3, keyLen);
                const QChar valueType = QChar(body.at(3 + keyLen));
                const QByteArray raw = body.mid(4 + keyLen);
                out.frameName = type == 'P' ? QStringLiteral("PARAM") : QStringLiteral("PARAM_CHANGED");
                out.frameKey = QStringLiteral("P:%1").arg(key);
                out.fields.append(PsDecodedField{QStringLiteral("key"), key, key, {}, {}, {}, {}, -1, 0});
                out.fields.append(PsDecodedField{QStringLiteral("value_type"), valueType, valueType,
                                                 {}, {}, {}, {}, -1, 1});
                PsDecodedField valueField;
                valueField.name = QStringLiteral("value");
                valueField.raw = raw;
                valueField.value = raw;
                valueField.offset = 4 + keyLen;
                valueField.size = raw.size();
                out.fields.append(valueField);
            }
        }
    }
    else if (type == 'I')
    {
        if (size >= 3)
        {
            const quint8 keyLen = static_cast<quint8>(body.at(0));
            if (1 + keyLen <= body.size())
            {
                const QString key = QString::fromUtf8(body.constData() + 1, keyLen);
                const QString value = QString::fromUtf8(body.constData() + 1 + keyLen,
                                                        body.size() - 1 - keyLen);
                out.frameName = QStringLiteral("INFO");
                out.frameKey = QStringLiteral("I:%1").arg(key);
                out.fields.append(PsDecodedField{QStringLiteral("key"), key, key, {}, {}, {}, {}, -1, 0});
                out.fields.append(PsDecodedField{QStringLiteral("value"), value, value, {}, {}, {}, {}, -1, 0});
            }
        }
    }
    else
    {
        out.frameName = QStringLiteral("ULOG_%1").arg(QChar(type));
        PsDecodedField bodyField;
        bodyField.name = QStringLiteral("body");
        bodyField.raw = body;
        bodyField.value = body;
        bodyField.offset = 3;
        bodyField.size = body.size();
        out.fields.append(bodyField);
    }

    return true;
}
