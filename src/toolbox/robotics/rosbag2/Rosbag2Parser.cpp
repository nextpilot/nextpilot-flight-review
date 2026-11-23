#include "Rosbag2Parser.h"

#include "ParserHub.h"

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

// getpid 在 Windows 上走 <process.h> 的 _getpid。
#if defined(_WIN32)
#include <process.h>
#define PS_GETPID() static_cast<long>(_getpid())
#else
#include <unistd.h>
#define PS_GETPID() static_cast<long>(::getpid())
#endif

namespace nextpilot::plotscope::ext::rosbag2
{
namespace
{

// ---- CDR（ROS2 序列化）----
//
// 头 4 字节 = 表示标识：bit0=0 小端。之后按字段对齐到 1/2/4/8。
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
    void align(uint32_t a)
    {
        const uint32_t rel = m_pos - 4;
        m_pos += (a - rel % a) % a;
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
    uint64_t u64()
    {
        align(8);
        if (m_pos + 8 > m_n) { m_ok = false; return 0; }
        uint64_t v = 0;
        if (m_le)
            std::memcpy(&v, m_d + m_pos, 8);
        else
            for (int i = 0; i < 8; ++i) v = (v << 8) | m_d[m_pos + i];
        m_pos += 8;
        return v;
    }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    int64_t i64() { return static_cast<int64_t>(u64()); }
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

using Fields = std::vector<std::pair<std::string, double>>;

void put(Fields &f, const std::string &name, double v) { f.emplace_back(name, v); }

void header(Cdr &c)
{
    c.u32(); // sec
    c.u32(); // nanosec
    c.str(); // frame_id
}

// 9 个 double 的协方差矩阵。
void skipCov9(Cdr &c)
{
    for (int i = 0; i < 9; ++i)
        c.f64();
}

void vec3(Cdr &c, Fields &f, const std::string &p, const char *x, const char *y, const char *z)
{
    put(f, p + x, c.f64());
    put(f, p + y, c.f64());
    put(f, p + z, c.f64());
}

void quat(Cdr &c, Fields &f, const std::string &p)
{
    put(f, p + "x", c.f64());
    put(f, p + "y", c.f64());
    put(f, p + "z", c.f64());
    put(f, p + "w", c.f64());
}

void decode(const std::string &type, const uint8_t *data, uint32_t size, Fields &out)
{
    Cdr c(data, size);
    if (type == "std_msgs/msg/Float64")
        put(out, "data", c.f64());
    else if (type == "std_msgs/msg/Float32")
        put(out, "data", c.f32());
    else if (type == "std_msgs/msg/Int32")
        put(out, "data", c.i32());
    else if (type == "std_msgs/msg/Int64")
        put(out, "data", static_cast<double>(c.i64()));
    else if (type == "std_msgs/msg/Bool")
        put(out, "data", c.u8() ? 1.0 : 0.0);
    else if (type == "std_msgs/msg/String")
        put(out, "len", static_cast<double>(c.str().size()));
    else if (type == "sensor_msgs/msg/Imu")
    {
        header(c);
        quat(c, out, "orientation.");
        skipCov9(c);
        vec3(c, out, "angular_velocity.", "x", "y", "z");
        skipCov9(c);
        vec3(c, out, "linear_acceleration.", "x", "y", "z");
        skipCov9(c);
    }
    else if (type == "geometry_msgs/msg/Twist")
    {
        vec3(c, out, "linear.", "x", "y", "z");
        vec3(c, out, "angular.", "x", "y", "z");
    }
    else if (type == "geometry_msgs/msg/PoseStamped")
    {
        header(c);
        vec3(c, out, "position.", "x", "y", "z");
        quat(c, out, "orientation.");
    }
    else if (type == "nav_msgs/msg/Odometry")
    {
        header(c);
        c.str(); // child_frame_id
        vec3(c, out, "position.", "x", "y", "z");
        quat(c, out, "orientation.");
        for (int i = 0; i < 36; ++i)
            c.f64(); // pose.covariance
        vec3(c, out, "linear.", "x", "y", "z");
        vec3(c, out, "angular.", "x", "y", "z");
        for (int i = 0; i < 36; ++i)
            c.f64(); // twist.covariance
    }
    else if (type == "sensor_msgs/msg/NavSatFix")
    {
        header(c);
        c.u8(); // status.status
        c.u8(); // status.service
        put(out, "latitude", c.f64());
        put(out, "longitude", c.f64());
        put(out, "altitude", c.f64());
    }
    else if (type == "std_msgs/msg/Header")
        header(c);
    else
        // 未识别类型：只给原始长度。宁可少一列，也不静默造数据。
        put(out, "payload_bytes", static_cast<double>(size));
}

// ---- 每个 topic 的列式缓冲 ----
struct TopicBuf
{
    std::string table;
    std::string type;
    std::vector<std::string> cols;
    std::vector<std::vector<double>> data; // data[列][行]
    std::vector<int64_t> timeNs;

    void appendRow(int64_t t, const Fields &fields)
    {
        timeNs.push_back(t);
        for (size_t i = 0; i < cols.size(); ++i)
            data[i].push_back(NAN); // 新行先填空，等字段命中再覆盖
        for (const auto &kv : fields)
        {
            size_t idx = 0;
            for (; idx < cols.size(); ++idx)
                if (cols[idx] == kv.first)
                    break;
            if (idx == cols.size())
            {
                // 新列：之前所有行补 NAN，保持矩阵矩形。
                cols.push_back(kv.first);
                data.emplace_back();
                data.back().assign(timeNs.size() - 1, NAN);
                data.back().push_back(kv.second);
                continue;
            }
            data[idx].back() = kv.second;
        }
    }
};

constexpr size_t kChunk = 16384;

bool emitTopic(const TopicBuf &t, const PsParserSink *sink, std::string &err)
{
    if (t.timeNs.empty() || t.cols.empty())
        return true;
    std::vector<PsFrameColumn> cols(t.cols.size());
    for (size_t start = 0; start < t.timeNs.size(); start += kChunk)
    {
        const size_t rows = std::min(kChunk, t.timeNs.size() - start);
        for (size_t c = 0; c < t.cols.size(); ++c)
        {
            PsFrameColumn &col = cols[c];
            col.name = t.cols[c].c_str();
            col.unit = nullptr;
            col.type = PS_VALUE_F64;
            col.values = t.data[c].data() + start;
            col.nulls = nullptr;
        }
        PsFrameBatch batch{};
        batch.table = t.table.c_str();
        batch.time_ns = t.timeNs.data() + start;
        batch.rows = static_cast<uint32_t>(rows);
        batch.columns = static_cast<uint32_t>(cols.size());
        batch.column_data = cols.data();
        if (sink && sink->frame_batch && sink->frame_batch(sink->ctx, &batch) != 0)
        {
            err = "宿主拒绝了一批数据";
            return false;
        }
    }
    return true;
}

// ---- SQLite：优先内存反序列化，失败回退临时文件 ----
struct Db
{
    sqlite3 *db = nullptr;
    std::string tmpPath;

    ~Db() { close(); }
    void close()
    {
        if (db)
        {
            sqlite3_close(db);
            db = nullptr;
        }
        if (!tmpPath.empty())
        {
            std::remove(tmpPath.c_str());
            tmpPath.clear();
        }
    }
};

bool openDb(const std::vector<uint8_t> &bytes, Db &out, std::string &err)
{
    if (bytes.size() < 16 || std::memcmp(bytes.data(), "SQLite format 3", 15) != 0)
    {
        err = "不是 SQLite3 数据库（ROS2 bag 需要 .db3）";
        return false;
    }
    if (sqlite3_open_v2(":memory:", &out.db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                        nullptr) == SQLITE_OK)
    {
        // READONLY：sqlite 不会释放这块内存（由调用者的 vector 持有）。
        const int rc = sqlite3_deserialize(
            out.db, "main", const_cast<unsigned char *>(bytes.data()),
            static_cast<sqlite3_int64>(bytes.size()),
            static_cast<sqlite3_int64>(bytes.size()), SQLITE_DESERIALIZE_READONLY);
        if (rc == SQLITE_OK)
            return true;
        sqlite3_close(out.db);
        out.db = nullptr;
    }
    // 回退：写临时文件再打开（老 sqlite 构建没有 deserialize）。
    std::error_code fsEc;
    const std::filesystem::path tmpDir = std::filesystem::temp_directory_path(fsEc);
    const std::string path =
        ((fsEc ? std::filesystem::path(".") : tmpDir) /
         ("plotscope_rosbag2_" + std::to_string(PS_GETPID()) + ".db3"))
            .string();
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
    {
        err = "无法创建临时文件";
        return false;
    }
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    out.tmpPath = path;
    if (sqlite3_open_v2(path.c_str(), &out.db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        err = "无法打开 rosbag2 数据库";
        return false;
    }
    return true;
}

std::string normalizeType(const std::string &type)
{
    // 兼容 "std_msgs/Float64"（早期 rosbag2 元数据）与 "std_msgs/msg/Float64"。
    std::string t = type;
    const size_t slash = t.find('/');
    if (slash != std::string::npos && t.compare(slash, 5, "/msg/") != 0)
        t.insert(slash, "/msg");
    return t;
}

std::string topicTable(const std::string &name)
{
    std::string t = name;
    while (!t.empty() && t.front() == '/')
        t.erase(t.begin());
    std::replace(t.begin(), t.end(), '/', '_');
    if (t.empty())
        t = "topic";
    return t;
}

bool parseDb(const std::vector<uint8_t> &bytes, const std::string &topicsFilter,
             const PsParserSink *sink, std::string &err)
{
    Db db;
    if (!openDb(bytes, db, err))
        return false;

    struct TopicInfo
    {
        std::string name;
        std::string type;
    };
    std::map<int, TopicInfo> topics;
    {
        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(db.db, "SELECT id,name,type FROM topics", -1, &st, nullptr) !=
            SQLITE_OK)
        {
            err = std::string("读 topics 失败：") + sqlite3_errmsg(db.db);
            return false;
        }
        while (sqlite3_step(st) == SQLITE_ROW)
        {
            const int id = sqlite3_column_int(st, 0);
            const char *name = reinterpret_cast<const char *>(sqlite3_column_text(st, 1));
            const char *type = reinterpret_cast<const char *>(sqlite3_column_text(st, 2));
            topics[id] = {name ? name : "", type ? normalizeType(type) : ""};
        }
        sqlite3_finalize(st);
    }
    if (topics.empty())
    {
        err = "topics 表为空";
        return false;
    }

    // topics 过滤（configure 的 "topics":"a,b"，空=全部）：按 topic 名匹配，
    // 带不带前导 '/' 都认。
    std::vector<std::string> wanted;
    if (!topicsFilter.empty())
    {
        size_t pos = 0;
        while (pos <= topicsFilter.size())
        {
            const size_t comma = topicsFilter.find(',', pos);
            std::string one = topicsFilter.substr(pos, comma == std::string::npos
                                                           ? std::string::npos
                                                           : comma - pos);
            while (!one.empty() && (one.front() == ' ' || one.front() == '/'))
                one.erase(one.begin());
            while (!one.empty() && one.back() == ' ')
                one.pop_back();
            if (!one.empty())
                wanted.push_back(one);
            if (comma == std::string::npos)
                break;
            pos = comma + 1;
        }
    }

    std::map<int, TopicBuf> bufs;
    std::map<std::string, int> tableUsed;
    for (const auto &kv : topics)
    {
        if (!wanted.empty())
        {
            std::string name = kv.second.name;
            while (!name.empty() && name.front() == '/')
                name.erase(name.begin());
            bool hit = false;
            for (const auto &w : wanted)
                if (w == name) { hit = true; break; }
            if (!hit)
                continue;
        }
        TopicBuf b;
        b.type = kv.second.type;
        const std::string base = topicTable(kv.second.name);
        const int n = ++tableUsed[base];
        b.table = n == 1 ? base : base + "#" + std::to_string(n);
        bufs[kv.first] = std::move(b);
    }

    sqlite3_stmt *mq = nullptr;
    if (sqlite3_prepare_v2(db.db, "SELECT topic_id,timestamp,data FROM messages ORDER BY timestamp",
                           -1, &mq, nullptr) != SQLITE_OK)
    {
        err = std::string("读 messages 失败：") + sqlite3_errmsg(db.db);
        return false;
    }
    int64_t t0 = -1;
    while (sqlite3_step(mq) == SQLITE_ROW)
    {
        const int tid = sqlite3_column_int(mq, 0);
        auto it = bufs.find(tid);
        if (it == bufs.end())
            continue;
        const int64_t ns = sqlite3_column_int64(mq, 1);
        if (t0 < 0)
            t0 = ns;
        const void *blob = sqlite3_column_blob(mq, 2);
        const int blobSize = sqlite3_column_bytes(mq, 2);
        if (!blob || blobSize <= 0)
            continue;
        Fields fields;
        decode(it->second.type, static_cast<const uint8_t *>(blob),
               static_cast<uint32_t>(blobSize), fields);
        // 首条消息为 0 点；rosbag2 timestamp 原生纳秒，内部轴同为纳秒，直接使用。
        const int64_t tNs = ns - t0;
        it->second.appendRow(tNs, fields);
    }
    sqlite3_finalize(mq);

    bool any = false;
    for (const auto &kv : bufs)
    {
        if (kv.second.timeNs.empty())
            continue;
        any = true;
        if (!emitTopic(kv.second, sink, err))
            return false;
    }
    if (!any)
    {
        err = "messages 表里没有可解码的消息";
        return false;
    }
    return true;
}

// ---- SPI 包装 ----

struct SpiCtx
{
    std::vector<uint8_t> bytes;
    std::string topics; // configure 传入的 topic 过滤（逗号分隔，空=全部）
};

void *create() { return new SpiCtx(); }
void destroy(void *ctx) { delete static_cast<SpiCtx *>(ctx); }

int32_t sniff(void *, const uint8_t *data, uint32_t size)
{
    if (!data || size < 16)
        return 0;
    return std::memcmp(data, "SQLite format 3", 15) == 0 ? 1 : 0;
}

int32_t feed(void *ctx, const uint8_t *data, uint32_t size, const PsParserSink *, PsError *err)
{
    auto *c = static_cast<SpiCtx *>(ctx);
    if (!c)
    {
        if (err) { err->code = 1; std::snprintf(err->message, sizeof(err->message), "解析器实例为空"); }
        return -1;
    }
    if (data && size)
        c->bytes.insert(c->bytes.end(), data, data + size);
    return 0;
}

int32_t finish(void *ctx, const PsParserSink *sink, PsError *err)
{
    auto *c = static_cast<SpiCtx *>(ctx);
    if (!c)
        return -1;
    std::string msg;
    if (!parseDb(c->bytes, c->topics, sink, msg))
    {
        if (err) { err->code = 2; std::snprintf(err->message, sizeof(err->message), "%s", msg.c_str()); }
        return -1;
    }
    return 0;
}

void reset(void *ctx)
{
    auto *c = static_cast<SpiCtx *>(ctx);
    if (c)
    {
        c->bytes.clear();
        c->topics.clear();
    }
}

const PsParserInfo *info(void *) { return &parserInfo(); }
uint64_t capabilities(void *) { return 0; }

int32_t configure(void *ctx, const char *optsJson, PsError *)
{
    auto *c = static_cast<SpiCtx *>(ctx);
    if (!c)
        return -1;
    c->topics.clear();
    if (!optsJson || !*optsJson)
        return 0;
    // 极简 JSON 取值：只认 "topics":"a,b"（不引第三方 JSON 库）。
    const std::string json(optsJson);
    const size_t key = json.find("\"topics\"");
    if (key == std::string::npos)
        return 0;
    const size_t colon = json.find(':', key);
    const size_t q1 = json.find('"', colon);
    if (colon == std::string::npos || q1 == std::string::npos)
        return 0;
    const size_t q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos)
        return 0;
    c->topics = json.substr(q1 + 1, q2 - q1 - 1);
    return 0;
}

static const PsParserVtable kVtable = {
    PS_PARSER_ABI_VERSION,
    sizeof(PsParserVtable),
    create,
    destroy,
    sniff,
    feed,
    finish,
    reset,
    info,
    capabilities,
    configure,
};

} // namespace

const PsParserInfo &parserInfo()
{
    static const PsParserInfo kInfo = {
        PS_PARSER_ABI_VERSION,
        "rosbag2",
        "ROS2 bag（SQLite3 .db3）",
        ".db3,.db2",
        1, // can_sniff（SQLite 头）
        0, // streaming
        {0, 0},
    };
    return kInfo;
}

const PsParserVtable *parserVtable() { return &kVtable; }

// 注意限定命名空间：类内直接写 parserInfo() 会解析到成员自身 → 无限递归（静态初始化阶段崩）。
const PsParserInfo &Rosbag2ParserSpi::parserInfo() { return rosbag2::parserInfo(); }
const PsParserVtable *Rosbag2ParserSpi::parserVtable() { return rosbag2::parserVtable(); }

} // namespace nextpilot::plotscope::ext::rosbag2

// 静态自注册：宿主按后缀 .db3 / sniff 即可选到 rosbag2。
PS_REGISTER_PARSER(::nextpilot::plotscope::ext::rosbag2::Rosbag2ParserSpi)
