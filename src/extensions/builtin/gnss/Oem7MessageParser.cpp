#include "Oem7MessageParser.h"
#include "datasource/FrameDecoder.h"

#include <QCoreApplication>
#include <QDir>
#include <QString>
#include <QStringList>
#include <QVariant>
namespace
{

quint16 rd16(const unsigned char *p)
{
    return static_cast<quint16>(p[0]) | (static_cast<quint16>(p[1]) << 8);
}

quint32 rd32(const unsigned char *p)
{
    return static_cast<quint32>(p[0]) | (static_cast<quint32>(p[1]) << 8) |
           (static_cast<quint32>(p[2]) << 16) | (static_cast<quint32>(p[3]) << 24);
}

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

QByteArray firstLine(const QByteArray &data)
{
    QByteArray line = data;
    const int nl = line.indexOf('\n');
    if (nl >= 0)
        line.truncate(nl);
    return line.trimmed();
}

void addField(PsDecodedFrame &out, const QString &name, const QVariant &value, int offset = -1,
              int size = 0)
{
    PsDecodedField field;
    field.name = name;
    field.value = value;
    field.offset = offset;
    field.size = size;
    out.fields.append(field);
}

bool parseBinary(const QByteArray &data, PsDecodedFrame &out, const ProtocolCatalog *catalog)
{
    const unsigned char *p = reinterpret_cast<const unsigned char *>(data.constData());
    if (p[2] != 0x12 && p[2] != 0x13)
        return false;
    const int headerLength = p[2] == 0x12 ? static_cast<int>(p[3]) : 12;
    if (headerLength < 12 || data.size() < headerLength)
        return false;

    const int bodyLength = p[2] == 0x12 ? rd16(p + 8) : static_cast<int>(p[3]);
    const int total = headerLength + bodyLength + 4;
    if (data.size() < total)
        return false;

    const QByteArray frame = data.left(total);
    const int messageId = rd16(p + 4);
    const int week = p[2] == 0x12 ? rd16(p + 14) : rd16(p + 6);
    const int milliseconds = p[2] == 0x12 ? rd32(p + 16) : rd32(p + 8);

    out.protocol = QStringLiteral("oem7");
    out.frameKey = QStringLiteral("#%1").arg(messageId);
    out.frameName = out.frameKey;
    out.rawBytes = frame;
    out.unknown = false;
    out.timestamp = week * 604800.0 + milliseconds / 1000.0;
    out.checksumOk =
        rd32(reinterpret_cast<const unsigned char *>(frame.constData()) + headerLength +
             bodyLength) == crc32(frame.left(headerLength + bodyLength));
    out.fields.clear();

    addField(out, QStringLiteral("messageId"), messageId, 4, 2);
    addField(out, QStringLiteral("messageType"), p[6], 6, 1);
    addField(out, QStringLiteral("port"), p[7], 7, 1);
    addField(out, QStringLiteral("length"), bodyLength, 8, 2);
    if (p[2] == 0x12) {
        addField(out, QStringLiteral("sequence"), rd16(p + 10), 10, 2);
        addField(out, QStringLiteral("timeStatus"), p[13], 13, 1);
        addField(out, QStringLiteral("week"), week, 14, 2);
        addField(out, QStringLiteral("milliseconds"), milliseconds, 16, 4);
        addField(out, QStringLiteral("receiverStatus"), rd32(p + 20), 20, 4);
        addField(out, QStringLiteral("messageDefinitionCrc"), rd16(p + 24), 24, 2);
    } else {
        addField(out, QStringLiteral("week"), week, 6, 2);
        addField(out, QStringLiteral("milliseconds"), milliseconds, 8, 4);
    }
    addField(out, QStringLiteral("body"), frame.mid(headerLength, bodyLength), headerLength,
             bodyLength);
    addField(out, QStringLiteral("checksum"),
             rd32(reinterpret_cast<const unsigned char *>(frame.constData()) + headerLength +
                  bodyLength),
             headerLength + bodyLength, 4);

    if (catalog) {
        const QString variant =
            p[2] == 0x12 ? QString::number(rd16(p + 24)) : QString();
        const PsMessageDescriptor *descriptor = catalog->find(QStringLiteral("oem7"), messageId,
                                                              variant);
        if (!descriptor) {
            out.unknown = true;
        } else {
            QVector<PsDecodedField> bodyFields;
            if (FrameDecoder::decode(*descriptor, frame, headerLength, bodyFields))
                out.fields.append(bodyFields);
        }
    }
    return true;
}

bool parseAscii(const QByteArray &data, PsDecodedFrame &out, const ProtocolCatalog *catalog)
{
    const QByteArray line = firstLine(data);
    if (!line.startsWith('#') && !line.startsWith('%'))
        return false;

    const int star = line.indexOf('*');
    const QByteArray crcText = star >= 0 ? line.mid(star + 1).trimmed() : QByteArray();
    bool ok = false;
    const quint32 expected = crcText.toUInt(&ok, 16);
    const quint32 actual = crc32(line.left(star >= 0 ? star : line.size()));

    const int semicolon = line.indexOf(';');
    const QByteArray headerPart = line.mid(1, semicolon >= 0 ? semicolon - 1 : line.size() - 1);
    const QList<QByteArray> tokens = headerPart.split(',');
    if (tokens.isEmpty() || tokens.first().isEmpty())
        return false;

    out.protocol = QStringLiteral("oem7");
    out.frameKey = QString::fromLatin1(tokens.first());
    out.frameName = out.frameKey;
    out.rawBytes = line;
    out.unknown = false;
    out.checksumOk = star >= 0 && ok && expected == actual;
    out.fields.clear();

    static const char *const names[] = {"logName", "port", "sequence", "idleTime",
                                        "timeStatus", "week", "milliseconds", "receiverStatus",
                                        "reserved", "messageDefinitionCrc"};
    for (int i = 0; i < tokens.size() && i < static_cast<int>(sizeof(names) / sizeof(names[0]));
         ++i) {
        addField(out, QString::fromLatin1(names[i]), QString::fromLatin1(tokens.at(i)));
    }

    const bool weekOk = tokens.size() > 5;
    const bool msOk = tokens.size() > 6;
    bool weekValue = false;
    bool msValue = false;
    const int week = weekOk ? tokens.at(5).toInt(&weekValue) : 0;
    const double ms = msOk ? tokens.at(6).toDouble(&msValue) : 0.0;
    if (weekValue && msValue)
        out.timestamp = week * 604800.0 + ms / 1000.0;

    if (semicolon >= 0) {
        const int bodyEnd = star >= 0 ? star : line.size();
        const QString bodyText = QString::fromLatin1(
            line.mid(semicolon + 1, bodyEnd - semicolon - 1));
        addField(out, QStringLiteral("body"), bodyText);

        if (catalog) {
            QString logName = out.frameKey;
            if (logName.endsWith(QLatin1Char('A')))
                logName.chop(1);
            const QString variant =
                tokens.size() > 9 ? QString::fromLatin1(tokens.at(9)).trimmed() : QString();
            const PsMessageDescriptor *descriptor =
                catalog->findByName(QStringLiteral("oem7"), logName.toUpper(), variant);
            if (!descriptor) {
                out.unknown = true;
            } else {
                QVector<PsDecodedField> bodyFields;
                FrameDecoder::decodeText(*descriptor,
                                         bodyText.split(QLatin1Char(','), Qt::KeepEmptyParts),
                                         bodyFields);
                out.fields.append(bodyFields);
            }
        }
    }
    addField(out, QStringLiteral("checksum"), QString::fromLatin1(crcText));
    return true;
}

} // namespace

Oem7MessageParser::Oem7MessageParser()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/oem7_database.json"),
        appDir + QStringLiteral("/../oem7_database.json"),
        appDir + QStringLiteral("/../../oem7_database.json"),
        QDir::currentPath() + QStringLiteral("/oem7_database.json"),
        QDir::currentPath() + QStringLiteral("/src/extensions/builtin/gnss/oem7_database.json")};
    for (const QString &path : candidates) {
        if (loadDatabase(path))
            return;
    }
}

Oem7MessageParser::Oem7MessageParser(const QString &databasePath)
{
    loadDatabase(databasePath);
}

bool Oem7MessageParser::loadDatabase(const QString &path)
{
    m_loaded = m_catalog.load(path, QStringLiteral("oem7"));
    return m_loaded;
}

QString Oem7MessageParser::protocolId() const
{
    return QStringLiteral("oem7");
}

bool Oem7MessageParser::sniff(const QByteArray &data) const
{
    if (data.size() >= 3 && static_cast<unsigned char>(data.at(0)) == 0xAA &&
        static_cast<unsigned char>(data.at(1)) == 0x44 &&
        (static_cast<unsigned char>(data.at(2)) == 0x12 ||
         static_cast<unsigned char>(data.at(2)) == 0x13))
        return true;
    const QByteArray line = firstLine(data);
    return line.startsWith('#') || line.startsWith('%');
}

bool Oem7MessageParser::parse(const QByteArray &data, PsDecodedFrame &out) const
{
    if (data.size() >= 3 && static_cast<unsigned char>(data.at(0)) == 0xAA &&
        static_cast<unsigned char>(data.at(1)) == 0x44)
        return parseBinary(data, out, m_loaded ? &m_catalog : nullptr);
    return parseAscii(data, out, m_loaded ? &m_catalog : nullptr);
}
