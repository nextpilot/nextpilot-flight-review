#include "Oem7MessageParser.h"
#include "datasource/FrameDecoder.h"
#include "datasource/ProtocolCatalog.h"

#include <QByteArray>
#include <QString>
#include <QVariant>

#include <cassert>
#include <cstring>

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

void putDouble(QByteArray &data, int offset, double value)
{
    quint64 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int i = 0; i < 8; ++i)
        data[offset + i] = static_cast<char>((bits >> (8 * i)) & 0xFF);
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
    put16(header, 24, 52666);

    QByteArray frame = header + body;
    const quint32 checksum = crc32(frame);
    frame.append(static_cast<char>(checksum & 0xFF));
    frame.append(static_cast<char>((checksum >> 8) & 0xFF));
    frame.append(static_cast<char>((checksum >> 16) & 0xFF));
    frame.append(static_cast<char>((checksum >> 24) & 0xFF));
    return frame;
}

void testCatalog()
{
    ProtocolCatalog catalog;
    QString error;
    assert(catalog.load(QStringLiteral(TEST_OEM7_DB), QStringLiteral("oem7"), &error));

    const PsMessageDescriptor *descriptor = catalog.find(QStringLiteral("oem7"), 42, QStringLiteral("52666"));
    assert(descriptor != nullptr);
    assert(descriptor->name == QStringLiteral("BESTPOS"));
    assert(descriptor->fields.size() == 21);
    assert(FrameDecoder::requiredSize(*descriptor) == 72);
}

void testDecoder()
{
    ProtocolCatalog catalog;
    assert(catalog.load(QStringLiteral(TEST_OEM7_DB), QStringLiteral("oem7")));
    const PsMessageDescriptor *descriptor = catalog.find(QStringLiteral("oem7"), 42);
    assert(descriptor != nullptr);

    QByteArray body(72, '\0');
    putDouble(body, 8, 1.5);
    putDouble(body, 16, 2.5);
    putDouble(body, 24, 3.5);

    QVector<PsDecodedField> fields;
    QString error;
    assert(FrameDecoder::decode(*descriptor, body, 0, fields, &error));
    assert(fields.size() == 21);

    bool foundLatitude = false;
    bool foundLongitude = false;
    for (const PsDecodedField &field : fields) {
        if (field.name == QStringLiteral("latitude")) {
            assert(field.value.toDouble() == 1.5);
            foundLatitude = true;
        }
        if (field.name == QStringLiteral("longitude")) {
            assert(field.value.toDouble() == 2.5);
            foundLongitude = true;
        }
    }
    assert(foundLatitude);
    assert(foundLongitude);

    QVector<PsDecodedField> truncated;
    assert(!FrameDecoder::decode(*descriptor, body.left(71), 0, truncated, &error));
}

void testParserAndDecoder()
{
    const QByteArray body(72, '\0');
    const QByteArray frame = makeBinaryFrame(42, body);

    Oem7MessageParser parser;
    assert(parser.loadDatabase(QStringLiteral(TEST_OEM7_DB)));
    PsDecodedFrame decoded;
    assert(parser.parse(frame, decoded));
    assert(!decoded.unknown);

    bool foundLatitude = false;
    for (const PsDecodedField &field : decoded.fields) {
        if (field.name == QStringLiteral("latitude"))
            foundLatitude = true;
    }
    assert(foundLatitude);

    QByteArray rawBody;
    for (const PsDecodedField &field : decoded.fields) {
        if (field.name == QStringLiteral("body"))
            rawBody = field.value.toByteArray();
    }
    assert(rawBody.size() == 72);
}

} // namespace

int main()
{
    testCatalog();
    testDecoder();
    testParserAndDecoder();
    return 0;
}
