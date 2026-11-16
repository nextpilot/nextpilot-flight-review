#include "ps_extension_api.h"
#include "LegacyFileInput.hpp"
#include "PsLoaderCommon.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

class Cdr
{
public:
    Cdr(const uint8_t *d, size_t n) : m_d(d), m_n(n)
    {
        if (n >= 4)
            m_le = (d[0] & 1u) == 0;
        m_pos = 4;
    }
    bool ok() const { return m_ok; }
    uint32_t pos() const { return m_pos; }
    void align(uint32_t a)
    {
        const uint32_t rel = m_pos - 4;
        const uint32_t pad = (a - rel % a) % a;
        m_pos += pad;
    }
    uint8_t u8()
    {
        if (m_pos + 1 > m_n) { m_ok = false; return 0; }
        return m_d[m_pos++];
    }
    uint32_t u32()
    {
        align(4);
        if (m_pos + 4 > m_n) { m_ok = false; return 0; }
        uint32_t v = 0;
        if (m_le)
            std::memcpy(&v, m_d + m_pos, 4);
        else
            for (int i = 0; i < 4; ++i) v = (v << 8) | m_d[m_pos + i];
        m_pos += 4;
        return v;
    }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    double f64()
    {
        align(8);
        if (m_pos + 8 > m_n) { m_ok = false; return 0; }
        uint64_t bits = 0;
        if (m_le)
            std::memcpy(&bits, m_d + m_pos, 8);
        else
            for (int i = 0; i < 8; ++i) bits = (bits << 8) | m_d[m_pos + i];
        m_pos += 8;
        double v;
        std::memcpy(&v, &bits, 8);
        return v;
    }
    float f32()
    {
        align(4);
        if (m_pos + 4 > m_n) { m_ok = false; return 0; }
        uint32_t bits = 0;
        if (m_le)
            std::memcpy(&bits, m_d + m_pos, 4);
        else
            for (int i = 0; i < 4; ++i) bits = (bits << 8) | m_d[m_pos + i];
        m_pos += 4;
        float v;
        std::memcpy(&v, &bits, 4);
        return v;
    }
    std::string str()
    {
        const uint32_t len = u32();
        if (len > 4096 || m_pos + len > m_n) { m_ok = false; return {}; }
        std::string s(reinterpret_cast<const char *>(m_d + m_pos), len);
        m_pos += len;
        if (!s.empty() && s.back() == '\0')
            s.pop_back();
        return s;
    }

private:
    const uint8_t *m_d;
    size_t m_n;
    uint32_t m_pos = 0;
    bool m_le = true;
    bool m_ok = true;
};

struct Sink
{
    std::map<std::string, std::vector<std::pair<double, double>>> acc;
    void put(const std::string &name, double t, double v)
    {
        acc[name].push_back({t, v});
    }
};

void header(Cdr &c)
{
    c.u32(); // sec
    c.u32(); // nanosec
    c.str(); // frame_id
}

void vec3(Cdr &c, Sink &s, const std::string &p, double t, const char *x, const char *y, const char *z)
{
    s.put(p + x, t, c.f64());
    s.put(p + y, t, c.f64());
    s.put(p + z, t, c.f64());
}

void quat(Cdr &c, Sink &s, const std::string &p, double t)
{
    s.put(p + "x", t, c.f64());
    s.put(p + "y", t, c.f64());
    s.put(p + "z", t, c.f64());
    s.put(p + "w", t, c.f64());
}

void decode(const std::string &type, const uint8_t *data, uint32_t size, double t, Sink &s,
            const std::string &prefix)
{
    Cdr c(data, size);
    if (type == "std_msgs/msg/Float64")
        s.put(prefix + "data", t, c.f64());
    else if (type == "std_msgs/msg/Float32")
        s.put(prefix + "data", t, c.f32());
    else if (type == "std_msgs/msg/Int32")
        s.put(prefix + "data", t, c.i32());
    else if (type == "std_msgs/msg/Int64")
        s.put(prefix + "data", t, static_cast<double>(static_cast<int64_t>(c.u32()) |
                                                       (static_cast<int64_t>(c.u32()) << 32)));
    else if (type == "std_msgs/msg/Bool")
        s.put(prefix + "data", t, c.u8() ? 1.0 : 0.0);
    else if (type == "std_msgs/msg/String")
        s.put(prefix + "len", t, static_cast<double>(c.str().size()));
    else if (type == "sensor_msgs/msg/Imu")
    {
        header(c);
        quat(c, s, prefix + "orientation.", t);
        for (int i = 0; i < 9; ++i) c.f64();
        vec3(c, s, prefix + "angular_velocity.", t, "x", "y", "z");
        for (int i = 0; i < 9; ++i) c.f64();
        vec3(c, s, prefix + "linear_acceleration.", t, "x", "y", "z");
        for (int i = 0; i < 9; ++i) c.f64();
    }
    else if (type == "geometry_msgs/msg/Twist")
    {
        vec3(c, s, prefix + "linear.", t, "x", "y", "z");
        vec3(c, s, prefix + "angular.", t, "x", "y", "z");
    }
    else if (type == "geometry_msgs/msg/PoseStamped")
    {
        header(c);
        vec3(c, s, prefix + "position.", t, "x", "y", "z");
        quat(c, s, prefix + "orientation.", t);
    }
    else if (type == "nav_msgs/msg/Odometry")
    {
        header(c);
        c.str();
        vec3(c, s, prefix + "position.", t, "x", "y", "z");
        quat(c, s, prefix + "orientation.", t);
        for (int i = 0; i < 36; ++i) c.f64();
        vec3(c, s, prefix + "linear.", t, "x", "y", "z");
        vec3(c, s, prefix + "angular.", t, "x", "y", "z");
        for (int i = 0; i < 36; ++i) c.f64();
    }
    else if (type == "sensor_msgs/msg/NavSatFix")
    {
        header(c);
        c.u8();
        c.u8();
        s.put(prefix + "latitude", t, c.f64());
        s.put(prefix + "longitude", t, c.f64());
        s.put(prefix + "altitude", t, c.f64());
    }
    else if (type == "std_msgs/msg/Header")
    {
        header(c);
    }
    else
    {
        // 未识别类型：只给原始长度，避免静默造数据。
        s.put(prefix + "payload_bytes", t, static_cast<double>(size));
    }
}

int32_t loadRosbag2(const char *path, const char *optsJson, const PsHostApi *host, void **dataset,
                    char *err, uint32_t errCap)
{
    QString dbPath = QString::fromUtf8(path);
    if (QFileInfo(dbPath).isDir())
    {
        const QString p = dbPath + QStringLiteral("/rosbag2.db3");
        if (!QFileInfo::exists(p))
        {
            psWriteErr(err, errCap, "目录内没有 rosbag2.db3");
            return 1;
        }
        dbPath = p;
    }
    QJsonObject opts;
    if (optsJson && *optsJson)
        opts = QJsonDocument::fromJson(QByteArray::fromRawData(optsJson, int(std::strlen(optsJson)))).object();
    std::vector<std::string> wanted;
    for (const QJsonValue &v : opts.value(QStringLiteral("topics")).toString().split(QLatin1Char(',')))
    {
        const QString s = v.toString().trimmed();
        if (!s.isEmpty())
            wanted.push_back(s.toStdString());
    }

    const QString conn = QStringLiteral("rosbag2_%1").arg(reinterpret_cast<quintptr>(dataset));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    db.setDatabaseName(dbPath);
    if (!db.open())
    {
        psWriteErr(err, errCap, "无法打开 rosbag2 数据库");
        QSqlDatabase::removeDatabase(conn);
        return 1;
    }

    std::map<int, std::pair<std::string, std::string>> topics;
    QSqlQuery tq(db);
    if (tq.exec(QStringLiteral("SELECT id,name,type FROM topics")))
        while (tq.next())
            topics[tq.value(0).toInt()] = {tq.value(1).toString().toStdString(),
                                           tq.value(2).toString().toStdString()};
    if (topics.empty())
    {
        psWriteErr(err, errCap, "topics 表为空");
        db.close();
        QSqlDatabase::removeDatabase(conn);
        return 2;
    }

    psLoaderSetHost(host);
    qint64 msgCount = 0;
    {
        QSqlQuery cq(db);
        if (cq.exec(QStringLiteral("SELECT COUNT(*) FROM messages")) && cq.next())
            msgCount = cq.value(0).toLongLong();
    }
    QSqlQuery mq(db);
    mq.setForwardOnly(true);
    if (!mq.exec(QStringLiteral("SELECT topic_id,timestamp,data FROM messages ORDER BY timestamp")))
    {
        psWriteErr(err, errCap, mq.lastError().text().toStdString());
        db.close();
        QSqlDatabase::removeDatabase(conn);
        return 2;
    }
    double t0 = -1;
    Sink sink;
    qint64 readCount = 0;
    while (mq.next())
    {
        ++readCount;
        if (msgCount > 0)
            psLoaderProgress(10 + static_cast<uint64_t>(readCount) * 85 / static_cast<uint64_t>(msgCount), 100);
        const int tid = mq.value(0).toInt();
        auto it = topics.find(tid);
        if (it == topics.end())
            continue;
        if (!wanted.empty())
        {
            bool hit = false;
            for (const auto &w : wanted)
                if (it->second.first == w) { hit = true; break; }
            if (!hit)
                continue;
        }
        const qint64 ns = mq.value(1).toLongLong();
        const double t = static_cast<double>(ns) / 1e9;
        if (t0 < 0)
            t0 = t;
        const QByteArray blob = mq.value(2).toByteArray();
        if (blob.isEmpty())
            continue;
        std::string prefix = it->second.first;
        if (!prefix.empty() && prefix.front() == '/')
            prefix.erase(prefix.begin());
        std::replace(prefix.begin(), prefix.end(), '/', '.');
        prefix += ".";
        decode(it->second.second, reinterpret_cast<const uint8_t *>(blob.constData()),
               static_cast<uint32_t>(blob.size()), t - t0, sink, prefix);
    }
    tq.finish();
    mq.finish();
    tq = QSqlQuery();
    mq = QSqlQuery();
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(conn);

    if (sink.acc.empty())
    {
        psWriteErr(err, errCap, "没有可解码的消息");
        return 3;
    }
    std::vector<PsChannelData> data;
    for (auto &kv : sink.acc)
    {
        PsChannelData ch;
        ch.name = "ros." + kv.first;
        for (const auto &p : kv.second)
        {
            ch.t.push_back(p.first);
            ch.v.push_back(p.second);
        }
        data.push_back(std::move(ch));
    }
    std::string derr;
    PsDataset *ds = psBuildDataset(data, derr, host);
    if (!ds)
    {
        psWriteErr(err, errCap, derr);
        return 4;
    }
    *dataset = ds;
    return 0;
}

int32_t schema(char *out, uint32_t cap)
{
    const char *json = R"json([{"key":"topics","label":"主题过滤（逗号分隔，空=全部）","type":"string","placeholder":"/imu,/odom"}])json";
    if (!out || cap < std::strlen(json) + 1)
        return 1;
    std::strcpy(out, json);
    return 0;
}

const PsLoaderVtable kRosbag2Loader = {
    PS_EXTENSION_ABI_VERSION,
    "data_loader_rosbag2",
    "ROS2 bag (.db3)",
    "ROS2 bag (*.db3);;全部 (*)",
    ".db3",
    "ROS2 bag",
    0,
    1,
    {0, 0, 0, 0, 0, 0},
    schema,
    nullptr,
    nullptr,
    nullptr,
    loadRosbag2,
};

const PsDataSourceVtable *rosbag2InputVtable()
{
    static const PsDataSourceVtable vt = ps::VtableBuilder::make<LegacyFileInput<&kRosbag2Loader>>(R"({"id":"data_loader_rosbag2","name":"ROS2 bag","version":"1.0.0","sdk":"1.0","kind":"file"})");
    return &vt;
}

const PsExtensionDescriptor kRosbag2Extension = {
    PS_EXTENSION_ABI_VERSION,
    R"({"id":"data_loader_rosbag2","name":"ROS2 bag","version":"1.0.0","sdk":"1.0","family":"data_source","kind":"file","entry":"ps_extension_entry_data_loader_rosbag2"})",
    PS_FAMILY_DATA_SOURCE,
    PS_CAP_FILE | PS_CAP_CACHE,
    &kRosbag2Loader,
    nullptr,
    nullptr,
    rosbag2InputVtable(),
};

} // namespace

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_rosbag2(void)
{
    return &kRosbag2Extension;
}
