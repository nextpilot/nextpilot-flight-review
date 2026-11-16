#include "Oem7MessageParser.h"

#include <QByteArray>
#include <QString>
#include <QVariant>

#include <cassert>

namespace
{

quint32 crc32(const QByteArray &data)
{
    quint32 crc = 0;
    for (const char c : data) {
        crc ^= static_cast<unsigned char>(c);
        for (int i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

void put16(QByteArray &data, int offset, quint16 value)
{
    data[offset] = static_cast<char>(value & 0xFF);
    data[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
}

void put32(QByteArray &data, int offset, quint32 value)
{
    data[offset] = static_cast<char>(value & 0xFF);
    data[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
    data[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
    data[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
}

QByteArray makeBinaryFrame(quint16 messageId, const QByteArray &body)
{
    QByteArray header(28, '\0');
    header[0] = static_cast<char>(0xAA);
    header[1] = static_cast<char>(0x44);
    header[2] = static_cast<char>(0x12);
    header[3] = 28;
    put16(header, 4, messageId);
    put16(header, 8, static_cast<quint16>(body.size()));
    put16(header, 14, 2209);
    put32(header, 16, 510354000);
    put32(header, 20, 0x12000020);
    put16(header, 24, 16809);

    QByteArray frame = header + body;
    const quint32 checksum = crc32(frame);
    frame.append(static_cast<char>(checksum & 0xFF));
    frame.append(static_cast<char>((checksum >> 8) & 0xFF));
    frame.append(static_cast<char>((checksum >> 16) & 0xFF));
    frame.append(static_cast<char>((checksum >> 24) & 0xFF));
    return frame;
}

QByteArray makeAsciiFrame(const QByteArray &text)
{
    const quint32 checksum = crc32(text);
    QByteArray line = text;
    line.append('*');
    line.append(QStringLiteral("%1").arg(checksum, 8, 16, QLatin1Char('0')).toUpper().toLatin1());
    return line;
}

void testBinary()
{
    Oem7MessageParser parser;
    assert(parser.protocolId() == QStringLiteral("oem7"));
    assert(parser.sniff(makeBinaryFrame(42, "abcd")));
    assert(!parser.sniff("$GPGGA,1*00"));

    const QByteArray frame = makeBinaryFrame(42, "abcd");
    PsDecodedFrame decoded;
    assert(parser.parse(frame, decoded));
    assert(decoded.protocol == QStringLiteral("oem7"));
    assert(decoded.frameKey == QStringLiteral("#42"));
    assert(decoded.frameName == QStringLiteral("#42"));
    assert(decoded.checksumOk);
    assert(decoded.rawBytes == frame);
    assert(decoded.timestamp > 0);
    assert(decoded.fields.size() >= 8);
    assert(decoded.fields.at(0).name == QStringLiteral("messageId"));
    assert(decoded.fields.at(0).value.toInt() == 42);
    assert(decoded.fields.at(3).name == QStringLiteral("length"));
    assert(decoded.fields.at(3).value.toInt() == 4);
    assert(decoded.fields.at(decoded.fields.size() - 2).value.toByteArray() == QByteArray("abcd"));

    QByteArray corrupted = frame;
    corrupted[corrupted.size() - 5] = static_cast<char>(0x00);
    PsDecodedFrame failed;
    assert(parser.parse(corrupted, failed));
    assert(!failed.checksumOk);

    QByteArray truncated = frame;
    truncated.chop(1);
    PsDecodedFrame invalid;
    assert(!parser.parse(truncated, invalid));
}

void testAscii()
{
    Oem7MessageParser parser;
    assert(parser.loadDatabase(QStringLiteral(TEST_OEM7_DB)));
    const QByteArray text =
        "#BESTPOSA,USB1,0,51.5,FINESTEERING,2209,510354.000,12000020,3303,16809;"
        "SOL_COMPUTED,WAAS,1.5,2.5,3.5,4.5,0,0.1,0.2,0.3,0000,0.5,0.6,12,10,8,6,2,0,"
        "0x3,0x1";
    const QByteArray frame = makeAsciiFrame(text);
    PsDecodedFrame decoded;
    assert(parser.parse(frame, decoded));
    assert(decoded.protocol == QStringLiteral("oem7"));
    assert(decoded.frameKey == QStringLiteral("BESTPOSA"));
    assert(decoded.checksumOk);
    assert(decoded.rawBytes == frame);
    assert(decoded.fields.first().name == QStringLiteral("logName"));
    assert(decoded.fields.first().value.toString() == QStringLiteral("BESTPOSA"));
    assert(decoded.timestamp > 2209.0 * 604800.0);
    assert(!decoded.unknown);

    bool foundStatus = false;
    bool foundLatitude = false;
    for (const PsDecodedField &field : decoded.fields) {
        if (field.name == QStringLiteral("solution_status")) {
            assert(field.value.toString() == QStringLiteral("SOL_COMPUTED"));
            foundStatus = true;
        }
        if (field.name == QStringLiteral("latitude")) {
            assert(field.value.toString() == QStringLiteral("1.5"));
            foundLatitude = true;
        }
    }
    assert(foundStatus);
    assert(foundLatitude);

    QByteArray corrupted = frame;
    corrupted[corrupted.size() - 1] = corrupted.at(corrupted.size() - 1) == '0' ? '1' : '0';
    PsDecodedFrame failed;
    assert(parser.parse(corrupted, failed));
    assert(!failed.checksumOk);
}

} // namespace

int main()
{
    testBinary();
    testAscii();
    return 0;
}
