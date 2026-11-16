#include "UbxMessageParser.h"

#include <QByteArray>
#include <QString>
#include <QVariant>

#include <QtGlobal>

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
    assert(!decoded.unknown);
    assert(decoded.fields.size() == 6);
    assert(decoded.fields.at(0).value.toInt() == 1);
    assert(decoded.fields.at(1).value.toInt() == 7);
    assert(decoded.fields.at(2).value.toInt() == 4);
    assert(decoded.fields.at(3).value.toByteArray() == QByteArray::fromHex("01020304"));
    assert(decoded.fields.at(5).name == QStringLiteral("i_tow"));
    assert(decoded.fields.at(5).value.toDouble() > 0.0);

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

void putI4(QByteArray &payload, int offset, qint32 value)
{
    payload[offset] = static_cast<char>(value & 0xFF);
    payload[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
    payload[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
    payload[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
}

void testSchemaFields()
{
    QByteArray payload(92, 0);
    payload[26] = 3;  // fix_type
    payload[29] = 12; // num_sv
    putI4(payload, 30, 1163970000); // longitude
    putI4(payload, 34, 399000000);  // latitude
    putI4(payload, 42, 12345);      // altitude mm
    putI4(payload, 54, 100);        // vel_n cm/s
    putI4(payload, 70, 9000000);    // heading 1e-5 deg

    UbxMessageParser parser;
    PsDecodedFrame decoded;
    assert(parser.parse(makeFrame(0x01, 0x07, payload), decoded));
    assert(!decoded.unknown);

    bool found = false;
    for (const PsDecodedField &field : decoded.fields) {
        if (field.name == QStringLiteral("latitude")) {
            found = true;
            assert(qAbs(field.value.toDouble() - 39.9) < 1e-6);
            assert(field.unit == QStringLiteral("deg"));
        }
    }
    assert(found);

    found = false;
    for (const PsDecodedField &field : decoded.fields) {
        if (field.name == QStringLiteral("heading")) {
            found = true;
            assert(qAbs(field.value.toDouble() - 90.0) < 1e-6);
        }
    }
    assert(found);
}

} // namespace

int main()
{
    testFrame();
    testSchemaFields();
    return 0;
}
