#include "NmeaMessageParser.h"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QVariant>

namespace nextpilot::plotscope::ext
{
namespace
{

QByteArray firstLine(const QByteArray &data)
{
    QByteArray line = data;
    const int nl = line.indexOf('\n');
    if (nl >= 0)
        line.truncate(nl);
    const int cr = line.indexOf('\r');
    if (cr >= 0)
        line.truncate(cr);
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

nextpilot::plotscope::core::DecodedField textField(const QString &name, const QString &raw, const QString &unit,
                                 const QString &description)
{
    nextpilot::plotscope::core::DecodedField f;
    f.name = name;
    f.raw = raw;
    f.value = raw;
    f.unit = unit;
    f.description = description;
    return f;
}

} // namespace

// 内置精简模板：GGA / RMC / GSV 三种常见句子。字段与老 nmea_database.json 同构，
// 但不需要外部文件——无 configure 也能独立解析。完整模板由宿主经 database_path 传入。
QByteArray NmeaMessageParser::builtinTemplate()
{
    return R"({"version":2,"sentences":[
      {"key":"GGA","fields":[
        {"name":"time","index":1,"type":"time","label":"UTC 时间"},
        {"name":"lat","index":2,"type":"dm","hemiIndex":3,"label":"纬度","unit":"deg"},
        {"name":"lon","index":4,"type":"dm","hemiIndex":5,"label":"经度","unit":"deg"},
        {"name":"quality","index":6,"type":"int","label":"定位质量"},
        {"name":"satellites","index":7,"type":"int","label":"卫星数"},
        {"name":"hdop","index":8,"type":"number","label":"水平精度因子"},
        {"name":"altitude","index":9,"type":"number","label":"海拔","unit":"m"}
      ]},
      {"key":"RMC","fields":[
        {"name":"time","index":1,"type":"time","label":"UTC 时间"},
        {"name":"status","index":2,"type":"string","label":"状态"},
        {"name":"lat","index":3,"type":"dm","hemiIndex":4,"label":"纬度","unit":"deg"},
        {"name":"lon","index":5,"type":"dm","hemiIndex":6,"label":"经度","unit":"deg"},
        {"name":"speed","index":7,"type":"number","label":"速度","unit":"kn"},
        {"name":"course","index":8,"type":"number","label":"航向","unit":"deg"},
        {"name":"date","index":9,"type":"string","label":"日期"}
      ]},
      {"key":"GSV","fields":[
        {"name":"total_msgs","index":1,"type":"int","label":"总消息数"},
        {"name":"msg_no","index":2,"type":"int","label":"本消息序号"},
        {"name":"sat_count","index":3,"type":"int","label":"可见卫星数"}
      ]}
    ]})";
}

QHash<QString, QVector<NmeaMessageParser::FieldDef>>
NmeaMessageParser::parseTemplate(const QByteArray &json)
{
    QHash<QString, QVector<FieldDef>> out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return out;
    const QJsonArray sentences = doc.object().value(QStringLiteral("sentences")).toArray();
    for (const QJsonValue &sv : sentences)
    {
        const QJsonObject so = sv.toObject();
        const QString key = so.value(QStringLiteral("key")).toString();
        QVector<FieldDef> fields;
        const QJsonArray fa = so.value(QStringLiteral("fields")).toArray();
        for (const QJsonValue &fv : fa)
        {
            const QJsonObject fo = fv.toObject();
            FieldDef f;
            f.name = fo.value(QStringLiteral("name")).toString();
            f.type = fo.value(QStringLiteral("type")).toString();
            f.unit = fo.value(QStringLiteral("unit")).toString();
            f.label = fo.value(QStringLiteral("label")).toString();
            f.index = fo.value(QStringLiteral("index")).toInt(-1);
            f.hemiIndex = fo.value(QStringLiteral("hemiIndex")).toInt(-1);
            if (!f.name.isEmpty() && f.index >= 0)
                fields.append(f);
        }
        if (!key.isEmpty() && !fields.isEmpty())
            out.insert(key, fields);
    }
    return out;
}

NmeaMessageParser::NmeaMessageParser()
    : m_sentences(parseTemplate(builtinTemplate()))
{
}

QJsonObject NmeaMessageParser::configSchema() const
{
    QJsonObject s;
    s[QStringLiteral("title")] = QStringLiteral("NMEA 解析选项");
    QJsonArray fields;
    fields.append(QJsonObject{{QStringLiteral("key"), QStringLiteral("database_path")},
                              {QStringLiteral("type"), QStringLiteral("file")},
                              {QStringLiteral("label"), QStringLiteral("句子模板 JSON（可选）")},
                              {QStringLiteral("default"), QString()},
                              {QStringLiteral("hint"),
                               QStringLiteral("留空使用内置 GGA/RMC/GSV 模板；指定后用该文件的 sentences 字段")}});
    s[QStringLiteral("fields")] = fields;
    return s;
}

bool NmeaMessageParser::configure(const QJsonObject &params, QString *err)
{
    if (!IMessageParser::configure(params, err))
        return false;

    // 唯一配置来源：本次传入的 database_path。不读 QCoreApplication / 当前目录等全局。
    const QString dbPath = param(QStringLiteral("database_path")).toString();
    if (dbPath.isEmpty())
    {
        m_sentences = parseTemplate(builtinTemplate()); // 默认：内置模板
        return true;
    }

    QFile file(dbPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (err)
            *err = QStringLiteral("NMEA: 无法打开句子模板 %1").arg(dbPath);
        return false;
    }
    const QByteArray json = file.readAll();
    const auto parsed = parseTemplate(json);
    if (parsed.isEmpty())
    {
        if (err)
            *err = QStringLiteral("NMEA: 句子模板无有效 sentences：%1").arg(dbPath);
        return false;
    }
    m_sentences = parsed;
    return true;
}

bool NmeaMessageParser::sniff(const QByteArray &head) const
{
    return firstLine(head).startsWith('$');
}

bool NmeaMessageParser::parse(const QByteArray &bytes, nextpilot::plotscope::core::DecodedFrame &out) const
{
    const QByteArray line = firstLine(bytes);
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
    const bool proprietary = head.startsWith('P');
    const QString talker = proprietary ? head.left(1) : head.left(2);
    const QString sentence = proprietary ? head.mid(1) : head.mid(2);

    out.protocol = id();
    out.frameKey = head;
    out.frameName = sentence;
    out.rawBytes = line;
    out.checksumOk = ok && expected == actual;
    out.unknown = false;
    out.fields.clear();
    out.timestamp = 0.0;

    out.fields.append(textField(QStringLiteral("talker"), talker, {}, {}));
    out.fields.append(textField(QStringLiteral("sentence"), sentence, {}, {}));

    const auto it = m_sentences.constFind(sentence);
    if (it == m_sentences.constEnd())
    {
        out.unknown = true;
        for (int i = 1; i < parts.size(); ++i)
            out.fields.append(textField(QStringLiteral("field_%1").arg(i),
                                        QString::fromLatin1(parts.at(i)), {}, {}));
    }
    else
    {
        QSet<int> covered;
        for (const FieldDef &def : it.value())
        {
            if (def.index >= parts.size())
                continue;
            covered.insert(def.index);
            if (def.hemiIndex > 0)
                covered.insert(def.hemiIndex);
            const QString raw = QString::fromLatin1(parts.at(def.index));
            auto f = textField(def.name, raw, def.unit, def.label);
            f.offset = def.index;

            if (def.type == QLatin1String("dm"))
            {
                const QString hemi = def.hemiIndex >= 0 && def.hemiIndex < parts.size()
                                         ? QString::fromLatin1(parts.at(def.hemiIndex))
                                         : QString();
                f.value = degreesMinutes(raw, hemi);
            }
            else if (def.type == QLatin1String("number"))
            {
                bool numOk = false;
                const double number = raw.toDouble(&numOk);
                if (numOk)
                    f.value = number;
            }
            else if (def.type == QLatin1String("int"))
            {
                bool intOk = false;
                const int number = raw.toInt(&intOk);
                if (intOk)
                    f.value = number;
            }
            else if (def.type == QLatin1String("time"))
            {
                const double seconds = utcSeconds(raw);
                if (seconds > 0.0 && out.timestamp == 0.0)
                    out.timestamp = seconds;
            }
            out.fields.append(f);
        }
        for (int i = 1; i < parts.size(); ++i)
            if (!covered.contains(i))
                out.fields.append(textField(QStringLiteral("extra_%1").arg(i),
                                            QString::fromLatin1(parts.at(i)), {}, {}));
    }

    auto cs = textField(QStringLiteral("checksum"), QString::fromLatin1(checksumText), {}, {});
    cs.value = out.checksumOk;
    out.fields.append(cs);
    return true;
}

} // namespace nextpilot::plotscope::ext
