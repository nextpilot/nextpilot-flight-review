#include "UbxMessageParser.h"

#include <QByteArray>
#include <QString>
#include <QVariant>

#include <cassert>

namespace
{

QByteArray makeFrame(unsigned char classId, unsigned char messageId, const QByteArray &payload)
{
    QByteArray frame;
    frame.append(static_cast<char>(0xB5));
    frame.append(static_cast<char>(0x62));
    frame.append(static_cast<char>(classId));
    frame.append(static_cast<char>(messageId));
    frame.append(static_cast<char>(payload.size() & 0xFF));
    frame.append(static_cast<char>((payload.size() >> 8) & 0xFF));
    frame.append(payload);

    unsigned char ckA = 0;
    unsigned char ckB = 0;
    for (int i = 2; i < frame.size(); ++i) {
        ckA = static_cast<unsigned char>(ckA + static_cast<unsigned char>(frame.at(i)));
        ckB = static_cast<unsigned char>(ckB + ckA);
    }
    frame.append(static_cast<char>(ckA));
    frame.append(static_cast<char>(ckB));
    return frame;
}

void testFrame()
{
    UbxMessageParser parser;
    assert(parser.protocolId() == QStringLiteral("ubx"));
    assert(!parser.sniff(QByteArray::fromHex("B5")));
    assert(parser.sniff(QByteArray::fromHex("B5620107")));

    const QByteArray frame = makeFrame(0x01, 0x07, QByteArray::fromHex("01020304"));
    PsDecodedFrame decoded;
    assert(parser.parse(frame, decoded));
    assert(decoded.protocol == QStringLiteral("ubx"));
    assert(decoded.frameKey == QStringLiteral("01:07"));
    assert(decoded.checksumOk);
    assert(decoded.rawBytes == frame);
    assert(decoded.fields.size() == 5);
    assert(decoded.fields.at(0).value.toInt() == 1);
    assert(decoded.fields.at(1).value.toInt() == 7);
    assert(decoded.fields.at(2).value.toInt() == 4);
    assert(decoded.fields.at(3).value.toByteArray() == QByteArray::fromHex("01020304"));

    PsDecodedFrame bad;
    assert(parser.parse(makeFrame(0x01, 0x07, QByteArray::fromHex("0000")), bad));
    assert(bad.checksumOk);

    QByteArray corrupted = frame;
    corrupted[corrupted.size() - 1] = static_cast<char>(0x00);
    PsDecodedFrame failed;
    assert(parser.parse(corrupted, failed));
    assert(!failed.checksumOk);

    QByteArray truncated = frame;
    truncated.chop(1);
    PsDecodedFrame invalid;
    assert(!parser.parse(truncated, invalid));
}

} // namespace

int main()
{
    testFrame();
    return 0;
}
