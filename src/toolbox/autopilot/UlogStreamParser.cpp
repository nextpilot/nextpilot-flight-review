#include "UlogStreamParser.h"

#include "UlogParserSpi.h"

#include "UlogParserSpi.h"

#include "Dataset.h"
#include "ParserHub.h"
#include "ps_parser_spi.h"

#include <QByteArray>
#include <QFile>
#include <QMap>
#include <QString>
#include <QVector>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::parser;

namespace nextpilot::plotscope::ext::ulog
{
namespace
{

// ============================================================================
// 以下协议解析算法复用自旧 extension：
//   src/extensions/builtin/autopilot/datasources/ulog/UlogParser.cpp
// 格式知识（magic / 段语义 / 类型解码 / 字段展平）逐字保留，未重新发明。
// ============================================================================


// ============================================================================
// 以下协议解析算法复用自旧 extension：
//   src/extensions/builtin/autopilot/datasources/ulog/UlogParser.cpp
// 格式知识（magic / 段语义 / 类型解码 / 字段展平）逐字保留，未重新发明。
// ============================================================================

constexpr uint8_t kMagic[7] = {'U', 'L', 'o', 'g', 0x01, 0x12, 0x35};

enum class Prim { I8, U8, I16, U16, I32, U32, I64, U64, F32, F64, Bool, Char };

size_t primSize(Prim p)
{
    switch (p)
    {
    case Prim::I8: case Prim::U8: case Prim::Bool: case Prim::Char: return 1;
    case Prim::I16: case Prim::U16: return 2;
    case Prim::I32: case Prim::U32: case Prim::F32: return 4;
    case Prim::I64: case Prim::U64: case Prim::F64: return 8;
    }
    return 0;
}

bool primFromName(const std::string &s, Prim &p)
{
    static const std::map<std::string, Prim> table{
        {"int8_t", Prim::I8}, {"uint8_t", Prim::U8}, {"int16_t", Prim::I16},
        {"uint16_t", Prim::U16}, {"int32_t", Prim::I32}, {"uint32_t", Prim::U32},
        {"int64_t", Prim::I64}, {"uint64_t", Prim::U64}, {"float", Prim::F32},
        {"double", Prim::F64}, {"bool", Prim::Bool}, {"char", Prim::Char},
    };
    auto it = table.find(s);
    if (it == table.end())
        return false;
    p = it->second;
    return true;
}

bool readPrim(const uint8_t *b, size_t len, size_t off, Prim p, double &out)
{
    if (off + primSize(p) > len)
        return false;
    switch (p)
    {
    case Prim::I8: out = static_cast<double>(static_cast<int8_t>(b[off])); return true;
    case Prim::U8: case Prim::Char: out = static_cast<double>(b[off]); return true;
    case Prim::Bool: out = b[off] != 0 ? 1.0 : 0.0; return true;
    case Prim::I16: out = static_cast<double>(static_cast<int16_t>(b[off] | (b[off + 1] << 8))); return true;
    case Prim::U16: out = static_cast<double>(b[off] | (b[off + 1] << 8)); return true;
    case Prim::I32:
    {
        const int32_t v = static_cast<int32_t>(static_cast<uint32_t>(b[off]) |
            (uint32_t(b[off + 1]) << 8) | (uint32_t(b[off + 2]) << 16) | (uint32_t(b[off + 3]) << 24));
        out = static_cast<double>(v);
        return true;
    }
    case Prim::U32:
        out = static_cast<double>(uint32_t(b[off]) | (uint32_t(b[off + 1]) << 8) |
                                  (uint32_t(b[off + 2]) << 16) | (uint32_t(b[off + 3]) << 24));
        return true;
    case Prim::F32:
    {
        float v = 0;
        std::memcpy(&v, b + off, 4);
        out = static_cast<double>(v);
        return true;
    }
    case Prim::I64:
    {
        int64_t v = 0;
        std::memcpy(&v, b + off, 8);
        out = static_cast<double>(v);
        return true;
    }
    case Prim::U64:
    {
        uint64_t v = 0;
        std::memcpy(&v, b + off, 8);
        out = static_cast<double>(v);
        return true;
    }
    case Prim::F64:
    {
        double v = 0;
        std::memcpy(&v, b + off, 8);
        out = v;
        return true;
    }
    }
    return false;
}

// ULog timestamp 是 U64 微秒。这里直接读原生字节，不经 double 中转，
// 避免 1e15 量级时间戳的精度损失（readPrim 的 double 仅用于普通数值列）。
bool readTimestampUs(const uint8_t *b, size_t len, size_t off, uint64_t &out)
{
    if (off + 8 > len)
        return false;
    uint64_t v = 0;
    std::memcpy(&v, b + off, 8);
    out = v;
    return true;
}

struct Base
{
    bool isPrim = false;
    Prim prim = Prim::U8;
    std::string msg;
};

struct Field
{
    std::string name;
    Base base;
    size_t array = 0;
};

struct Leaf
{
    std::string path;
    size_t off = 0;
    bool hasPrim = false;
    Prim prim = Prim::U8;
    bool keepValue = true;
};

std::string trim(const std::string &s)
{
    const size_t lb = s.find_first_not_of(" \t\r\n");
    const size_t rb = s.find_last_not_of(" \t\r\n");
    return lb == std::string::npos ? std::string() : s.substr(lb, rb - lb + 1);
}

bool parseSize(const std::string &s, size_t &out)
{
    if (s.empty())
        return false;
    size_t v = 0;
    for (char c : s)
    {
        if (c < '0' || c > '9')
            return false;
        const size_t d = static_cast<size_t>(c - '0');
        if (v > (std::numeric_limits<size_t>::max() - d) / 10)
            return false;
        v = v * 10 + d;
    }
    out = v;
    return true;
}

bool splitFormat(const std::string &text, std::string &name, std::vector<Field> &fields)
{
    const size_t colon = text.find(':');
    if (colon == std::string::npos)
        return false;
    name = trim(text.substr(0, colon));
    if (name.empty())
        return false;
    fields.clear();
    size_t start = colon + 1;
    while (start <= text.size())
    {
        const size_t semi = text.find(';', start);
        const std::string raw = trim(text.substr(start, semi == std::string::npos ? std::string::npos : semi - start));
        if (!raw.empty())
        {
            const size_t space = raw.find(' ');
            if (space == std::string::npos)
                return false;
            std::string ty = trim(raw.substr(0, space));
            std::string fname = trim(raw.substr(space + 1));
            if (fname.empty())
                return false;
            size_t array = 0;
            const size_t lb = ty.find('[');
            if (lb != std::string::npos)
            {
                const size_t rb = ty.find(']', lb);
                if (rb == std::string::npos)
                    return false;
                if (!parseSize(ty.substr(lb + 1, rb - lb - 1), array))
                    return false;
                ty = ty.substr(0, lb);
            }
            if (array == 0)
            {
                const size_t flb = fname.find('[');
                if (flb != std::string::npos)
                {
                    const size_t frb = fname.find(']', flb);
                    if (frb == std::string::npos)
                        return false;
                    if (!parseSize(fname.substr(flb + 1, frb - flb - 1), array))
                        return false;
                    fname = fname.substr(0, flb);
                }
            }
            Base base;
            Prim p;
            if (primFromName(ty, p))
            {
                base.isPrim = true;
                base.prim = p;
            }
            else
            {
                base.msg = ty;
            }
            fields.push_back({fname, base, array});
        }
        if (semi == std::string::npos)
            break;
        start = semi + 1;
    }
    return true;
}

bool flatten(const std::map<std::string, std::vector<Field>> &formats, const std::string &name,
             size_t off, const std::string &prefix, std::vector<Leaf> &out, size_t depth,
             size_t &consumed)
{
    if (depth > 16)
        return false;
    auto it = formats.find(name);
    if (it == formats.end())
        return false;
    size_t cur = off;
    for (const Field &f : it->second)
    {
        const std::string path = prefix.empty() ? f.name : prefix + "." + f.name;
        const size_t repeated = std::max<size_t>(f.array, 1);
        const bool skip = f.name == "timestamp" || f.name.rfind("_padding", 0) == 0;
        if (f.base.isPrim && f.base.prim == Prim::Char && f.array > 0)
        {
            Leaf l;
            l.path = path;
            l.off = cur;
            l.hasPrim = false;
            l.keepValue = false;
            out.push_back(std::move(l));
            cur += f.array;
        }
        else if (f.base.isPrim)
        {
            for (size_t k = 0; k < repeated; ++k)
            {
                Leaf l;
                l.path = repeated > 1 ? path + "." + (k < 10 ? "0" : "") + std::to_string(k) : path;
                l.off = cur;
                l.hasPrim = true;
                l.prim = f.base.prim;
                l.keepValue = !skip;
                out.push_back(std::move(l));
                cur += primSize(f.base.prim);
            }
        }
        else
        {
            for (size_t k = 0; k < repeated; ++k)
            {
                const std::string sub = repeated > 1 ? path + "." + (k < 10 ? "0" : "") + std::to_string(k) : path;
                size_t child = 0;
                if (!flatten(formats, f.base.msg, cur, sub, out, depth + 1, child))
                    return false;
                cur += child;
            }
        }
    }
    consumed = cur - off;
    return true;
}

bool decodeParam(const uint8_t *p, size_t len, std::string &key, std::string &value)
{
    if (len < 3)
        return false;
    const uint8_t keyLen = p[2];
    if (3 + keyLen + 1 > len)
        return false;
    key.assign(reinterpret_cast<const char *>(p + 3), keyLen);
    if (key.empty())
        return false;
    const char ty = static_cast<char>(p[3 + keyLen]);
    const uint8_t *d = p + 3 + keyLen + 1;
    const size_t dlen = len - (3 + keyLen + 1);
    auto str = [](auto v) { return std::to_string(v); };
    switch (ty)
    {
    case 'b': if (dlen < 1) return false; value = std::to_string(static_cast<int8_t>(d[0])); return true;
    case 'B': if (dlen < 1) return false; value = std::to_string(d[0]); return true;
    case 'h': if (dlen < 2) return false; value = std::to_string(static_cast<int16_t>(d[0] | (d[1] << 8))); return true;
    case 'H': if (dlen < 2) return false; value = std::to_string(d[0] | (d[1] << 8)); return true;
    case 'i': if (dlen < 4) return false; value = str(static_cast<int32_t>(uint32_t(d[0]) | (uint32_t(d[1]) << 8) | (uint32_t(d[2]) << 16) | (uint32_t(d[3]) << 24))); return true;
    case 'I': if (dlen < 4) return false; value = str(uint32_t(d[0]) | (uint32_t(d[1]) << 8) | (uint32_t(d[2]) << 16) | (uint32_t(d[3]) << 24)); return true;
    case 'q': case 'Q':
    {
        if (dlen < 8) return false;
        uint64_t v = 0;
        std::memcpy(&v, d, 8);
        value = std::to_string(ty == 'q' ? static_cast<int64_t>(v) : v);
        return true;
    }
    case 'f': if (dlen < 4) return false; { float v = 0; std::memcpy(&v, d, 4); value = std::to_string(v); return true; }
    case 'd': if (dlen < 8) return false; { double v = 0; std::memcpy(&v, d, 8); value = std::to_string(v); return true; }
    default: return false;
    }
}

std::string readCString(const uint8_t *p, size_t len)
{
    size_t n = 0;
    while (n < len && p[n] != 0)
        ++n;
    return std::string(reinterpret_cast<const char *>(p), n);
}

std::string infoValue(const std::string &key, const uint8_t *p, size_t n)
{
    if ((key.find("ver") != std::string::npos || key.find("revision") != std::string::npos ||
         key.find("count") != std::string::npos || key.find("bits") != std::string::npos) && n >= 4)
    {
        uint32_t v = 0;
        std::memcpy(&v, p, 4);
        return std::to_string(v);
    }
    return readCString(p, n);
}

void addInfo(std::vector<std::pair<std::string, std::string>> &out, const uint8_t *p, size_t n)
{
    if (n < 1) return;
    const size_t kl = p[0];
    if (n < 1 + kl) return;
    const std::string key = readCString(p + 1, kl);
    out.emplace_back(key, infoValue(key, p + 1 + kl, n - 1 - kl));
}

// ============================================================================
// 产出端：流式 typed 缓冲 + appendFrame 灌入 Rust。
// 协议解析算法不变，只把「攒满全量再建表」换成「满批即灌入、缓冲复用」。
// ============================================================================

constexpr int kChunkRows = 4096; // C++ 侧每批缓冲行数，恒定，与文件大小无关

ValueType primToValueType(Prim p)
{
    switch (p)
    {
    case Prim::I8: return ValueType::I8;
    case Prim::U8: case Prim::Char: return ValueType::U8;
    case Prim::I16: return ValueType::I16;
    case Prim::U16: return ValueType::U16;
    case Prim::I32: return ValueType::I32;
    case Prim::U32: return ValueType::U32;
    case Prim::I64: return ValueType::I64;
    case Prim::U64: return ValueType::U64;
    case Prim::F32: return ValueType::F32;
    case Prim::F64: return ValueType::F64;
    case Prim::Bool: return ValueType::Bool;
    }
    return ValueType::F64;
}

// 小数据元数据表（params/facts/events/dropouts）：量本身有界，攒完一次写出。
struct KvRows { std::vector<std::pair<std::string, std::string>> rows; };
struct EventRow { double t; uint8_t level; uint16_t tag; bool tagged; std::string text; };

// 单个数值列的一批缓冲：按原生类型存字节，values 指针直接交给 kernel。
struct ColumnBuffer
{
    QString name;
    ValueType type = ValueType::F64;
    QByteArray bytes;

    void init(const std::string &leafPath, ValueType t, int rows)
    {
        name = QString::fromStdString(leafPath);
        type = t;
        bytes.clear();
        bytes.reserve(static_cast<qsizetype>(rows) * valueTypeBytes(type));
    }
    void clear() { bytes.clear(); }
    const void *data() const { return bytes.constData(); }

    // readPrim 统一产出 double，这里按目标类型转成原生字节追加。
    void append(double v)
    {
        switch (type)
        {
        case ValueType::F64: appendPod<double>(v); break;
        case ValueType::F32: appendPod<float>(static_cast<float>(v)); break;
        case ValueType::I64: appendPod<int64_t>(static_cast<int64_t>(v)); break;
        case ValueType::U64: appendPod<uint64_t>(static_cast<uint64_t>(v)); break;
        case ValueType::I32: appendPod<int32_t>(static_cast<int32_t>(v)); break;
        case ValueType::U32: appendPod<uint32_t>(static_cast<uint32_t>(v)); break;
        case ValueType::I16: appendPod<int16_t>(static_cast<int16_t>(v)); break;
        case ValueType::U16: appendPod<uint16_t>(static_cast<uint16_t>(v)); break;
        case ValueType::I8: appendPod<int8_t>(static_cast<int8_t>(v)); break;
        case ValueType::U8: appendPod<uint8_t>(static_cast<uint8_t>(v)); break;
        case ValueType::Bool: appendPod<uint8_t>(v != 0.0 ? 1 : 0); break;
        case ValueType::Text: break; // D 段数值列不会出现 TEXT
        }
    }

private:
    template <typename T>
    void appendPod(T v)
    {
        const char *p = reinterpret_cast<const char *>(&v);
        bytes.append(p, static_cast<qsizetype>(sizeof(T)));
    }
};

// 一个 topic 一张表：含一组数值列缓冲 + 共享时间轴，满批 flush。
struct StreamTable
{
    std::string baseTopic;
    uint8_t multiId = 0;
    QString tableName;                    // 首次 flush 时按 multiTopics 规则确定并缓存
    std::optional<size_t> tsOff;
    std::vector<Leaf> leaves;             // 仅数值 leaf（hasPrim && keepValue）
    QVector<ColumnBuffer> buffers;
    QVector<TimeUs> timeBuf;
    int filled = 0;

    void init(std::optional<size_t> timestampOff)
    {
        tsOff = timestampOff;
        timeBuf.resize(kChunkRows);
        buffers.resize(static_cast<qsizetype>(leaves.size()));
        for (size_t k = 0; k < leaves.size(); ++k)
            buffers[static_cast<qsizetype>(k)].init(leaves[k].path, primToValueType(leaves[k].prim), kChunkRows);
    }
};

// ============================================================================
// 产出端：流式 typed 缓冲 + appendFrame 灌入 Rust。
// 协议解析算法不变，只把「攒满全量再建表」换成「满批即灌入、缓冲复用」。
// ============================================================================


// 单个数值列的一批缓冲：按原生类型存字节，values 指针直接交给 kernel。
// ============================================================================
// 产出端（emitter）：解析器把"一批行"交给它，由它决定去哪。
//
// 这是**两条数据通路归并的唯一分叉点**（AGENTS.md §3 P0-1）：
//   - 经 PsParserSink 吐批：SPI 路径，宿主决定批去哪（Rust 表 / 转发 / 丢弃）；
//   - 直接写进 Dataset：旧路径，driver 直连仓库。
// 协议解析逻辑完全不感知它——所以格式知识只有**一份实现**，
// 不会出现"每个格式写两份 parser"的问题。
// ============================================================================
struct Emitter
{
    virtual ~Emitter() = default;

    // 方法名不能叫 emit：Qt 把 emit 定义成空宏（#define emit），
    // 写成成员函数会被展开成 "virtual bool (params)" —— 语法错误。
    virtual bool emitBatch(const QString &table, const int64_t *timeUs, uint32_t rows,
                           const std::vector<PsFrameColumn> &cols, QString &err) = 0;
};

// core 的 ValueType <-> SPI 的 PsValueType（同值域，见 ps_parser_spi.h）。
PsValueType toPsValueType(ValueType t)
{
    switch (t)
    {
    case ValueType::F64: return PS_VALUE_F64;
    case ValueType::F32: return PS_VALUE_F32;
    case ValueType::I64: return PS_VALUE_I64;
    case ValueType::U64: return PS_VALUE_U64;
    case ValueType::I32: return PS_VALUE_I32;
    case ValueType::U32: return PS_VALUE_U32;
    case ValueType::I16: return PS_VALUE_I16;
    case ValueType::U16: return PS_VALUE_U16;
    case ValueType::I8: return PS_VALUE_I8;
    case ValueType::U8: return PS_VALUE_U8;
    case ValueType::Bool: return PS_VALUE_BOOL;
    case ValueType::Text: return PS_VALUE_TEXT;
    }
    return PS_VALUE_F64;
}

ValueType fromPsValueType(PsValueType t)
{
    switch (t)
    {
    case PS_VALUE_F64: return ValueType::F64;
    case PS_VALUE_F32: return ValueType::F32;
    case PS_VALUE_I64: return ValueType::I64;
    case PS_VALUE_U64: return ValueType::U64;
    case PS_VALUE_I32: return ValueType::I32;
    case PS_VALUE_U32: return ValueType::U32;
    case PS_VALUE_I16: return ValueType::I16;
    case PS_VALUE_U16: return ValueType::U16;
    case PS_VALUE_I8: return ValueType::I8;
    case PS_VALUE_U8: return ValueType::U8;
    case PS_VALUE_BOOL: return ValueType::Bool;
    case PS_VALUE_TEXT: return ValueType::Text;
    }
    return ValueType::F64;
}

bool flushTable(StreamTable &st, Emitter &out, const std::map<std::string, uint8_t> &multiTopics,
                QString &err)
{
    if (st.filled <= 0)
        return true;
    if (st.tableName.isEmpty())
    {
        const uint8_t maxMulti = multiTopics.count(st.baseTopic) ? multiTopics.at(st.baseTopic) : 0;
        const std::string suffix = maxMulti > 0
            ? std::string(".") + (st.multiId < 10 ? "0" : "") + std::to_string(st.multiId)
            : std::string();
        st.tableName = QString::fromStdString(st.baseTopic + suffix);
    }
    // 列的原生字节布局与 PsFrameColumn 同构（name/unit/type/values/nulls），
    // 所以这里直接组装 SPI 形状——零转换。
    //
    // PsFrameColumn.name 是 const char*，只在回调期间有效：
    // utf8 副本必须活到 emitBatch() 返回，用局部数组兜住。
    std::vector<QByteArray> names;
    names.reserve(st.buffers.size());
    std::vector<PsFrameColumn> cols;
    cols.reserve(st.buffers.size());
    for (const ColumnBuffer &buf : st.buffers)
    {
        names.push_back(buf.name.toUtf8());
        PsFrameColumn c{};
        c.name = names.back().constData();
        c.unit = "";
        c.type = toPsValueType(buf.type);
        c.values = buf.data();
        c.nulls = nullptr; // readPrim 失败写 NaN，kernel 统计天然忽略
        cols.push_back(c);
    }
    if (!out.emitBatch(st.tableName, st.timeBuf.constData(), static_cast<uint32_t>(st.filled),
                       cols, err))
        return false;
    st.filled = 0;
    for (ColumnBuffer &buf : st.buffers)
        buf.clear();
    return true;
}
bool writeTextTable(Emitter &out, const QString &table, const QVector<TimeUs> &timeUs,
                    const QString &col0, const std::vector<std::string> &c0,
                    const QString &col1, const std::vector<std::string> &c1, QString &err)
{
    if (c0.empty())
        return true;
    QVector<QByteArray> store0, store1;
    store0.reserve(static_cast<qsizetype>(c0.size()));
    store1.reserve(static_cast<qsizetype>(c1.size()));
    QVector<const char *> p0, p1;
    for (const auto &s : c0) { store0.push_back(QByteArray::fromStdString(s)); p0.push_back(store0.constLast().constData()); }
    for (const auto &s : c1) { store1.push_back(QByteArray::fromStdString(s)); p1.push_back(store1.constLast().constData()); }
    const QByteArray n0 = col0.toUtf8();
    const QByteArray n1 = col1.toUtf8();
    std::vector<PsFrameColumn> cols(2);
    cols[0].name = n0.constData(); cols[0].unit = ""; cols[0].type = PS_VALUE_TEXT; cols[0].values = p0.constData(); cols[0].nulls = nullptr;
    cols[1].name = n1.constData(); cols[1].unit = ""; cols[1].type = PS_VALUE_TEXT; cols[1].values = p1.constData(); cols[1].nulls = nullptr;
    return out.emitBatch(table, timeUs.constData(), static_cast<uint32_t>(c0.size()), cols, err);
}

// ---------------------------------------------------------------------------
// 喂字节式解析：把字节流喂进来，解析出的批交给产出端。
//
// 这是 SPI 路径的**唯一**实现；旧路径 parseUlogStream() 也委托给它。
// 于是"格式解析"只有一份，"读文件"与"批去哪"由调用方决定。
// ---------------------------------------------------------------------------

// 解析器的可变状态（跨 feed 调用保持）。
struct UlogState
{
    QByteArray pending; // 段缓冲：字节流 -> 攒够一段

    std::map<std::string, std::vector<Field>> formats;
    std::vector<StreamTable> tables;
    std::map<uint16_t, size_t> byId;
    std::map<std::pair<std::string, uint8_t>, size_t> byTopic;
    std::map<std::string, uint8_t> multiTopics;

    KvRows params;
    KvRows facts;
    std::vector<EventRow> events;
    std::vector<std::pair<double, uint16_t>> dropouts;

    bool headerDone = false;
    bool failed = false;
    double lastTimestamp = 0.0;
};

// 处理一个完整段（type + body）。返回 false 表示出错。
bool handleSegment(UlogState &st, uint8_t ty, const uint8_t *p, uint16_t size, Emitter &out,
                   QString &err)
{
    auto &formats = st.formats;
    auto &tables = st.tables;
    auto &byId = st.byId;
    auto &byTopic = st.byTopic;
    auto &multiTopics = st.multiTopics;
    auto &params = st.params;
    auto &facts = st.facts;
    auto &events = st.events;
    auto &dropouts = st.dropouts;
    auto &lastTimestamp = st.lastTimestamp;

        // F：格式定义
        if (ty == 'F')
        {
            std::string text(reinterpret_cast<const char *>(p), size);
            std::string name;
            std::vector<Field> fields;
            if (splitFormat(text, name, fields))
                formats[name] = std::move(fields);
        }
        // A：订阅（msgId -> topic + 字段展平）
        else if (ty == 'A' && size >= 3)
        {
            const uint8_t multiId = p[0];
            const uint16_t msgId = static_cast<uint16_t>(p[1] | (p[2] << 8));
            const std::string name = readCString(p + 3, size - 3);
            auto fit = formats.find(name);
            if (fit == formats.end())
                return true;
            std::vector<Leaf> leaves;
            size_t consumed = 0;
            if (!flatten(formats, name, 0, "", leaves, 0, consumed))
                return true;
            std::optional<size_t> tsOff;
            for (const Leaf &l : leaves)
                if (l.path == "timestamp" && l.hasPrim && l.prim == Prim::U64)
                {
                    tsOff = l.off;
                    break;
                }
            if (!tsOff)
                return true;
            if (byTopic.count({name, multiId}))
                return true;

            StreamTable st;
            st.baseTopic = name;
            st.multiId = multiId;
            for (const Leaf &l : leaves)
                if (l.hasPrim && l.keepValue)
                    st.leaves.push_back(l);
            if (st.leaves.empty())
                return true;
            st.init(tsOff);
            const size_t idx = tables.size();
            byTopic[{name, multiId}] = idx;
            byId[msgId] = idx;
            multiTopics[name] = std::max(multiTopics[name], multiId);
            tables.push_back(std::move(st));
        }
        // D：数值数据 —— 流式灌入，不在 C++ 攒全量
        else if (ty == 'D' && size >= 2)
        {
            const uint16_t msgId = static_cast<uint16_t>(p[0] | (p[1] << 8));
            auto bit = byId.find(msgId);
            if (bit == byId.end())
                return true;
            StreamTable &st = tables[bit->second];
            const uint8_t *payload = p + 2;
            const size_t plen = size - 2;
            uint64_t tsUs = 0;
            if (!st.tsOff || !readTimestampUs(payload, plen, *st.tsOff, tsUs))
                return true;
            lastTimestamp = static_cast<double>(tsUs) / 1e6;
            st.timeBuf[st.filled] = static_cast<TimeUs>(tsUs);
            for (size_t k = 0; k < st.leaves.size(); ++k)
            {
                const Leaf &l = st.leaves[k];
                double v = std::numeric_limits<double>::quiet_NaN();
                readPrim(payload, plen, l.off, l.prim, v);
                st.buffers[static_cast<qsizetype>(k)].append(v);
            }
            ++st.filled;
            if (st.filled == kChunkRows)
            {
                if (!flushTable(st, out, multiTopics, err))
                {
                    return false;
                }
            }
        }
        // P/Q：参数（小数据）
        else if (ty == 'P' || ty == 'Q')
        {
            std::string key, value;
            if (decodeParam(p, size, key, value))
            {
                bool found = false;
                auto &target = params.rows;
                for (auto &kv : target)
                    if (kv.first == key) { kv.second = value; found = true; break; }
                if (!found)
                    target.emplace_back(key, value);
            }
        }
        // I/M：文件级事实（小数据）
        else if (ty == 'I')
        {
            addInfo(facts.rows, p, size);
        }
        else if (ty == 'M')
        {
            if (size >= 1) addInfo(facts.rows, p + 1, size - 1);
        }
        // L/C：文本事件（小数据）
        else if (ty == 'L' && size >= 9)
        {
            EventRow e{};
            e.level = p[0];
            uint64_t ts = 0;
            std::memcpy(&ts, p + 1, 8);
            e.t = static_cast<double>(ts) / 1e6;
            lastTimestamp = e.t;
            e.text = readCString(p + 9, size - 9);
            events.push_back(std::move(e));
        }
        else if (ty == 'C' && size >= 11)
        {
            EventRow e{};
            e.level = p[0];
            std::memcpy(&e.tag, p + 1, 2);
            uint64_t ts = 0;
            std::memcpy(&ts, p + 3, 8);
            e.t = static_cast<double>(ts) / 1e6;
            lastTimestamp = e.t;
            e.text = readCString(p + 11, size - 11);
            e.tagged = true;
            events.push_back(std::move(e));
        }
        // O：数据丢失（小数据）
        else if (ty == 'O' && size >= 10)
        {
            uint64_t ts = 0;
            uint16_t dur = 0;
            std::memcpy(&ts, p, 8);
            std::memcpy(&dur, p + 8, 2);
            dropouts.emplace_back(static_cast<double>(ts) / 1e6, dur);
        }
        // R：取消订阅
        else if (ty == 'R' && size >= 2)
        {
            const uint16_t msgId = static_cast<uint16_t>(p[0] | (p[1] << 8));
            byId.erase(msgId);
        }

    return true;
}


// 喂字节 / 收尾（SPI 与旧路径共用）。
bool feedUlogBytes(UlogState &st, const uint8_t *data, uint32_t size, Emitter &out, QString &err);
bool finishUlogBytes(UlogState &st, Emitter &out, QString &err);

// ---------------------------------------------------------------------------
// SPI 包装：把 ULog 以标准解析器的身份暴露给 ParserHub。
//
// 归并的支点（AGENTS.md §3 P0-1）：
//   ng 曾有两条数据通路并存——
//     A：data.load → IMessageParser::load()，驱动自己读文件、自己建 Dataset；
//     B：Source → IngestPipeline → parser SPI → Dataset，生产零调用。
//   处置是**归并到 B，不新建第三条路**。有了这个包装，宿主可以：
//     · 同一份解析器既读文件、也读流（喂字节的是调用者）；
//     · 同一份字节同时喂给多个解析器（扇出）；
//     · 读字节的同时旁路到录制器（raw/mcap 多路录制）。
// 格式解析本体与旧路径共用——**只有一份实现**。
// ---------------------------------------------------------------------------



// SPI 实例的上下文。
struct UlogCtx
{
    // 解析状态机（段缓冲 + 格式表 + 订阅表 + 元数据）。
    std::unique_ptr<UlogState> state;
    QString error;
};

// SPI 产出端：把批交给宿主给的 sink。
struct SpiEmitter final : Emitter
{
    const PsParserSink *sink = nullptr;

    bool emitBatch(const QString &table, const int64_t *timeUs, uint32_t rows,
                   const std::vector<PsFrameColumn> &cols, QString &err) override
    {
        if (!sink || !sink->frame_batch)
            return true;
        // PsFrameBatch 与 Rust ps_dataset_append_frame 的入参同构，落库零转换。
        PsFrameBatch batch{};
        const QByteArray tableName = table.toUtf8();
        batch.table = tableName.constData();
        batch.time_us = timeUs;
        batch.rows = rows;
        batch.columns = static_cast<uint32_t>(cols.size());
        batch.column_data = cols.empty() ? nullptr : cols.data();
        if (sink->frame_batch(sink->ctx, &batch) != 0)
        {
            err = QStringLiteral("宿主拒绝了一批数据");
            return false;
        }
        return true;
    }
};

void *ulogCreate()
{
    auto *ctx = new UlogCtx();
    ctx->state = std::make_unique<UlogState>();
    return ctx;
}

void ulogDestroy(void *p)
{
    delete static_cast<UlogCtx *>(p);
}

int32_t ulogFeed(void *p, const uint8_t *data, uint32_t size, const PsParserSink *sink,
                  PsError *err)
{
    auto *ctx = static_cast<UlogCtx *>(p);
    if (!ctx)
        return -1;
    SpiEmitter emitter;
    emitter.sink = sink;
    QString msg;
    if (!feedUlogBytes(*ctx->state, data, size, emitter, msg))
    {
        ctx->error = msg;
        if (err)
        {
            err->code = -1;
            const QByteArray utf8 = msg.toUtf8();
            const int n = qMin(static_cast<int>(utf8.size()), 255);
            memcpy(err->message, utf8.constData(), static_cast<size_t>(n));
            err->message[n] = '\0';
        }
        return -1;
    }
    return 0;
}

int32_t ulogFinish(void *p, const PsParserSink *sink, PsError *err)
{
    auto *ctx = static_cast<UlogCtx *>(p);
    if (!ctx)
        return -1;
    SpiEmitter emitter;
    emitter.sink = sink;
    QString msg;
    if (!finishUlogBytes(*ctx->state, emitter, msg))
    {
        ctx->error = msg;
        if (err)
        {
            err->code = -1;
            const QByteArray utf8 = msg.toUtf8();
            const int n = qMin(static_cast<int>(utf8.size()), 255);
            memcpy(err->message, utf8.constData(), static_cast<size_t>(n));
            err->message[n] = '\0';
        }
        return -1;
    }
    return 0;
}

void ulogReset(void *p)
{
    auto *ctx = static_cast<UlogCtx *>(p);
    if (ctx)
        ctx->state = std::make_unique<UlogState>();
}

// 嗅探：ULog 的 magic 是 "ULog\x01\x12\x35"（7 字节）。
int32_t ulogSniff(void *, const uint8_t *data, uint32_t size)
{
    if (!data || size < 7)
        return 0;
    static const uint8_t magic[7] = {'U', 'L', 'o', 'g', 0x01, 0x12, 0x35};
    return memcmp(data, magic, 7) == 0 ? 1 : 0;
}

const PsParserInfo *ulogInfo(void *)
{
    static const PsParserInfo info = {
        PS_PARSER_ABI_VERSION,
        "ulog",
        "ULog (ArduPilot)",
        ".ulg,.ulog",
        1,  // can_sniff
        1,  // streaming（段式，可增量喂）
        {0, 0}
    };
    return &info;
}

const PsParserVtable g_ulogVtable = {
    PS_PARSER_ABI_VERSION,
    static_cast<uint32_t>(sizeof(PsParserVtable)),
    ulogCreate,
    ulogDestroy,
    ulogSniff,
    ulogFeed,
    ulogFinish,
    ulogReset,
    ulogInfo,
    nullptr // capabilities
};



// 收尾：把元数据小表（params/facts/events/dropouts）吐出去。
bool flushMetaTables(UlogState &st, Emitter &out, QString &err)
{
    auto &params = st.params;
    auto &facts = st.facts;
    auto &events = st.events;
    auto &dropouts = st.dropouts;

    // params（P 段）与 facts（头信息 + I 段）：两列文本小表。
    // 归并到 SPI 时曾漏掉这两张表的 emitBatch —— 数据攒在 st 里却从未落库，
    // 表现为 ULog 数据集只有 sensor/events（facts 消失）。这里补回。
    if (!params.rows.empty())
    {
        QVector<TimeUs> t(params.rows.size());
        std::vector<std::string> names, values;
        for (size_t i = 0; i < params.rows.size(); ++i)
        {
            t[static_cast<qsizetype>(i)] = static_cast<TimeUs>(i);
            names.push_back(params.rows[i].first);
            values.push_back(params.rows[i].second);
        }
        if (!writeTextTable(out, QStringLiteral("params"), t,
                            QStringLiteral("name"), names,
                            QStringLiteral("value"), values, err))
            return false;
    }
    if (!facts.rows.empty())
    {
        QVector<TimeUs> t(facts.rows.size());
        std::vector<std::string> keys, values;
        for (size_t i = 0; i < facts.rows.size(); ++i)
        {
            t[static_cast<qsizetype>(i)] = static_cast<TimeUs>(i);
            keys.push_back(facts.rows[i].first);
            values.push_back(facts.rows[i].second);
        }
        if (!writeTextTable(out, QStringLiteral("facts"), t,
                            QStringLiteral("key"), keys,
                            QStringLiteral("value"), values, err))
            return false;
    }

    if (!events.empty())
    {
        QVector<TimeUs> t(events.size());
        std::vector<PsFrameColumn> eventsCols(3);
        QByteArray levelBytes, tagBytes;
        QVector<QByteArray> textStore;
        QVector<const char *> textPtrs;
        levelBytes.reserve(static_cast<qsizetype>(events.size()));
        tagBytes.reserve(static_cast<qsizetype>(events.size()) * 2);
        textStore.reserve(static_cast<qsizetype>(events.size()));
        for (size_t i = 0; i < events.size(); ++i)
        {
            t[static_cast<qsizetype>(i)] = static_cast<TimeUs>(std::llround(events[i].t * 1e6));
            const uint8_t lv = events[i].level;
            levelBytes.append(reinterpret_cast<const char *>(&lv), 1);
            const uint16_t tg = events[i].tagged ? events[i].tag : 0;
            tagBytes.append(reinterpret_cast<const char *>(&tg), 2);
            textStore.push_back(QByteArray::fromStdString(events[i].text));
            textPtrs.push_back(textStore.constLast().constData());
        }
        eventsCols[0].name = "level"; eventsCols[0].unit = ""; eventsCols[0].type = PS_VALUE_U8; eventsCols[0].values = levelBytes.constData(); eventsCols[0].nulls = nullptr;
        eventsCols[1].name = "tag"; eventsCols[1].unit = ""; eventsCols[1].type = PS_VALUE_U16; eventsCols[1].values = tagBytes.constData(); eventsCols[1].nulls = nullptr;
        eventsCols[2].name = "text"; eventsCols[2].unit = ""; eventsCols[2].type = PS_VALUE_TEXT; eventsCols[2].values = textPtrs.constData(); eventsCols[2].nulls = nullptr;
        if (!out.emitBatch(QStringLiteral("events"), t.constData(), static_cast<uint32_t>(events.size()), eventsCols, err))
            return false;
    }
    if (!dropouts.empty())
    {
        QVector<TimeUs> t(dropouts.size());
        QByteArray durBytes;
        durBytes.reserve(static_cast<qsizetype>(dropouts.size()) * 2);
        for (size_t i = 0; i < dropouts.size(); ++i)
        {
            t[static_cast<qsizetype>(i)] = static_cast<TimeUs>(std::llround(dropouts[i].first * 1e6));
            const uint16_t d = dropouts[i].second;
            durBytes.append(reinterpret_cast<const char *>(&d), 2);
        }
        std::vector<PsFrameColumn> cols(1);
        cols[0].name = "duration"; cols[0].unit = ""; cols[0].type = PS_VALUE_U16; cols[0].values = durBytes.constData(); cols[0].nulls = nullptr;
        if (!out.emitBatch(QStringLiteral("dropouts"), t.constData(), static_cast<uint32_t>(dropouts.size()), cols, err))
            return false;
    }

    return true;
}

// 喂一批字节：攒段 -> 解析 -> 吐批。
//
// 段缓冲上限 = 最大段长（u16 -> 64KB）+ 段头，与文件行数无关。
bool feedUlogBytes(UlogState &st, const uint8_t *data, uint32_t size, Emitter &out, QString &err)
{
    if (st.failed)
        return false;
    if (!data || size == 0)
        return true;

    st.pending.append(reinterpret_cast<const char *>(data), static_cast<qsizetype>(size));

    // 文件头：magic(7) + version(1) + start_timestamp(8)
    if (!st.headerDone)
    {
        if (st.pending.size() < 16)
            return true;
        const auto *hb = reinterpret_cast<const uint8_t *>(st.pending.constData());
        if (std::memcmp(hb, kMagic, 7) != 0 || hb[7] != 1)
        {
            err = QStringLiteral("magic/版本不匹配（不是 ULog v1）");
            st.failed = true;
            return false;
        }
        uint64_t startUs = 0;
        std::memcpy(&startUs, hb + 8, 8);
        st.facts.rows.emplace_back("start_timestamp_us", std::to_string(startUs));
        st.facts.rows.emplace_back("file_version", std::to_string(hb[7]));
        // 消费掉文件头：注意 QByteArray::remove(pos,len) **不改变长度**
        // （原地抹除），而我们要的是"丢掉已消费的前缀"——所以用 mid()。
        st.pending = st.pending.mid(16);
        st.headerDone = true;
    }

    // 逐段解析：3 字节段头（size u16 + type u8）+ size 字节体。
    while (true)
    {
        if (st.pending.size() < 3)
            break;
        const auto *h = reinterpret_cast<const uint8_t *>(st.pending.constData());
        const uint16_t segSize = static_cast<uint16_t>(h[0] | (h[1] << 8));
        const uint8_t ty = h[2];
        if (st.pending.size() < 3 + segSize)
            break; // 段还没到齐，等下一批
        const uint8_t *p = reinterpret_cast<const uint8_t *>(st.pending.constData()) + 3;
        if (segSize > 0)
        {
            const bool segOk = handleSegment(st, ty, p, segSize, out, err);
            if (!segOk)
            {
                st.failed = true;
                return false;
            }
        }
        // 同上：丢掉已解析的段，剩下的继续攒。
        st.pending = st.pending.mid(3 + segSize);
    }
    return true;
}

// 收尾：冲刷未满的批与元数据表。
bool finishUlogBytes(UlogState &st, Emitter &out, QString &err)
{
    if (st.failed)
        return false;
    if (!st.headerDone)
    {
        err = QStringLiteral("ULog 数据不足（没有文件头）");
        return false;
    }
    // 每个 topic 的尾批
    for (StreamTable &t : st.tables)
        if (!flushTable(t, out, st.multiTopics, err))
            return false;
    if (st.tables.empty())
    {
        err = QStringLiteral("没有可绘制的数值通道（可能只有参数/文本）");
        return false;
    }
    return flushMetaTables(st, out, err);
}

} // namespace

bool parseUlogStream(const QString &path, Dataset &ds, QString &err)
{
    // 旧路径：自己读文件 + 直写 Dataset。
    // 读字节与批的归宿都在这里定；格式解析委托给喂字节式实现。
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        err = QStringLiteral("无法打开 ULog: %1").arg(path);
        return false;
    }

    // 产出端：把 SPI 形状的批转成 Dataset::appendFrame。
    struct DatasetEmitter final : Emitter
    {
        Dataset &ds;
        explicit DatasetEmitter(Dataset &d) : ds(d) {}
        bool emitBatch(const QString &table, const int64_t *timeUs, uint32_t rows,
                       const std::vector<PsFrameColumn> &cols, QString &err) override
        {
            QVector<FrameColumn> frame;
            frame.reserve(static_cast<qsizetype>(cols.size()));
            for (const PsFrameColumn &c : cols)
            {
                FrameColumn f;
                f.name = QString::fromUtf8(c.name);
                f.unit = QString::fromUtf8(c.unit);
                f.type = fromPsValueType(c.type);
                f.values = c.values;
                f.nulls = c.nulls;
                frame.push_back(f);
            }
            return ds.appendFrame(table, timeUs, static_cast<int>(rows), frame, &err);
        }
    } datasetEmitter(ds);

    UlogState st;
    QByteArray buf;
    buf.resize(256 * 1024);
    while (true)
    {
        const qint64 n = file.read(buf.data(), buf.size());
        if (n <= 0)
            break;
        if (!feedUlogBytes(st, reinterpret_cast<const uint8_t *>(buf.constData()),
                           static_cast<uint32_t>(n), datasetEmitter, err))
        {
            file.close();
            return false;
        }
    }
    file.close();
    return finishUlogBytes(st, datasetEmitter, err);
}

const PsParserInfo &parserInfo()
{
    static const PsParserInfo info = {
        PS_PARSER_ABI_VERSION,
        "ulog",
        "ULog (ArduPilot)",
        ".ulg,.ulog",
        1,
        1,
        {0, 0}
    };
    return info;
}

const PsParserVtable *parserVtable()
{
    return &g_ulogVtable;
}

} // namespace nextpilot::plotscope::ext::ulog

// 编译期自注册：ULog 以标准 SPI 解析器的身份进入 ParserHub。
// 宿主于是能用统一管道（Source → IngestPipeline → parser）处理它，
// 而不必为它单独写一条数据通路。
namespace
{
struct UlogParserRegistration
{
    static const PsParserInfo &parserInfo() { return nextpilot::plotscope::ext::ulog::parserInfo(); }
    static const PsParserVtable *parserVtable() { return nextpilot::plotscope::ext::ulog::parserVtable(); }
};
const bool g_ulogParserReg_ =
    ::nextpilot::plotscope::parser::ParserHub::registerParser(UlogParserRegistration::parserInfo(),
                                               UlogParserRegistration::parserVtable(),
                                               QStringLiteral("builtin"));
}
