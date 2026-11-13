#include "ps_plugin_api.h"
#include "PsDataInputBase.hpp"
#include "PsCaptureFormat.h"

#include <QByteArray>
#include <QFile>
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
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#ifdef Q_OS_UNIX
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

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

const char *kSchema = R"JSON({
  "title":"NovAtel OEM7 接收机",
  "transport":{"supported":["tcp","serial","file"],"default":"tcp://192.168.1.20:3001"},
  "parser":{
    "label":"解析参数",
    "fields":[
      {"key":"raw","type":"bool","label":"保留未知报文（调试）","default":true},
      {"key":"database","type":"string","label":"OEM7 协议库（空=自动查找）","default":""}
    ]
  },
  "debug":{
    "title":"设备调试",
    "hint":"发送命令后，接收机响应显示在下方。",
    "commands":[
      {"label":"查询版本","command":"VERSION"},
      {"label":"BESTPOS 变化输出","command":"LOG BESTPOSA ONCHANGED"},
      {"label":"INSPVA 1 Hz","command":"LOG INSPVAA ONTIME 1"},
      {"label":"BESTATT 变化输出","command":"LOG BESTATTA ONCHANGED"},
      {"label":"BESTVEL 变化输出","command":"LOG BESTVELA ONCHANGED"},
      {"label":"关闭全部日志","command":"UNLOGALL"}
    ],
    "status":[
      {"key":"state","label":"状态"},
      {"key":"tx","label":"TX"},
      {"key":"lines","label":"行数"},
      {"key":"logs","label":"日志"},
      {"key":"frames","label":"报文"},
      {"key":"db","label":"协议库","type":"bool","true":"已加载","false":"-"}
    ]
  }
})JSON";
const char *kDefaults = R"({"transport":"tcp","host":"192.168.1.20","port":3001,"timeout_ms":2000,"serial_port":"/dev/ttyUSB0","baud":115200,"raw":true,"database":"","raw_path":"","filepath":""})";

class SerialPort
{
public:
    ~SerialPort() { close(); }

    bool open(const QString &path, int baud, QString &err)
    {
#ifdef Q_OS_UNIX
        close();
        fd_ = ::open(path.toUtf8().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ < 0)
        {
            err = QStringLiteral("打开串口失败：%1").arg(path);
            return false;
        }
        struct termios tio;
        if (tcgetattr(fd_, &tio) != 0)
        {
            err = QStringLiteral("读取串口属性失败：%1").arg(path);
            close();
            return false;
        }
        cfmakeraw(&tio);
        tio.c_cflag |= CLOCAL | CREAD;
        tio.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
        tio.c_cflag |= CS8;
        tio.c_iflag &= ~(IXON | IXOFF | IXANY);
        tio.c_lflag &= ~(ICANON | ECHO | ISIG);
        tio.c_cc[VMIN] = 0;
        tio.c_cc[VTIME] = 0;
        const speed_t speed = baudSpeed(baud);
        if (speed == 0)
        {
            err = QStringLiteral("不支持的波特率：%1").arg(baud);
            close();
            return false;
        }
        cfsetispeed(&tio, speed);
        cfsetospeed(&tio, speed);
        if (tcsetattr(fd_, TCSANOW, &tio) != 0)
        {
            err = QStringLiteral("配置串口失败：%1").arg(path);
            close();
            return false;
        }
        path_ = path;
        return true;
#else
        (void)path;
        (void)baud;
        err = QStringLiteral("串口模式当前仅支持 Linux/macOS");
        return false;
#endif
    }

    QByteArray readAll()
    {
        QByteArray out;
#ifdef Q_OS_UNIX
        if (fd_ < 0)
            return out;
        char buf[64 * 1024];
        while (true)
        {
            const ssize_t n = ::read(fd_, buf, sizeof(buf));
            if (n > 0)
            {
                out.append(buf, static_cast<int>(n));
                continue;
            }
            if (n < 0 && errno == EINTR)
                continue;
            break;
        }
#endif
        return out;
    }

    bool waitReadable(int timeoutMs)
    {
#ifdef Q_OS_UNIX
        if (fd_ < 0)
            return false;
        struct pollfd pfd{fd_, POLLIN, 0};
        const int rc = ::poll(&pfd, 1, timeoutMs);
        return rc > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR));
#else
        (void)timeoutMs;
        return false;
#endif
    }

    bool write(const QByteArray &data, QString &err)
    {
#ifdef Q_OS_UNIX
        if (fd_ < 0)
        {
            err = QStringLiteral("串口未打开");
            return false;
        }
        size_t off = 0;
        while (off < static_cast<size_t>(data.size()))
        {
            const ssize_t n = ::write(fd_, data.constData() + off,
                                      static_cast<size_t>(data.size()) - off);
            if (n > 0)
            {
                off += static_cast<size_t>(n);
                continue;
            }
            if (n < 0 && (errno == EINTR || errno == EAGAIN))
                continue;
            err = QStringLiteral("写串口失败");
            return false;
        }
        return true;
#else
        (void)data;
        err = QStringLiteral("串口模式当前仅支持 Linux/macOS");
        return false;
#endif
    }

    void close()
    {
#ifdef Q_OS_UNIX
        if (fd_ >= 0)
        {
            ::close(fd_);
            fd_ = -1;
        }
#endif
    }

    bool isOpen() const { return fd_ >= 0; }
    QString path() const { return path_; }

private:
#ifdef Q_OS_UNIX
    static speed_t baudSpeed(int baud)
    {
        switch (baud)
        {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default: return 0;
        }
    }
#endif
    int fd_ = -1;
    QString path_;
};

struct OemState
{
    PsSourceSink sink{};
    ps::capture::RawWriter rawWriter;
    ps::capture::RawReader rawReader;
    QFile legacyReplayFile;
    bool legacyReplay = false;
    bool replayMode = false;
    bool replayFinished = false;
    bool rawDropped = false;
    std::string rawError;
    std::unique_ptr<QTcpSocket> socket;
    std::unique_ptr<SerialPort> serial;
    QElapsedTimer clock;
    QByteArray rx;
    QHash<QString, QVector<double>> times;
    QHash<QString, QVector<double>> values;
    QHash<QString, QString> units;
    QStringList order;
    QHash<QString, QJsonObject> defs;
    QHash<int, QString> byId;
    QHash<QString, QHash<QString, int>> enums;
    bool dbLoaded = false;
    bool dbEmbedded = false;
    QString dbPath;
    std::string dbBytes;
    uint64_t lines = 0;
    uint64_t logs = 0;
    uint64_t frames = 0;
    uint64_t events = 0;
    uint64_t tx = 0;
    QString lastRx;
    double last_sow = 0.0;
    double replay_t = 0.0;
    bool replayStarted = false;
    int64_t replayBaseNs = 0;
};

int64_t nowNs()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
}

double nowSeconds(const OemState *st)
{
    return st->replayMode ? st->replay_t : st->clock.elapsed() / 1000.0;
}

void recordRaw(OemState *st, const QByteArray &in)
{
    if (in.isEmpty())
        return;
    if (st->sink.raw_batch)
    {
        st->sink.raw_batch(st->sink.ctx, "oem7", "oem7", nowNs(),
                           reinterpret_cast<const uint8_t *>(in.constData()),
                           static_cast<uint32_t>(in.size()));
        return;
    }
    if (!st->rawWriter.isOpen() || st->rawDropped)
        return;
    ps::capture::Record record;
    record.timestamp_ns = nowNs();
    record.topic = "oem7";
    record.encoding = "oem7";
    record.part_count = 1;
    record.part_lengths = {static_cast<uint32_t>(in.size())};
    record.payload.assign(reinterpret_cast<const uint8_t *>(in.constData()),
                          reinterpret_cast<const uint8_t *>(in.constData()) + in.size());
    std::string re;
    if (!st->rawWriter.write(record, re))
    {
        st->rawDropped = true;
        st->rawError = re;
    }
}

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

uint16_t rd16(const unsigned char *p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t rd32(const unsigned char *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
double rdDouble(const unsigned char *p)
{
    double v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
int fieldSize(const QString &dtype)
{
    if (dtype == QStringLiteral("DOUBLE")) return 8;
    if (dtype == QStringLiteral("FLOAT") || dtype == QStringLiteral("INT") ||
        dtype == QStringLiteral("UINT") || dtype == QStringLiteral("LONG") ||
        dtype == QStringLiteral("ULONG") || dtype == QStringLiteral("ENUM")) return 4;
    if (dtype == QStringLiteral("SHORT") || dtype == QStringLiteral("USHORT")) return 2;
    return 1;
}
double readScalar(const QString &dtype, const unsigned char *p)
{
    if (dtype == QStringLiteral("DOUBLE")) return rdDouble(p);
    if (dtype == QStringLiteral("FLOAT"))
    {
        float f = 0;
        std::memcpy(&f, p, sizeof(f));
        return f;
    }
    if (dtype == QStringLiteral("INT") || dtype == QStringLiteral("LONG") || dtype == QStringLiteral("ENUM"))
        return static_cast<int32_t>(rd32(p));
    if (dtype == QStringLiteral("UINT") || dtype == QStringLiteral("ULONG")) return rd32(p);
    if (dtype == QStringLiteral("SHORT")) return static_cast<int16_t>(rd16(p));
    if (dtype == QStringLiteral("USHORT")) return rd16(p);
    if (dtype == QStringLiteral("CHAR")) return static_cast<int8_t>(p[0]);
    return p[0];
}
QString unitOf(const QString &fname)
{
    const QString lower = fname.toLower();
    if (lower.contains(QStringLiteral("latitude")) || lower.contains(QStringLiteral("longitude")) ||
        lower.contains(QStringLiteral("azimuth")) || lower.contains(QStringLiteral("roll")) ||
        lower.contains(QStringLiteral("pitch")) || lower.contains(QStringLiteral("heading")))
        return QStringLiteral("deg");
    if (lower.contains(QStringLiteral("height")) || lower.contains(QStringLiteral("altitude")) ||
        lower.contains(QStringLiteral("undulation")))
        return QStringLiteral("m");
    if (lower.contains(QStringLiteral("velocity")) || lower.contains(QStringLiteral("speed")))
        return QStringLiteral("m/s");
    return {};
}

// OEM7 standard/short binary 头之后的字段按 database.json 的 dataType 长度顺序读取。
bool parseBinaryFrame(OemState *st, int headerLength, const unsigned char *hdr,
                      const unsigned char *body, int bodyLength, int msgId,
                      int msgDefCrc, double t)
{
    const QString name = st->byId.value(msgId);
    if (name.isEmpty())
        return false;
    const QJsonObject def = st->defs.value(name);
    if (def.isEmpty())
        return false;
    const QJsonObject fields = def.value(QStringLiteral("fields")).toObject();
    QJsonArray arr = fields.value(QString::number(msgDefCrc)).toArray();
    if (arr.isEmpty())
        arr = fields.value(def.value(QStringLiteral("latestMsgDefCrc")).toString()).toArray();
    if (arr.isEmpty())
        return false;
    int offset = 0;
    int emitted = 0;
    for (const QJsonValue &fv : arr)
    {
        const QJsonObject f = fv.toObject();
        const QString fname = f.value(QStringLiteral("name")).toString();
        const QString type = f.value(QStringLiteral("type")).toString();
        const QJsonObject dataType = f.value(QStringLiteral("dataType")).toObject();
        const QString dtype = dataType.value(QStringLiteral("name")).toString();
        const int elem = fieldSize(dtype);
        int count = 1;
        if (type == QStringLiteral("FIXED_LENGTH_ARRAY") || type == QStringLiteral("VARIABLE_LENGTH_ARRAY"))
            count = f.value(QStringLiteral("arrayLength")).toInt(1);
        if (type == QStringLiteral("VARIABLE_LENGTH_ARRAY") || count <= 0)
            break;
        const int total = elem * count;
        if (offset + total > bodyLength)
            break;
        if (!fname.isEmpty() && !fname.contains(QStringLiteral("reserved")) && count == 1 &&
            emitted < 64)
        {
            double value = readScalar(dtype, body + offset);
            if (std::isfinite(value))
            {
                addSample(st, QStringLiteral("oem7.%1.%2").arg(name.toLower(), fname),
                          unitOf(fname), t, value);
                ++emitted;
            }
        }
        offset += total;
    }
    Q_UNUSED(headerLength);
    Q_UNUSED(hdr);
    return emitted > 0;
}

// 从 TCP 字节流里切出 OEM7 binary frame；ASCII 行留给 parseLines。
void parseBinary(OemState *st)
{
    while (true)
    {
        const int sync = st->rx.indexOf(QByteArray("\xAA\x44", 2));
        if (sync < 0)
            return;
        if (sync > 0)
            st->rx.remove(0, sync);
        if (st->rx.size() < 4)
            return;
        const unsigned char *p = reinterpret_cast<const unsigned char *>(st->rx.constData());
        if (p[2] == 0x12)
        {
            if (p[3] != 28 || st->rx.size() < 28)
                return;
            const int bodyLen = rd16(p + 8);
            const int total = 28 + bodyLen + 4;
            if (st->rx.size() < total)
                return;
            const int msgId = rd16(p + 4);
            const int crc = rd16(p + 24);
            const double t = rd16(p + 14) * 604800.0 + rd32(p + 16) / 1000.0;
            ++st->frames;
            st->logs++;
            st->lastRx = QStringLiteral("#%1 (binary)").arg(st->byId.value(msgId));
            parseBinaryFrame(st, 28, p, p + 28, bodyLen, msgId, crc, t);
            st->rx.remove(0, total);
        }
        else if (p[2] == 0x13)
        {
            if (st->rx.size() < 12)
                return;
            const int bodyLen = p[3];
            const int total = 12 + bodyLen + 4;
            if (st->rx.size() < total)
                return;
            const int msgId = rd16(p + 4);
            const double t = rd16(p + 6) * 604800.0 + rd32(p + 8) / 1000.0;
            ++st->frames;
            st->logs++;
            st->lastRx = QStringLiteral("#%1 (short binary)").arg(st->byId.value(msgId));
            parseBinaryFrame(st, 12, p, p + 12, bodyLen, msgId, -1, t);
            st->rx.remove(0, total);
        }
        else
        {
            st->rx.remove(0, 1);
        }
    }
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
    const double t = ok ? sow : nowSeconds(st);
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
    const double t = nowSeconds(st);
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
    // 以 OEM7 binary sync 开头时不按行切，避免把帧体里的 0x0A 当换行。
    if (st->rx.startsWith(QByteArray("\xAA\x44", 2)))
        return;
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
        st->lastRx = line.left(240);
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

void loadDatabaseBytes(OemState *st, const std::string &bytes, const QString &label)
{
    if (bytes.empty())
        return;
    const QByteArray data(reinterpret_cast<const char *>(bytes.data()), static_cast<int>(bytes.size()));
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (!doc.isObject())
        return;
    st->defs.clear();
    st->byId.clear();
    st->enums.clear();
    const QJsonObject root = doc.object();
    for (const QJsonValue &v : root.value(QStringLiteral("messages")).toArray())
    {
        const QJsonObject msg = v.toObject();
        const QString name = msg.value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
        {
            st->defs.insert(name, msg);
            st->byId.insert(msg.value(QStringLiteral("messageID")).toInt(), name);
        }
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
    st->dbPath = label;
}

void loadDatabase(OemState *st, const QString &path)
{
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray data = file.readAll();
    st->dbBytes.assign(reinterpret_cast<const char *>(data.constData()),
                       static_cast<size_t>(data.size()));
    loadDatabaseBytes(st, st->dbBytes, path);
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

int32_t openSource(const char *config, const PsSourceSink *sink, const PsHostApi *, void **handle,
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
    const std::string cfgJson = (config && config[0]) ? config : "{}";
    QString replayPath = cfg.value(QStringLiteral("filepath")).toString();
    if (replayPath.isEmpty())
        replayPath = cfg.value(QStringLiteral("__replay_path")).toString();
    if (!replayPath.isEmpty())
    {
        const std::string path = replayPath.toStdString();
        if (ps::capture::isRawFile(path))
        {
            std::string re;
            if (!st->rawReader.open(path, re))
            {
                if (err && errCap)
                    std::snprintf(err, errCap, "打开回放文件失败：%s", re.c_str());
                delete st;
                return 3;
            }
            if (!st->rawReader.header().schema_blob.empty())
            {
                st->dbBytes = st->rawReader.header().schema_blob;
                st->dbEmbedded = true;
                loadDatabaseBytes(st, st->dbBytes, QStringLiteral("<embedded>"));
            }
            else
            {
                loadDatabase(st, findDatabase(cfg.value(QStringLiteral("database")).toString()));
            }
        }
        else
        {
            st->legacyReplayFile.setFileName(replayPath);
            if (!st->legacyReplayFile.open(QIODevice::ReadOnly))
            {
                if (err && errCap)
                    std::snprintf(err, errCap, "打开回放文件失败：%s", replayPath.toUtf8().constData());
                delete st;
                return 3;
            }
            st->legacyReplay = true;
            loadDatabase(st, findDatabase(cfg.value(QStringLiteral("database")).toString()));
        }
        st->replayMode = true;
        st->clock.start();
        *handle = st;
        return 0;
    }

    loadDatabase(st, findDatabase(cfg.value(QStringLiteral("database")).toString()));
    const QString rawPath = cfg.value(QStringLiteral("raw_path")).toString();
    if (!rawPath.isEmpty())
    {
        ps::capture::Header h;
        h.source_id = "data_source_oem7";
        h.plugin_id = "data_source_oem7";
        h.config_json = cfgJson;
        h.schema_blob = st->dbBytes;
        h.created_at_ns = nowNs();
        std::string re;
        if (!st->rawWriter.open(rawPath.toStdString(), h, re))
        {
            if (err && errCap)
                std::snprintf(err, errCap, "无法写原始流：%s", re.c_str());
            delete st;
            return 4;
        }
    }
    const QString transport = cfg.value(QStringLiteral("transport")).toString(QStringLiteral("tcp"));
    if (transport == QLatin1String("serial"))
    {
        st->serial = std::make_unique<SerialPort>();
        const QString port = cfg.value(QStringLiteral("serial_port"))
                                 .toString(QStringLiteral("/dev/ttyUSB0"));
        const int baud = qBound(1200, cfg.value(QStringLiteral("baud")).toInt(115200), 921600);
        QString serialErr;
        if (!st->serial->open(port, baud, serialErr))
        {
            if (err && errCap)
                std::snprintf(err, errCap, "%s", serialErr.toUtf8().constData());
            delete st;
            return 3;
        }
        st->clock.start();
        *handle = st;
        return 0;
    }

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
    auto *st = static_cast<OemState *>(handle);
    if (!st)
        return 1;
    if (st->replayMode)
    {
        if (st->replayFinished)
            return 0;
        if (st->legacyReplay)
        {
            const QByteArray chunk = st->legacyReplayFile.read(256 * 1024);
            if (chunk.isEmpty())
            {
                parseBinary(st);
                parseLines(st);
                flush(st);
                st->replayFinished = true;
                return 0;
            }
            st->rx.append(chunk);
            parseBinary(st);
            parseLines(st);
            flush(st);
            return 0;
        }

        size_t budget = 256 * 1024;
        while (budget > 0)
        {
            ps::capture::Record record;
            std::string re;
            if (!st->rawReader.next(record, re))
            {
                if (!re.empty())
                {
                    if (err && errCap)
                        std::snprintf(err, errCap, "读取 raw 失败：%s", re.c_str());
                    return 1;
                }
                parseBinary(st);
                parseLines(st);
                flush(st);
                st->replayFinished = true;
                return 0;
            }
            if (record.encoding != "oem7")
            {
                if (err && errCap)
                    std::snprintf(err, errCap, "raw 记录 encoding=%s，不是 oem7",
                                  record.encoding.c_str());
                return 1;
            }
            if (!st->replayStarted)
            {
                st->replayBaseNs = record.timestamp_ns;
                st->replayStarted = true;
            }
            st->replay_t = static_cast<double>(record.timestamp_ns - st->replayBaseNs) / 1e9;
            if (!record.payload.empty())
                st->rx.append(reinterpret_cast<const char *>(record.payload.data()),
                              static_cast<int>(record.payload.size()));
            parseBinary(st);
            parseLines(st);
            flush(st);
            budget = record.payload.size() >= budget ? 0 : budget - record.payload.size();
        }
        return 0;
    }
    QByteArray in;
    if (st->serial)
    {
        if (timeoutMs > 0)
            st->serial->waitReadable(qMin(timeoutMs, 10));
        in = st->serial->readAll();
    }
    else
    {
        if (!st->socket)
            return 1;
        if (timeoutMs > 0)
            st->socket->waitForReadyRead(qMin(timeoutMs, 50));
        in = st->socket->readAll();
    }
    recordRaw(st, in);
    st->rx.append(in);
    parseBinary(st);
    parseLines(st);
    flush(st);
    return 0;
}

int32_t closeSource(void *handle)
{
    auto *st = static_cast<OemState *>(handle);
    if (st)
    {
        if (st->socket)
            st->socket->disconnectFromHost();
        if (st->serial)
            st->serial->close();
        if (st->rawWriter.isOpen())
        {
            std::string re;
            st->rawWriter.close(re);
        }
        if (st->legacyReplayFile.isOpen())
            st->legacyReplayFile.close();
    }
    delete st;
    return 0;
}

int32_t sendCommand(void *handle, const char *command, char *out, uint32_t cap)
{
    auto *st = static_cast<OemState *>(handle);
    const bool tcp = st && st->socket && st->socket->state() == QAbstractSocket::ConnectedState;
    const bool serial = st && st->serial && st->serial->isOpen();
    if (!tcp && !serial)
        return writeJson(R"({"ok":false,"error":"未连接"})", out, cap);
    QString cmd = QString::fromUtf8(command ? command : "").trimmed();
    if (cmd.isEmpty())
        return writeJson(R"({"ok":false,"error":"空命令"})", out, cap);
    if (!cmd.startsWith(QLatin1Char('$')) && !cmd.startsWith(QLatin1Char('%')))
        cmd.prepend(QLatin1Char('$'));
    QByteArray tx = cmd.toLatin1();
    if (!tx.endsWith("\r\n"))
        tx.append("\r\n");
    if (serial)
    {
        QString we;
        if (!st->serial->write(tx, we))
        {
            const QJsonObject res{{QStringLiteral("ok"), false},
                                  {QStringLiteral("error"), we}};
            return writeJson(QJsonDocument(res).toJson(QJsonDocument::Compact).constData(), out, cap);
        }
    }
    else
    {
        st->socket->write(tx);
        st->socket->flush();
        st->socket->waitForBytesWritten(100);
    }
    ++st->tx;
    const QJsonObject res{{QStringLiteral("ok"), true},
                          {QStringLiteral("command"), cmd},
                          {QStringLiteral("tx"), static_cast<double>(st->tx)}};
    return writeJson(QJsonDocument(res).toJson(QJsonDocument::Compact).constData(), out, cap);
}

int32_t statusSource(void *handle, char *out, uint32_t cap)
{
    auto *st = static_cast<OemState *>(handle);
    if (!st)
        return writeJson("{\"state\":\"closed\"}", out, cap);
    const bool serial = st->serial && st->serial->isOpen();
    const QString state = st->replayMode
                              ? (st->replayFinished ? "replay_done" : "replaying")
                              : (serial ? "connected"
                                        : (st->socket && st->socket->state() == QAbstractSocket::ConnectedState
                                               ? "connected" : "disconnected"));
    QJsonObject res{
        {QStringLiteral("state"), state},
        {QStringLiteral("source"), st->replayMode ? "file" : (serial ? "serial" : "tcp")},
        {QStringLiteral("transport"), serial ? "serial" : "tcp"},
        {QStringLiteral("serial_port"), serial ? st->serial->path() : QString()},
        {QStringLiteral("host"), st->socket ? st->socket->peerName() : QString()},
        {QStringLiteral("port"), st->socket ? st->socket->peerPort() : 0},
        {QStringLiteral("lines"), static_cast<double>(st->lines)},
        {QStringLiteral("logs"), static_cast<double>(st->logs)},
        {QStringLiteral("frames"), static_cast<double>(st->frames)},
        {QStringLiteral("events"), static_cast<double>(st->events)},
        {QStringLiteral("tx"), static_cast<double>(st->tx)},
        {QStringLiteral("sow"), st->last_sow},
        {QStringLiteral("db"), st->dbLoaded},
        {QStringLiteral("db_source"), st->dbEmbedded ? QStringLiteral("embedded") : st->dbPath},
        {QStringLiteral("format"), st->replayMode
                                       ? (st->legacyReplay ? QStringLiteral("legacy")
                                                           : QStringLiteral("PSRAW1"))
                                       : QStringLiteral("PSRAW1")},
        {QStringLiteral("last_rx"), st->lastRx},
    };
    if (!st->rawError.empty())
        res.insert(QStringLiteral("raw_error"), QString::fromStdString(st->rawError));
    const QByteArray json = QJsonDocument(res).toJson(QJsonDocument::Compact);
    return writeJson(json.constData(), out, cap);
}

int32_t schema(char *out, uint32_t cap) { return writeJson(kSchema, out, cap); }
int32_t defaults(char *out, uint32_t cap) { return writeJson(kDefaults, out, cap); }

} // namespace

class OemInput final : public ps::DataInputBase
{
public:
    uint64_t capabilities() const override
    {
        return PS_INPUT_STREAM | PS_INPUT_FILE | PS_INPUT_DIRECT | PS_INPUT_REPLAY |
               PS_INPUT_RECORD | PS_INPUT_COMMAND | PS_INPUT_DIALOG;
    }

    bool loadConfig(std::string_view json, PsError *err) override
    {
        config_.assign(json.data() ? json.data() : "", json.size());
        if (!config_.empty())
        {
            const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(config_.c_str()));
            if (!doc.isObject())
            {
                ps::fillError(err, 2, "config 不是 JSON 对象");
                return false;
            }
        }
        return true;
    }

    bool saveConfig(std::string &out, PsError *err) override
    {
        (void)err;
        out = config_.empty() ? "{}" : config_;
        return true;
    }

    PsInputDialog dialog() const override
    {
        return PsInputDialog{nullptr, kSchema, kDefaults};
    }

    bool start(PsError *err) override
    {
        if (handle_)
            return true;
        const PsSourceSink *s = sink();
        if (!s || !s->channel_batch)
        {
            ps::fillError(err, 1, "宿主 sink 未绑定");
            return false;
        }
        char e[256] = {0};
        if (openSource(config_.c_str(), s, host(), &handle_, e, sizeof(e)) != 0)
        {
            ps::fillError(err, 3, e[0] ? e : "打开 OEM7 失败");
            return false;
        }
        state_ = PS_INPUT_RUNNING;
        return true;
    }

    bool poll(PsError *err) override
    {
        if (!handle_)
            return true;
        char e[256] = {0};
        if (pollSource(handle_, 0, e, sizeof(e)) != 0)
        {
            ps::fillError(err, 4, e[0] ? e : "OEM7 poll 失败");
            return false;
        }
        char st[768] = {0};
        if (statusSource(handle_, st, sizeof(st)) > 0 && std::strstr(st, "replay_done"))
            state_ = PS_INPUT_DONE;
        return true;
    }

    void stop() override
    {
        if (handle_)
        {
            closeSource(handle_);
            handle_ = nullptr;
        }
        state_ = PS_INPUT_IDLE;
    }

    PsInputState currentState() const override { return state_; }

    bool statusJson(std::string &out) override
    {
        char b[1024] = {0};
        const int n = statusSource(handle_, b, sizeof(b));
        if (n < 0)
            return false;
        out.assign(b, static_cast<size_t>(n));
        return true;
    }

    bool sendCommand(std::string_view command, std::string &out, PsError *err) override
    {
        if (!handle_)
        {
            ps::fillError(err, 1, "未启动");
            return false;
        }
        char buf[1024] = {0};
        const int n = ::sendCommand(handle_, std::string(command).c_str(), buf, sizeof(buf));
        if (n < 0)
        {
            ps::fillError(err, 2, "命令发送失败");
            return false;
        }
        out.assign(buf, static_cast<size_t>(n));
        return true;
    }

private:
    std::string config_;
    void *handle_ = nullptr;
    PsInputState state_ = PS_INPUT_IDLE;
};

namespace {
const PsDataInputVtable *oemVtable()
{
    static const PsDataInputVtable vt = ps::VtableBuilder::make<OemInput>(R"({"id":"data_source_oem7","name":"NovAtel OEM7","version":"1.0.0","kind":"gnss","transport":"tcp","family":"data_input","file_extensions":[".bin",".gps",".log",".dat",".psraw",".raw"]})");
    return &vt;
}
} // namespace


extern "C" const PsPluginDescriptor *ps_plugin_entry_data_source_oem7()
{
    static const PsPluginDescriptor desc = {
        PS_PLUGIN_ABI_VERSION,
        R"({"id":"data_source_oem7","name":"NovAtel OEM7","version":"1.0.0","kind":"gnss","transport":"tcp","family":"data_input","file_extensions":[".bin",".gps",".log",".dat",".psraw",".raw"]})",
        PS_FAMILY_DATA_INPUT,
        0,
        nullptr,
        nullptr,
        nullptr,
        oemVtable(),
    };
    return &desc;
}
