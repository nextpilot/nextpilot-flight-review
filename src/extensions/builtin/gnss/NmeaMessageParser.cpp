#include "NmeaMessageParser.h"

#include <QVariant>

namespace
{

QByteArray firstLine(const QByteArray &data)
{
    QByteArray line = data;
    const int nl = line.indexOf('\n');
    if (nl >= 0)
        line.truncate(nl);
    return line.trimmed();
}

int checksum(const QByteArray &payload)
{
    int value = 0;
    for (const char c : payload)
        value ^= static_cast<unsigned char>(c);
    return value;
}

} // namespace

QString NmeaMessageParser::protocolId() const
{
    return QStringLiteral("nmea");
}

bool NmeaMessageParser::sniff(const QByteArray &data) const
{
    return firstLine(data).startsWith('$');
}

bool NmeaMessageParser::parse(const QByteArray &data, PsDecodedFrame &out) const
{
    const QByteArray line = firstLine(data);
    if (!line.startsWith('$'))
        return false;

    const int star = line.indexOf('*');
    const QByteArray payload = star >= 0 ? line.mid(1, star - 1) : line.mid(1);
    const QByteArray checksumText = star >= 0 ? line.mid(star + 1).left(2) : QByteArray();

    bool ok = false;
    const int expected = checksumText.toInt(&ok, 16);
    const int actual = checksum(payload);

    out.protocol = protocolId();
    out.frameKey = payload.left(payload.indexOf(',')).trimmed();
    out.frameName = out.frameKey;
    out.rawBytes = line;
    out.checksumOk = ok && expected == actual;
    out.unknown = false;
    out.fields.clear();

    const QList<QByteArray> parts = payload.split(',');
    if (parts.isEmpty())
        return false;

    const QByteArray head = parts.first().trimmed();
    const bool proprietary = head.startsWith('P');
    PsDecodedField talker;
    talker.name = QStringLiteral("talker");
    talker.value = proprietary ? QString::fromLatin1(head.left(1))
                               : QString::fromLatin1(head.left(2));
    out.fields.append(talker);

    PsDecodedField sentence;
    sentence.name = QStringLiteral("sentence");
    sentence.value = proprietary ? QString::fromLatin1(head.mid(1))
                                 : QString::fromLatin1(head.mid(2));
    out.fields.append(sentence);

    for (int i = 1; i < parts.size(); ++i) {
        PsDecodedField field;
        field.name = QStringLiteral("field_%1").arg(i);
        field.raw = QString::fromLatin1(parts.at(i));
        field.value = field.raw;
        out.fields.append(field);
    }

    PsDecodedField checksumField;
    checksumField.name = QStringLiteral("checksum");
    checksumField.raw = QString::fromLatin1(checksumText);
    checksumField.value = out.checksumOk;
    out.fields.append(checksumField);
    return true;
}
