#include "ps_plugin_api.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTcpSocket>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace {

int32_t writeJson(const char *json, char *out, uint32_t cap)
{
    if (!out || cap == 0 || !json)
        return -1;
    const size_t len = std::strlen(json);
    if (len + 1 > cap)
        return -1;
    std::memcpy(out, json, len + 1);
    return static_cast<int32_t>(len);
}

const char *kSchema = R"({
  "title":"NovAtel OEM7 接收机",
  "fields":[
    {"key":"host","type":"string","label":"接收机地址","default":"192.168.1.20"},
    {"key":"port","type":"int","label":"TCP 端口","default":3001,"min":1,"max":65535},
    {"key":"timeout_ms","type":"int","label":"连接超时","default":2000,"min":100,"max":10000,"suffix":" ms"},
    {"key":"raw","type":"bool","label":"保留未知报文（调试）","default":true},
    {"key":"database","type":"string","label":"OEM7 协议库（空=自动查找）","default":""}
  ]
})";
const char *kDefaults = R"({"host":"192.168.1.20","port":3001,"timeout_ms":2000,"raw":true,"database":""})";

struct OemState
{
    PsSourceSink sink{};
    std::unique_ptr<QTcpSocket> socket;
    QElapsedTimer clock;
    QByteArray rx;
    QHash<QString, QVector<double>> times;
    QHash<QString, QVector<double>> values;
    QHash<QString, QString> units;
    QStringList order;
    QHash<QString, QJsonObject> defs;
    QHash<QString, QHash<QString, int>> enums;
    bool dbLoaded = false;
    QString dbPath;
    uint64_t lines = 0;
    uint64_t logs = 0;
    uint64_t events = 0;
    double last_sow = 0.0;
};

void addSample(OemState *st, const QString &name, const QString &unit, double t, double v)
{
    if (!st->times.contains(name))
        st->order.append(name);
    st->times[name].append(t);
    st->values[name].append(v);
    if (!st->units.contains(name))
        st->units.insert(name, unit);
}

void flush(OemState *st)
{
    for (const QString &name : st->order)
    {
        const QVector<double> &t = st->times.value(name);
        const QVector<double> &v = st->values.value(name);
        const uint32_t n = static_cast<uint32_t>(qMin(t.size(), v.size()));
        if (!n)
            continue;
        st->sink.channel_batch(st->sink.ctx, name.toUtf8().constData(),
                               st->units.value(name).toUtf8().constData(),
                               t.constData(), v.constData(), n);
    }
    st->order.clear();
    st->times.clear();
    st->values.clear();
}

bool parseDouble(const QString &s, double *out)
{
    bool ok = false;
    const double v = s.trimmed().toDouble(&ok);
    if (ok && std::isfinite(v) && out)
        *out = v;
    return ok;
}

QStringList splitCsv(const QString &text)
{
    QStringList out;
    QString cur;
    bool quoted = false;
    for (QChar c : text)
    {
        if (c == QLatin1Char('"'))
        {
            quoted = !quoted;
            continue;
        }
        if (c == QLatin1Char(',') && !quoted)
        {
            out.append(cur);
            cur.clear();
            continue;
        }
        cur.append(c);
    }
    out.append(cur);
    return out;
}

bool numericType(const QString &type)
{
    const QString t = type.toUpper();
    return t == QStringLiteral("DOUBLE") || t == QStringLiteral("FLOAT") ||
           t == QStringLiteral("INT") || t == QStringLiteral("UINT") ||
           t == QStringLiteral("SHORT") || t == QStringLiteral("USHORT") ||
           t == QStringLiteral("LONG") || t == QStringLiteral("ULONG") ||
           t == QStringLiteral("UCHAR") || t == QStringLiteral("CHAR") ||
           t == QStringLiteral("HEX") || t == QStringLiteral("BOOL");
}

// 用 EDIE database.json 的字段定义解码 ASCII 日志；同一日志按 header CRC 选择版本。
bool parseGeneric(OemState *st, const QString &logName, const QStringList &head,
                  const QStringList &body, double t)
{
    const QJsonObject def = st->defs.value(logName);
    if (def.isEmpty())
        return false;
    const QJsonObject fields = def.value(QStringLiteral("fields")).toObject();
    QString crcKey;
    if (head.size() > 9)
    {
        bool ok = false;
        const uint crc = head.at(9).toUInt(&ok, 16);
        if (ok)
            crcKey = QString::number(crc);
    }
    QJsonArray arr = fields.value(crcKey).toArray();
    if (arr.isEmpty())
        arr = fields.value(def.value(QStringLiteral("latestMsgDefCrc")).toString()).toArray();
    if (arr.isEmpty())
        return false;

    int emitted = 0;
    for (int i = 0; i < arr.size() && i < body.size() && emitted < 64; ++i)
    {
        const QJsonObject f = arr.at(i).toObject();
        const QString fname = f.value(QStringLiteral("name")).toString();
        if (fname.isEmpty() || fname.contains(QStringLiteral("reserved")))
            continue;
        const QString type = f.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("FIXED_LENGTH_ARRAY") || type == QStringLiteral("VARIABLE_LENGTH_ARRAY"))
            continue;
        if (f.contains(QStringLiteral("arrayLength")) &&
            !f.value(QStringLiteral("arrayLength")).isNull())
            continue;

        double value = 0.0;
        bool ok = false;
        if (type == QStringLiteral("ENUM"))
        {
            const QString label = body.at(i).trimmed();
            const QString enumId = f.value(QStringLiteral("enumID")).toString();
            if (st->enums.contains(enumId) && st->enums.value(enumId).contains(label))
            {
                value = st->enums.value(enumId).value(label);
                ok = true;
            }
            else
            {
                value = label.toDouble(&ok);
            }
        }
        else
        {
            const QJsonObject dataType = f.value(QStringLiteral("dataType")).toObject();
            if (!numericType(dataType.value(QStringLiteral("name")).toString()))
                continue;
            value = body.at(i).trimmed().toDouble(&ok);
        }
        if (!ok || !std::isfinite(value))
            continue;

        QString unit;
        const QString lower = fname.toLower();
        if (lower.contains(QStringLiteral("latitude")) || lower.contains(QStringLiteral("longitude")) ||
            lower.contains(QStringLiteral("azimuth")) || lower.contains(QStringLiteral("roll")) ||
            lower.contains(QStringLiteral("pitch")) || lower.contains(QStringLiteral("heading")))
            unit = QStringLiteral("deg");
        else if (lower.contains(QStringLiteral("height")) || lower.contains(QStringLiteral("altitude")) ||
                 lower.contains(QStringLiteral("undulation")))
            unit = QStringLiteral("m");
        else if (lower.contains(QStringLiteral("velocity")) || lower.contains(QStringLiteral("speed")))
            unit = QStringLiteral("m/s");

        addSample(st, QStringLiteral("oem7.%1.%2").arg(logName.toLower(), fname),
                  unit, t, value);
        ++emitted;
    }
    return emitted > 0;
}

// 参考 NovAtel EDIE 的 OEM7 ASCII header：日志名+端口+序号+...; 身体字段以逗号分隔。
// 这里只抽取调试/分析最常用的位置、速度、姿态字段，原始行通过 event 交给宿主。
void parseNovatelLine(OemState *st, const QString &line)
{
    const int semi = line.indexOf(QLatin1Char(';'));
    if (semi < 0)
        return;
    const QStringList head = line.left(semi).split(QLatin1Char(','));
    const QStringList body = splitCsv(line.mid(semi + 1));
    if (head.size() < 7 || body.isEmpty())
        return;
    QString name = head.at(0);
    name.remove(0, 1); // # 或 <
    name = name.trimmed().toUpper();
    const bool ascii = name.endsWith(QLatin1Char('A'));
    if (ascii)
        name.chop(1);
    bool ok = false;
    const double sow = head.at(6).toDouble(&ok);
    const double t = ok ? sow : st->clock.elapsed() / 1000.0;
    st->last_sow = t;
    ++st->logs;
    if (st->dbLoaded && parseGeneric(st, name, head, body, t))
        return;

    if (name == QStringLiteral("BESTPOS") && body.size() >= 6)
    {
        double lat = 0, lon = 0, hgt = 0, und = 0;
        if (parseDouble(body.at(2), &lat) && parseDouble(body.at(3), &lon))
        {
            addSample(st, QStringLiteral("oem7.bestpos.lat"), QStringLiteral("deg"), t, lat);
            addSample(st, QStringLiteral("oem7.bestpos.lon"), QStringLiteral("deg"), t, lon);
            if (parseDouble(body.at(4), &hgt))
                addSample(st, QStringLiteral("oem7.bestpos.height"), QStringLiteral("m"), t, hgt);
            if (parseDouble(body.at(5), &und))
                addSample(st, QStringLiteral("oem7.bestpos.undulation"), QStringLiteral("m"), t, und);
        }
    }
    else if (name == QStringLiteral("INSPVA") && body.size() >= 11)
    {
        double v = 0;
        if (parseDouble(body.at(2), &v)) addSample(st, QStringLiteral("oem7.inspva.lat"), QStringLiteral("deg"), t, v);
        if (parseDouble(body.at(3), &v)) addSample(st, QStringLiteral("oem7.inspva.lon"), QStringLiteral("deg"), t, v);
        if (parseDouble(body.at(4), &v)) addSample(st, QStringLiteral("oem7.inspva.height"), QStringLiteral("m"), t, v);
        if (parseDouble(body.at(9), &v)) addSample(st, QStringLiteral("oem7.inspva.roll"), QStringLiteral("deg"), t, v);
        if (parseDouble(body.at(10), &v)) addSample(st, QStringLiteral("oem7.inspva.pitch"), QStringLiteral("deg"), t, v);
        if (body.size() > 11 && parseDouble(body.at(11), &v)) addSample(st, QStringLiteral("oem7.inspva.azimuth"), QStringLiteral("deg"), t, v);
    }
    else if (name == QStringLiteral("BESTATT") && body.size() >= 6)
    {
        double v = 0;
        if (parseDouble(body.at(2), &v)) addSample(st, QStringLiteral("oem7.bestatt.roll"), QStringLiteral("deg"), t, v);
        if (parseDouble(body.at(3), &v)) addSample(st, QStringLiteral("oem7.bestatt.pitch"), QStringLiteral("deg"), t, v);
        if (parseDouble(body.at(4), &v)) addSample(st, QStringLiteral("oem7.bestatt.yaw"), QStringLiteral("deg"), t, v);
    }
    else if (name == QStringLiteral("BESTVEL") && body.size() >= 4)
    {
        double v = 0;
        if (parseDouble(body.at(2), &v)) addSample(st, QStringLiteral("oem7.bestvel.hor_speed"), QStringLiteral("m/s"), t, v);
        if (parseDouble(body.at(3), &v)) addSample(st, QStringLiteral("oem7.bestvel.ver_speed"), QStringLiteral("m/s"), t, v);
    }
    else if (st->sink.event && st->sink.ctx)
    {
        const QByteArray payload = QJsonDocument(QJsonObject{{QStringLiteral("log"), name},
                                                             {QStringLiteral("line"), line}})
                                       .toJson(QJsonDocument::Compact);
        st->sink.event(st->sink.ctx, "oem7.raw", payload.constData());
        ++st->events;
    }
}

void parseNmeaLine(OemState *st, const QString &line)
{
    const int star = line.indexOf(QLatin1Char('*'));
    const QString core = star >= 0 ? line.left(star) : line;
    const QStringList f = core.split(QLatin1Char(','));
    if (f.size() < 3)
        return;
    const double t = st->clock.elapsed() / 1000.0;
    if (f.at(0).endsWith(QStringLiteral("GGA")) && f.size() >= 10)
    {
        double lat = 0, lon = 0, alt = 0;
        bool ok = false;
        lat = f.at(2).toDouble(&ok);
        if (ok && f.at(3) == QStringLiteral("S")) lat = -lat;
        lon = f.at(4).toDouble(&ok);
        if (ok && f.at(5) == QStringLiteral("W")) lon = -lon;
        if (lat != 0) addSample(st, QStringLiteral("oem7.nmea.lat"), QStringLiteral("deg"), t, lat);
        if (lon != 0) addSample(st, QStringLiteral("oem7.nmea.lon"), QStringLiteral("deg"), t, lon);
        if (parseDouble(f.at(9), &alt)) addSample(st, QStringLiteral("oem7.nmea.alt"), QStringLiteral("m"), t, alt);
        if (parseDouble(f.at(7), &alt)) addSample(st, QStringLiteral("oem7.nmea.sats"), QStringLiteral("count"), t, alt);
    }
}

void parseLines(OemState *st)
{
    while (true)
    {
        const int nl = st->rx.indexOf('\n');
        if (nl < 0)
            break;
        QByteArray raw = st->rx.left(nl);
        st->rx.remove(0, nl + 1);
        QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty())
            continue;
        ++st->lines;
        if (line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('<')))
            parseNovatelLine(st, line);
        else if (line.startsWith(QLatin1Char('$')))
            parseNmeaLine(st, line);
        else if (st->sink.event)
        {
            const QByteArray payload = QJsonDocument(QJsonObject{{QStringLiteral("line"), line}})
                                           .toJson(QJsonDocument::Compact);
            st->sink.event(st->sink.ctx, "oem7.raw", payload.constData());
            ++st->events;
        }
    }
    if (st->rx.size() > 1024 * 1024)
        st->rx.clear();
}

void loadDatabase(OemState *st, const QString &path)
{
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &pe);
    if (!doc.isObject())
        return;
    const QJsonObject root = doc.object();
    for (const QJsonValue &v : root.value(QStringLiteral("messages")).toArray())
    {
        const QJsonObject msg = v.toObject();
        const QString name = msg.value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            st->defs.insert(name, msg);
    }
    for (const QJsonValue &v : root.value(QStringLiteral("enums")).toArray())
    {
        const QJsonObject e = v.toObject();
        const QString id = e.value(QStringLiteral("_id")).toString();
        QHash<QString, int> values;
        for (const QJsonValue &ev : e.value(QStringLiteral("enumerators")).toArray())
        {
            const QJsonObject item = ev.toObject();
            values.insert(item.value(QStringLiteral("name")).toString(),
                          item.value(QStringLiteral("value")).toInt());
        }
        st->enums.insert(id, values);
    }
    st->dbLoaded = !st->defs.isEmpty();
    st->dbPath = path;
}

QString findDatabase(const QString &configured)
{
    QStringList candidates;
    if (!configured.isEmpty())
        candidates << configured;
    const QString env = qEnvironmentVariable("PLOTSCOPE_OEM7_DATABASE");
    if (!env.isEmpty())
        candidates << env;
    const QString appDir = QCoreApplication::applicationDirPath();
    candidates << appDir + QStringLiteral("/oem7_database.json")
               << appDir + QStringLiteral("/../oem7_database.json")
               << appDir + QStringLiteral("/../../oem7_database.json")
               << QDir::currentPath() + QStringLiteral("/oem7_database.json");
    for (const QString &path : candidates)
        if (QFile::exists(path))
            return path;
    return {};
}

int32_t openSource(const char *config, const PsSourceSink *sink, void **handle,
                   char *err, uint32_t errCap)
{
    if (!handle || !sink || !sink->channel_batch)
    {
        if (err && errCap)
            std::snprintf(err, errCap, "sink 不完整");
        return 1;
    }
    QJsonObject cfg;
    if (config && config[0])
    {
        const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(config));
        if (!doc.isObject())
        {
            if (err && errCap)
                std::snprintf(err, errCap, "config 不是 JSON 对象");
            return 2;
        }
        cfg = doc.object();
    }
    auto *st = new OemState;
    st->sink = *sink;
    loadDatabase(st, findDatabase(cfg.value(QStringLiteral("database")).toString()));
    st->socket = std::make_unique<QTcpSocket>();
    const QString host = cfg.value(QStringLiteral("host")).toString(QStringLiteral("192.168.1.20"));
    const quint16 port = static_cast<quint16>(qBound(1, cfg.value(QStringLiteral("port")).toInt(3001), 65535));
    const int timeout = qBound(100, cfg.value(QStringLiteral("timeout_ms")).toInt(2000), 10000);
    st->socket->connectToHost(host, port);
    if (!st->socket->waitForConnected(timeout))
    {
        if (err && errCap)
            std::snprintf(err, errCap, "连接 %s:%u 失败：%s", host.toUtf8().constData(),
                          static_cast<unsigned>(port), st->socket->errorString().toUtf8().constData());
        delete st;
        return 3;
    }
    st->clock.start();
    *handle = st;
    return 0;
}

int32_t pollSource(void *handle, int32_t timeoutMs, char *err, uint32_t errCap)
{
    Q_UNUSED(err);
    Q_UNUSED(errCap);
    auto *st = static_cast<OemState *>(handle);
    if (!st || !st->socket)
        return 1;
    if (timeoutMs > 0)
        st->socket->waitForReadyRead(qMin(timeoutMs, 50));
    st->rx.append(st->socket->readAll());
    parseLines(st);
    flush(st);
    return 0;
}

int32_t closeSource(void *handle)
{
    auto *st = static_cast<OemState *>(handle);
    if (st && st->socket)
        st->socket->disconnectFromHost();
    delete st;
    return 0;
}

int32_t statusSource(void *handle, char *out, uint32_t cap)
{
    auto *st = static_cast<OemState *>(handle);
    if (!st)
        return writeJson("{\"state\":\"closed\"}", out, cap);
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  R"({"state":"%s","host":"%s","port":%u,"lines":%llu,"logs":%llu,"events":%llu,"sow":%.3f,"db":%u})",
                  st->socket->state() == QAbstractSocket::ConnectedState ? "connected" : "disconnected",
                  st->socket->peerName().toUtf8().constData(), st->socket->peerPort(),
                  static_cast<unsigned long long>(st->lines), static_cast<unsigned long long>(st->logs),
                  static_cast<unsigned long long>(st->events), st->last_sow,
                  static_cast<unsigned>(st->dbLoaded));
    return writeJson(buf, out, cap);
}

int32_t schema(char *out, uint32_t cap) { return writeJson(kSchema, out, cap); }
int32_t defaults(char *out, uint32_t cap) { return writeJson(kDefaults, out, cap); }

} // namespace

extern "C" const PsPluginDescriptor *ps_plugin_entry_data_source_oem7()
{
    static const PsDataSourceVtable vtable = {
        PS_PLUGIN_ABI_VERSION,
        "data_source_oem7",
        "OEM7",
        "gnss",
        "tcp",
        1,
        {0, 0, 0, 0, 0, 0, 0},
        schema,
        defaults,
        openSource,
        pollSource,
        closeSource,
        statusSource,
    };
    static const PsPluginDescriptor desc = {
        PS_PLUGIN_ABI_VERSION,
        R"({"id":"data_source_oem7","name":"NovAtel OEM7","version":"1.0.0","family":"data_source"})",
        PS_FAMILY_DATA_SOURCE,
        PS_CAP_STREAM | PS_CAP_RECORD | PS_CAP_SCHEMA,
        nullptr,
        nullptr,
        &vtable,
    };
    return &desc;
}
