#include "UbxMessageParser.h"

#include <QString>
#include <QVariant>

namespace
{

QString hexByte(int value)
{
    return QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
}

} // namespace

QString UbxMessageParser::protocolId() const
{
    return QStringLiteral("ubx");
}

bool UbxMessageParser::sniff(const QByteArray &data) const
{
    return data.size() >= 2 && static_cast<unsigned char>(data.at(0)) == 0xB5 &&
           static_cast<unsigned char>(data.at(1)) == 0x62;
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
    out.frameKey = QStringLiteral("%1:%2").arg(hexByte(classId), hexByte(messageId));
    out.frameName = out.frameKey;
    out.rawBytes = frame;
    out.checksumOk = ckA == expectedA && ckB == expectedB;
    out.unknown = false;
    out.fields.clear();

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
    return true;
}
