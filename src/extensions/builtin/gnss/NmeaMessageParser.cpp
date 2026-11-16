#include "NmeaMessageParser.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
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

QString degreesMinutes(const QString &text, const QString &hemisphere)
{
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok)
        return text;
    const int degrees = static_cast<int>(value / 100.0);
    const double minutes = value - degrees * 100.0;
    double result = degrees + minutes / 60.0;
    if (hemisphere.compare(QStringLiteral("S"), Qt::CaseInsensitive) == 0 ||
        hemisphere.compare(QStringLiteral("W"), Qt::CaseInsensitive) == 0)
        result = -result;
    return QString::number(result, 'f', 9);
}

double utcSeconds(const QString &text)
{
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok)
        return 0.0;
    const int hours = static_cast<int>(value / 10000.0);
    const int minutes = static_cast<int>((value - hours * 10000.0) / 100.0);
    const double seconds = value - hours * 10000.0 - minutes * 100.0;
    return hours * 3600.0 + minutes * 60.0 + seconds;
}

PsDecodedField textField(const QString &name, const QString &raw, const QString &unit,
                         const QString &description)
{
    PsDecodedField field;
    field.name = name;
    field.raw = raw;
    field.value = raw;
    field.unit = unit;
    field.description = description;
    return field;
}

} // namespace

NmeaMessageParser::NmeaMessageParser()
{
    loadDatabase();
}

NmeaMessageParser::NmeaMessageParser(const QString &databasePath)
{
    QFile file(databasePath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonArray sentences = document.object().value(QStringLiteral("sentences")).toArray();
    for (const QJsonValue &value : sentences) {
        const QJsonObject sentence = value.toObject();
        const QString key = sentence.value(QStringLiteral("key")).toString();
        QVector<FieldDef> fields;
        for (const QJsonValue &fieldValue : sentence.value(QStringLiteral("fields")).toArray()) {
            const QJsonObject object = fieldValue.toObject();
            FieldDef field;
            field.name = object.value(QStringLiteral("name")).toString();
            field.type = object.value(QStringLiteral("type")).toString();
            field.unit = object.value(QStringLiteral("unit")).toString();
            field.label = object.value(QStringLiteral("label")).toString();
            field.index = object.value(QStringLiteral("index")).toInt(-1);
            field.hemiIndex = object.value(QStringLiteral("hemiIndex")).toInt(-1);
            if (!field.name.isEmpty() && field.index > 0)
                fields.append(field);
        }
        if (!key.isEmpty() && !fields.isEmpty())
            m_sentences.insert(key, fields);
    }
}

void NmeaMessageParser::loadDatabase()
{
    if (!m_sentences.isEmpty())
        return;
    const QString appDir = QCoreApplication::instance() ? QCoreApplication::applicationDirPath()
                                                        : QString();
    const QStringList candidates = {
#ifdef PS_GNSS_DB_DIR
        QStringLiteral(PS_GNSS_DB_DIR) + QStringLiteral("/nmea_database.json"),
#endif
        appDir + QStringLiteral("/nmea_database.json"),
        appDir + QStringLiteral("/../nmea_database.json"),
        QDir::currentPath() + QStringLiteral("/nmea_database.json"),
        QDir::currentPath() + QStringLiteral("/src/extensions/builtin/gnss/nmea_database.json")};
    for (const QString &path : candidates) {
        if (!QFile::exists(path))
            continue;
        NmeaMessageParser parser(path);
        m_sentences = parser.m_sentences;
        if (!m_sentences.isEmpty())
            return;
    }
}

QString NmeaMessageParser::protocolId() const
{
    return QStringLiteral("nmea");
}

bool NmeaMessageParser::sniffData(const QByteArray &data)
{
    return firstLine(data).startsWith('$');
}

bool NmeaMessageParser::sniff(const QByteArray &data) const
{
    return sniffData(data);
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

    const QList<QByteArray> parts = payload.split(',');
    if (parts.isEmpty())
        return false;

    const QString head = QString::fromLatin1(parts.first()).trimmed();
    const bool proprietary = head.startsWith(QLatin1Char('P'));
    const QString talker = proprietary ? head.left(1) : head.left(2);
    const QString sentence = proprietary ? head.mid(1) : head.mid(2);

    out.protocol = protocolId();
    out.frameKey = head;
    out.frameName = head;
    out.rawBytes = line;
    out.checksumOk = ok && expected == actual;
    out.unknown = false;
    out.fields.clear();
    out.timestamp = 0.0;

    out.fields.append(textField(QStringLiteral("talker"), talker, {}, {}));
    out.fields.append(textField(QStringLiteral("sentence"), sentence, {}, {}));

    const auto schemaIt = m_sentences.constFind(sentence);
    if (schemaIt == m_sentences.constEnd()) {
        out.unknown = true;
        for (int i = 1; i < parts.size(); ++i)
            out.fields.append(textField(QStringLiteral("field_%1").arg(i),
                                        QString::fromLatin1(parts.at(i)), {}, {}));
    } else {
        QSet<int> covered;
        for (const FieldDef &definition : schemaIt.value()) {
            if (definition.index >= parts.size())
                continue;
            covered.insert(definition.index);
            if (definition.hemiIndex > 0)
                covered.insert(definition.hemiIndex);
            const QString raw = QString::fromLatin1(parts.at(definition.index));
            PsDecodedField field = textField(definition.name, raw, definition.unit,
                                             definition.label);

            if (definition.type == QLatin1String("dm")) {
                const QString hemi = definition.hemiIndex >= 0 &&
                                             definition.hemiIndex < parts.size()
                                         ? QString::fromLatin1(parts.at(definition.hemiIndex))
                                         : QString();
                field.value = degreesMinutes(raw, hemi);
            } else if (definition.type == QLatin1String("number")) {
                bool numberOk = false;
                const double number = raw.toDouble(&numberOk);
                if (numberOk)
                    field.value = number;
            } else if (definition.type == QLatin1String("int")) {
                bool intOk = false;
                const int number = raw.toInt(&intOk);
                if (intOk)
                    field.value = number;
            } else if (definition.type == QLatin1String("time")) {
                const double seconds = utcSeconds(raw);
                if (seconds > 0.0 && out.timestamp == 0.0)
                    out.timestamp = seconds;
            }
            out.fields.append(field);
        }

        for (int i = 1; i < parts.size(); ++i) {
            if (!covered.contains(i))
                out.fields.append(textField(QStringLiteral("extra_%1").arg(i),
                                            QString::fromLatin1(parts.at(i)), {}, {}));
        }
    }

    PsDecodedField checksumField;
    checksumField.name = QStringLiteral("checksum");
    checksumField.raw = QString::fromLatin1(checksumText);
    checksumField.value = out.checksumOk;
    out.fields.append(checksumField);
    return true;
}
