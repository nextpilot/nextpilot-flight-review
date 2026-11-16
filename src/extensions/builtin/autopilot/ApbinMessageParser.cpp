#include "ApbinMessageParser.h"

#include <QVariant>
#include <QStringList>

#include <cstring>

namespace {

constexpr quint8 kHead1 = 0xA3;
constexpr quint8 kHead2 = 0x95;
constexpr quint8 kFmtId = 128;
constexpr int kFmtLen = 89;

QString trimText(const QByteArray &data, int offset, int length)
{
    if (offset < 0 || offset >= data.size())
        return {};
    const int end = qMin(data.size(), offset + length);
    int n = 0;
    while (offset + n < end && data.at(offset + n) != '\0')
        ++n;
    return QString::fromUtf8(data.constData() + offset, n).trimmed();
}

int codeSize(char code)
{
    switch (code)
    {
    case 'h': case 'H': case 'c': case 'C': return 2;
    case 'i': case 'I': case 'f': case 'e': case 'E': case 'L': case 'M': return 4;
    case 'd': case 'q': case 'Q': return 8;
    case 'n': return 4;
    case 'N': return 16;
    case 'Z': case 'a': return 64;
    default: return 0;
    }
}

bool isTextCode(char code)
{
    return code == 'n' || code == 'N' || code == 'Z' || code == 'a';
}

bool readNumber(const QByteArray &data, int offset, char code, QVariant &value)
{
    const int size = codeSize(code);
    if (size <= 0 || offset < 0 || offset + size > data.size())
        return false;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(data.constData() + offset);
    switch (code)
    {
    case 'b': value = static_cast<int>(static_cast<qint8>(p[0])); return true;
    case 'B': case 'M': value = static_cast<int>(p[0]); return true;
    case 'h': case 'c':
        value = static_cast<int>(static_cast<qint16>(p[0] | (p[1] << 8)));
        return true;
    case 'H': case 'C':
        value = static_cast<int>(p[0] | (p[1] << 8));
        return true;
    case 'i': case 'e': case 'L':
        value = static_cast<int>(static_cast<qint32>(static_cast<quint32>(p[0]) |
                                                     (quint32(p[1]) << 8) | (quint32(p[2]) << 16) |
                                                     (quint32(p[3]) << 24)));
        return true;
    case 'I': case 'E':
        value = static_cast<quint32>(p[0]) | (quint32(p[1]) << 8) |
                (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
        return true;
    case 'f':
    {
        float v = 0;
        std::memcpy(&v, p, 4);
        value = v;
        return true;
    }
    case 'd':
    {
        double v = 0;
        std::memcpy(&v, p, 8);
        value = v;
        return true;
    }
    case 'q':
    {
        qint64 v = 0;
        std::memcpy(&v, p, 8);
        value = v;
        return true;
    }
    case 'Q':
    {
        quint64 v = 0;
        std::memcpy(&v, p, 8);
        value = v;
        return true;
    }
    default:
        return false;
    }
}

bool findPacketStart(const QByteArray &data, int &start, quint8 &id)
{
    start = -1;
    for (int i = 0; i + 3 <= data.size(); ++i)
    {
        if (static_cast<quint8>(data.at(i)) != kHead1 ||
            static_cast<quint8>(data.at(i + 1)) != kHead2)
            continue;
        const quint8 candidate = static_cast<quint8>(data.at(i + 2));
        if (candidate == kFmtId)
        {
            if (i + kFmtLen <= data.size())
            {
                start = i;
                id = candidate;
                return true;
            }
            continue;
        }
        // 未知 id 只能在后续 FMT 已学习后判断长度；首帧先接受 magic。
        start = i;
        id = candidate;
        return true;
    }
    return false;
}

} // namespace

bool ApbinMessageParser::sniff(const QByteArray &data) const
{
    int start = -1;
    quint8 id = 0;
    return findPacketStart(data, start, id);
}

void ApbinMessageParser::parseFormat(const QByteArray &packet) const
{
    if (packet.size() < kFmtLen)
        return;
    Format format;
    format.id = static_cast<quint8>(packet.at(3));
    format.length = static_cast<quint8>(packet.at(4));
    format.name = trimText(packet, 5, 4);
    if (format.name.isEmpty() || format.length < 3)
        return;

    const QString codes = trimText(packet, 9, 16);
    const QString labels = trimText(packet, 25, 64);
    const QStringList labelList = labels.split(QLatin1Char(','), Qt::KeepEmptyParts);
    int offset = 3;
    for (int i = 0; i < codes.size(); ++i)
    {
        const char code = static_cast<char>(codes.at(i).toLatin1());
        const int size = codeSize(code);
        if (size <= 0)
            return;
        Field field;
        field.code = code;
        field.offset = offset;
        field.size = size;
        field.text = isTextCode(code);
        field.name = i < labelList.size() ? labelList.at(i).trimmed() : QString();
        if (field.name.isEmpty())
            field.name = QStringLiteral("field%1").arg(i);
        format.fields.append(field);
        offset += size;
    }
    m_formats.insert(format.id, format);
}

bool ApbinMessageParser::parse(const QByteArray &data, PsDecodedFrame &out) const
{
    int start = -1;
    quint8 id = 0;
    if (!findPacketStart(data, start, id))
        return false;

    if (id == kFmtId)
    {
        if (start + kFmtLen > data.size())
            return false;
        out = PsDecodedFrame{};
        out.protocol = protocolId();
        out.frameName = QStringLiteral("FMT");
        out.frameKey = QString::number(kFmtId);
        out.rawBytes = data.mid(start, kFmtLen);
        out.checksumOk = true;
        parseFormat(out.rawBytes);
        out.fields.append(PsDecodedField{QStringLiteral("fmt_id"), kFmtId, kFmtId, {}, {}, {}, {}, -1, 1});
        out.fields.append(PsDecodedField{QStringLiteral("length"), kFmtLen, kFmtLen, {}, {}, {}, {}, -1, 1});
        out.fields.append(PsDecodedField{QStringLiteral("name"), trimText(out.rawBytes, 5, 4),
                                         trimText(out.rawBytes, 5, 4), {}, {}, {}, {}, -1, 4});
        out.fields.append(PsDecodedField{QStringLiteral("field_codes"), trimText(out.rawBytes, 9, 16),
                                         trimText(out.rawBytes, 9, 16), {}, {}, {}, {}, -1, 16});
        out.fields.append(PsDecodedField{QStringLiteral("field_labels"), trimText(out.rawBytes, 25, 64),
                                         trimText(out.rawBytes, 25, 64), {}, {}, {}, {}, -1, 64});
        return true;
    }

    auto it = m_formats.constFind(id);
    if (it == m_formats.constEnd())
    {
        // 未学习到 FMT 前仍把原始包交给检查面板，避免直接丢帧。
        out = PsDecodedFrame{};
        out.protocol = protocolId();
        out.frameName = QStringLiteral("MSG_%1").arg(id);
        out.frameKey = QString::number(id);
        out.rawBytes = data.mid(start, qMin(data.size() - start, 3));
        out.checksumOk = true;
        out.unknown = true;
        out.fields.append(PsDecodedField{QStringLiteral("id"), id, id, {}, {}, {}, {}, -1, 1});
        PsDecodedField raw;
        raw.name = QStringLiteral("raw");
        raw.raw = out.rawBytes;
        raw.value = out.rawBytes;
        raw.offset = start;
        out.fields.append(raw);
        return true;
    }

    const Format &format = it.value();
    if (format.length < 3 || start + format.length > data.size())
        return false;

    out = PsDecodedFrame{};
    out.protocol = protocolId();
    out.frameName = format.name;
    out.frameKey = QString::number(id);
    out.rawBytes = data.mid(start, format.length);
    out.checksumOk = true;
    out.fields.append(PsDecodedField{QStringLiteral("id"), id, id, {}, {}, {}, {}, -1, 1});
    out.fields.append(PsDecodedField{QStringLiteral("length"), format.length, format.length,
                                     {}, {}, {}, {}, -1, 1});

    for (const Field &field : format.fields)
    {
        if (field.text)
        {
            PsDecodedField decoded;
            decoded.name = field.name;
            decoded.raw = out.rawBytes.mid(field.offset, field.size);
            decoded.value = trimText(out.rawBytes, field.offset, field.size);
            decoded.offset = field.offset;
            decoded.size = field.size;
            out.fields.append(decoded);
            continue;
        }
        QVariant value;
        if (!readNumber(out.rawBytes, field.offset, field.code, value))
            continue;
        PsDecodedField decoded;
        decoded.name = field.name;
        decoded.raw = out.rawBytes.mid(field.offset, field.size);
        decoded.value = value;
        decoded.offset = field.offset;
        decoded.size = field.size;
        if (field.name == QLatin1String("TimeUS"))
        {
            decoded.value = value.toDouble() / 1e6;
            decoded.unit = QStringLiteral("s");
            out.timestamp = decoded.value.toDouble();
        }
        out.fields.append(decoded);
    }
    return true;
}
