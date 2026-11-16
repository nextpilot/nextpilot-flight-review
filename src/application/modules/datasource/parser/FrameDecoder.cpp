#include "datasource/FrameDecoder.h"

#include <QString>
#include <QVariant>
#include <QtEndian>

#include <cstring>

namespace
{

quint16 read16(const QByteArray &data, int offset)
{
    return static_cast<quint16>(static_cast<unsigned char>(data.at(offset))) |
           (static_cast<quint16>(static_cast<unsigned char>(data.at(offset + 1))) << 8);
}

quint32 read32(const QByteArray &data, int offset)
{
    return static_cast<quint32>(static_cast<unsigned char>(data.at(offset))) |
           (static_cast<quint32>(static_cast<unsigned char>(data.at(offset + 1))) << 8) |
           (static_cast<quint32>(static_cast<unsigned char>(data.at(offset + 2))) << 16) |
           (static_cast<quint32>(static_cast<unsigned char>(data.at(offset + 3))) << 24);
}

quint64 read64(const QByteArray &data, int offset)
{
    quint64 value = 0;
    for (int i = 0; i < 8; ++i)
        value |= static_cast<quint64>(static_cast<unsigned char>(data.at(offset + i))) << (8 * i);
    return value;
}

bool scalarValue(const QByteArray &data, int offset, const PsFieldDescriptor &field,
                 QVariant &value)
{
    const QString &type = field.dataType;
    if (type == QStringLiteral("UCHAR")) {
        value = static_cast<int>(static_cast<unsigned char>(data.at(offset)));
        return true;
    }
    if (type == QStringLiteral("CHAR")) {
        value = QString(QChar(static_cast<unsigned char>(data.at(offset))));
        return true;
    }
    if (type == QStringLiteral("USHORT")) {
        value = static_cast<int>(read16(data, offset));
        return true;
    }
    if (type == QStringLiteral("SHORT")) {
        value = static_cast<int>(static_cast<qint16>(read16(data, offset)));
        return true;
    }
    if (type == QStringLiteral("ULONG")) {
        value = read32(data, offset);
        return true;
    }
    if (type == QStringLiteral("LONG") || type == QStringLiteral("ENUM")) {
        value = static_cast<int>(static_cast<qint32>(read32(data, offset)));
        return true;
    }
    if (type == QStringLiteral("FLOAT")) {
        const quint32 bits = read32(data, offset);
        float number = 0.0f;
        std::memcpy(&number, &bits, sizeof(number));
        value = number;
        return true;
    }
    if (type == QStringLiteral("DOUBLE")) {
        const quint64 bits = read64(data, offset);
        double number = 0.0;
        std::memcpy(&number, &bits, sizeof(number));
        value = number;
        return true;
    }
    return false;
}

} // namespace

bool FrameDecoder::decodeText(const PsMessageDescriptor &descriptor, const QStringList &tokens,
                              QVector<PsDecodedField> &fields)
{
    fields.clear();
    const int count = qMin(descriptor.fields.size(), tokens.size());
    for (int i = 0; i < count; ++i) {
        const PsFieldDescriptor &field = descriptor.fields.at(i);
        PsDecodedField decoded;
        decoded.name = field.name;
        decoded.raw = tokens.at(i);
        decoded.value = tokens.at(i);
        fields.append(decoded);
    }
    return true;
}

int FrameDecoder::requiredSize(const PsMessageDescriptor &descriptor)
{
    int total = 0;
    for (const PsFieldDescriptor &field : descriptor.fields) {
        if (field.type == QStringLiteral("VARIABLE_LENGTH_ARRAY") ||
            field.type == QStringLiteral("FIELD_ARRAY"))
            return -1;
        const int count = field.arrayLength > 0 ? field.arrayLength : 1;
        total += field.length * count;
    }
    return total;
}

bool FrameDecoder::decode(const PsMessageDescriptor &descriptor, const QByteArray &data, int offset,
                          QVector<PsDecodedField> &fields, QString *error)
{
    fields.clear();
    int cursor = offset;
    for (const PsFieldDescriptor &field : descriptor.fields) {
        if (field.type == QStringLiteral("VARIABLE_LENGTH_ARRAY") ||
            field.type == QStringLiteral("FIELD_ARRAY")) {
            if (error)
                *error = QStringLiteral("unsupported field type %1").arg(field.type);
            return false;
        }

        const bool fixedArray = field.type == QStringLiteral("FIXED_LENGTH_ARRAY") ||
                                field.arrayLength > 0;
        const int count = fixedArray && field.arrayLength > 0 ? field.arrayLength : 1;
        const int size = field.length * count;
        if (size <= 0 || cursor + size > data.size()) {
            if (error)
                *error = QStringLiteral("field %1 out of range").arg(field.name);
            return false;
        }

        PsDecodedField decoded;
        decoded.name = field.name;
        decoded.offset = cursor;
        decoded.size = size;
        decoded.raw = data.mid(cursor, size);
        if (fixedArray)
            decoded.value = decoded.raw;
        else if (!scalarValue(data, cursor, field, decoded.value)) {
            if (error)
                *error = QStringLiteral("unsupported data type %1").arg(field.dataType);
            return false;
        }
        fields.append(decoded);
        cursor += size;
    }
    return true;
}
