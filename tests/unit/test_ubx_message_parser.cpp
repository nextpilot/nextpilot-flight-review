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
    assert(decoded.fields.size() >= 5);
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

void putI4(QByteArray &payload, int offset, qint32 value)
{
    payload[offset] = static_cast<char>(value & 0xFF);
    payload[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
    payload[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
    payload[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
}

void putU2(QByteArray &payload, int offset, quint16 value)
{
    payload[offset] = static_cast<char>(value & 0xFF);
    payload[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
}

void putI2(QByteArray &payload, int offset, qint16 value)
{
    payload[offset] = static_cast<char>(value & 0xFF);
    payload[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
}

void putU4(QByteArray &payload, int offset, quint32 value)
{
    payload[offset] = static_cast<char>(value & 0xFF);
    payload[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
    payload[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
    payload[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
}

const PsDecodedField *findField(const PsDecodedFrame &frame, const QString &name)
{
    for (const PsDecodedField &field : frame.fields)
        if (field.name == name)
            return &field;
    return nullptr;
}

void testSchemaFields()
{
    QByteArray payload(92, 0);
    payload[20] = 3; // fix_type
    payload[23] = 12; // num_sv
    putI4(payload, 24, 1163970000); // longitude
    putI4(payload, 28, 399000000);  // latitude
    putI4(payload, 36, 12345);      // hMSL
    putI4(payload, 48, 100);        // vel_n cm/s
    putI4(payload, 64, 9000000);    // head_mot 1e-5 deg

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
        if (field.name == QStringLiteral("head_mot")) {
            found = true;
            assert(qAbs(field.value.toDouble() - 90.0) < 1e-6);
        }
    }
    assert(found);
}

void testGroupedMessage()
{
    QByteArray payload(32, 0);
    putU4(payload, 0, 1000);
    payload[4] = 1; // version
    payload[5] = 2; // numSvs

    const auto putRecord = [&payload](int base, int gnssId, int svId, int cno, int elev, int azim,
                                      int prRes, quint32 flags) {
        payload[base] = static_cast<char>(gnssId);
        payload[base + 1] = static_cast<char>(svId);
        payload[base + 2] = static_cast<char>(cno);
        payload[base + 3] = static_cast<char>(elev);
        putI2(payload, base + 4, static_cast<qint16>(azim));
        putI2(payload, base + 6, static_cast<qint16>(prRes));
        putU4(payload, base + 8, flags);
    };
    putRecord(8, 0, 5, 45, 30, 120, 10, 12);
    putRecord(20, 2, 7, 40, -10, -45, -5, 0);

    UbxMessageParser parser;
    PsDecodedFrame decoded;
    assert(parser.parse(makeFrame(0x01, 0x35, payload), decoded));
    assert(decoded.frameName == QStringLiteral("nav_sat"));
    assert(!decoded.unknown);
    const PsDecodedField *field = findField(decoded, QStringLiteral("sv_01.gnss_id"));
    assert(field);
    assert(field->value.toInt() == 0);
    assert(field->enumLabel == QStringLiteral("GPS"));

    field = findField(decoded, QStringLiteral("sv_01.cno"));
    assert(field && field->value.toInt() == 45);
    field = findField(decoded, QStringLiteral("sv_01.azim"));
    assert(field && field->value.toInt() == 120);
    field = findField(decoded, QStringLiteral("sv_01.pr_res"));
    assert(field && qAbs(field->value.toDouble() - 1.0) < 1e-9);
    field = findField(decoded, QStringLiteral("sv_01.sv_used"));
    assert(field && field->value.toInt() == 1);

    field = findField(decoded, QStringLiteral("sv_02.gnss_id"));
    assert(field && field->value.toInt() == 2);
    field = findField(decoded, QStringLiteral("sv_02.pr_res"));
    assert(field && qAbs(field->value.toDouble() + 0.5) < 1e-9);
}

void testVariantMessage()
{
    QByteArray payload(68, 0);
    payload[0] = 1; // MGA-GPS-EPH type
    payload[1] = 0; // version
    payload[2] = 5; // svId

    UbxMessageParser parser;
    PsDecodedFrame decoded;
    assert(parser.parse(makeFrame(0x13, 0x00, payload), decoded));
    assert(decoded.frameName == QStringLiteral("mga_gps_eph"));
    assert(!decoded.unknown);
    const PsDecodedField *field = findField(decoded, QStringLiteral("sv_id"));
    assert(field && field->value.toInt() == 5);
}

} // namespace

int main()
{
    testFrame();
    testSchemaFields();
    testGroupedMessage();
    testVariantMessage();
    return 0;
}
