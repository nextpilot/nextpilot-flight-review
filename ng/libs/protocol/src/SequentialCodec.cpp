#include "protocol/SequentialCodec.h"
#include "protocol/ProtocolSpec.h"
#include "protocol/ProtocolDocument.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace nextpilot::plotscope::protocol;
using nextpilot::plotscope::protocol::ProtocolChannel;

/* ---------------- 基础读取 ---------------- */

inline std::string numberKey(double v)
{
    std::ostringstream os;
    os << std::setprecision(17) << v;
    return "n:" + os.str();
}

inline std::string stringKey(const std::string &v) { return "s:" + v; }

uint64_t readUInt(const uint8_t *p, uint32_t n, bool be)
{
    uint64_t v = 0;
    if (be)
    {
        for (uint32_t i = 0; i < n; ++i)
            v = (v << 8) | p[i];
    }
    else
    {
        for (uint32_t i = 0; i < n; ++i)
            v |= static_cast<uint64_t>(p[i]) << (8 * i);
    }
    return v;
}

// 按段类型读一个元素，保持原生类型（不经过 double）。
TypedValue readTyped(const uint8_t *p, const TypeInfo &ti, bool be)
{
    switch (ti.type)
    {
    case ValueType::U8: return TypedValue::fromU(p[0], ValueType::U8);
    case ValueType::I8: return TypedValue::fromI(static_cast<int8_t>(p[0]), ValueType::I8);
    case ValueType::U16: return TypedValue::fromU(readUInt(p, 2, be), ValueType::U16);
    case ValueType::I16:
        return TypedValue::fromI(static_cast<int16_t>(readUInt(p, 2, be)), ValueType::I16);
    case ValueType::U32: return TypedValue::fromU(readUInt(p, 4, be), ValueType::U32);
    case ValueType::I32:
        return TypedValue::fromI(static_cast<int32_t>(readUInt(p, 4, be)), ValueType::I32);
    case ValueType::U64: return TypedValue::fromU(readUInt(p, 8, be), ValueType::U64);
    case ValueType::I64:
        return TypedValue::fromI(static_cast<int64_t>(readUInt(p, 8, be)), ValueType::I64);
    case ValueType::F32:
    {
        const uint32_t raw = static_cast<uint32_t>(readUInt(p, 4, be));
        float f = 0.0f;
        std::memcpy(&f, &raw, sizeof(f));
        return TypedValue::fromF(static_cast<double>(f), ValueType::F32);
    }
    case ValueType::F64:
    {
        const uint64_t raw = readUInt(p, 8, be);
        double d = 0.0;
        std::memcpy(&d, &raw, sizeof(d));
        return TypedValue::fromF(d, ValueType::F64);
    }
    case ValueType::Bool: return TypedValue::fromU(p[0] ? 1u : 0u, ValueType::Bool);
    case ValueType::Bcd:
    {
        // 每字节两个十进制位，按大小端顺序组成十进制整数。
        uint64_t v = 0;
        for (uint32_t i = 0; i < ti.size; ++i)
        {
            const uint8_t b = be ? p[i] : p[ti.size - 1 - i];
            v = v * 100 + static_cast<uint64_t>(b >> 4) * 10 + (b & 0x0F);
        }
        return TypedValue::fromU(v, ValueType::Bcd);
    }
    case ValueType::Text: return TypedValue::fromText(std::string());
    }
    return TypedValue{};
}

// 物理换算：scale=1 且 offset=0 时保持原生类型，否则升到 f64。
TypedValue physOf(const TypedValue &raw, const SegSpec &seg)
{
    if (seg.scale == 1.0 && seg.offset == 0.0)
        return raw;
    return TypedValue::fromF(raw.asDouble() * seg.scale + seg.offset, ValueType::F64);
}

/* ---------------- 校验 ---------------- */

uint32_t reflectBits(uint32_t v, int width)
{
    uint32_t r = 0;
    for (int i = 0; i < width; ++i)
    {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

uint32_t crcGeneric(const uint8_t *data, size_t n, uint32_t poly, uint32_t init,
                    bool refin, bool refout, uint32_t xorout, int width)
{
    const uint32_t mask = (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
    const uint32_t top = 1u << (width - 1);
    uint32_t crc = init & mask;
    for (size_t i = 0; i < n; ++i)
    {
        if (refin)
        {
            crc ^= data[i];
            for (int b = 0; b < 8; ++b)
                crc = (crc & 1u) ? ((crc >> 1) ^ reflectBits(poly, width)) : (crc >> 1);
        }
        else
        {
            crc ^= static_cast<uint32_t>(data[i]) << (width - 8);
            for (int b = 0; b < 8; ++b)
                crc = (crc & top) ? ((crc << 1) ^ poly) : (crc << 1);
        }
        crc &= mask;
    }
    if (refout != refin)
        crc = reflectBits(crc, width);
    return (crc ^ xorout) & mask;
}

uint32_t sequenceValue(const CheckSpec &c, const std::vector<uint8_t> &buf)
{
    if (c.op == "sum8" || c.op == "sum16")
    {
        uint32_t sum = 0;
        for (uint8_t b : buf)
            sum += b;
        return static_cast<uint32_t>(c.op == "sum8" ? (sum & 0xFF) : (sum & 0xFFFF));
    }
    if (c.op == "xor8")
    {
        uint32_t x = 0;
        for (uint8_t b : buf)
            x ^= b;
        return x;
    }
    if (c.op == "parity")
    {
        uint32_t x = 0;
        for (uint8_t b : buf)
            x ^= b;
        return (x & 1u) ? 1u : 0u;
    }
    return crcGeneric(buf.data(), buf.size(), c.poly, c.init, c.refin, c.refout, c.xorout,
                      c.width > 0 ? c.width * 8 : 16);
}

int compareNumeric(const TypedValue &a, const TypedValue &b, std::string &err)
{
    if (a.type == ValueType::Text || b.type == ValueType::Text)
    {
        err = "文本值不能参与数值比较";
        return 2;
    }
    const bool af = isFloatType(a.type), bf = isFloatType(b.type);
    if (af != bf)
    {
        err = "整型与浮点不能直接比较，需要显式 toF64/toI64";
        return 2;
    }
    if (af)
        return (a.f < b.f) ? -1 : (a.f > b.f ? 1 : 0);
    if (a.isSigned() || b.isSigned())
    {
        const int64_t x = a.asI64(), y = b.asI64();
        return (x < y) ? -1 : (x > y ? 1 : 0);
    }
    const uint64_t x = a.asU64(), y = b.asU64();
    return (x < y) ? -1 : (x > y ? 1 : 0);
}

struct RtSeg
{
    const SegSpec *spec = nullptr;
    uint32_t offset = 0;
    uint32_t bytes = 0;
    uint32_t count = 0;
    TypedVec raw;
    TypedVec phys;
};

bool evalCount(const SegSpec &seg, const EvalCtx &ctx, uint32_t &count, uint32_t &bytes,
               std::string &err)
{
    int64_t v = 0;
    if (!evalExprInt(seg.countExpr, ctx, v, err))
        return false;
    if (v <= 0 || v > 1000000)
    {
        err = "段 " + seg.name + " 的 count 非法";
        return false;
    }
    count = static_cast<uint32_t>(v);
    bytes = count * seg.type.size;
    if (bytes == 0 || bytes > (1u << 20))
    {
        err = "段 " + seg.name + " 字节数非法";
        return false;
    }
    return true;
}

bool checkFrame(const std::vector<RtSeg> &fields, const std::map<std::string, RtSeg *> &byName,
                const std::map<std::string, std::pair<uint32_t, uint32_t>> &pseudo,
                const std::vector<std::string> &order, const uint8_t *frame,
                const EvalCtx &rawCtx, std::string &err)
{
    for (const RtSeg &rt : fields)
    {
        const SegSpec &seg = *rt.spec;
        if (seg.expectExpr)
        {
            TypedValue want;
            if (!evalExpr(seg.expectExpr, rawCtx, want, err))
                return false;
            for (std::size_t i = 0; i < rt.raw.size(); ++i)
            {
                const int cmp = compareNumeric(rt.raw.at(i), want, err);
                if (cmp == 2)
                    return false;
                if (cmp != 0)
                {
                    err = "段 " + seg.name + " 常量不匹配";
                    return false;
                }
            }
        }

        for (const CheckSpec &c : seg.checks)
        {
            const bool seqOp = (c.op == "crc8" || c.op == "crc16" || c.op == "crc32" ||
                                c.op == "sum8" || c.op == "sum16" || c.op == "xor8" || c.op == "parity");
            if (seqOp)
            {
                if (rt.raw.size() != 1 || rt.bytes != static_cast<uint32_t>(c.width))
                {
                    err = "段 " + seg.name + " 校验宽度与算子不匹配";
                    return false;
                }
                std::vector<uint8_t> buf;
                std::set<std::string> seen;
                for (const std::string &token : c.over)
                {
                    std::vector<std::string> names;
                    const size_t colon = token.find(':');
                    if (colon != std::string::npos)
                    {
                        const std::string a = token.substr(0, colon);
                        const std::string b = token.substr(colon + 1);
                        auto ia = std::find(order.begin(), order.end(), a);
                        auto ib = std::find(order.begin(), order.end(), b);
                        if (ia == order.end() || ib == order.end() || ia > ib)
                        {
                            err = "over 区间非法: " + token;
                            return false;
                        }
                        names.assign(ia, ib + 1);
                    }
                    else
                    {
                        names.push_back(token);
                    }
                    for (const std::string &n : names)
                    {
                        if (!seen.insert(n).second)
                            continue;
                        auto pit = pseudo.find(n);
                        if (pit != pseudo.end())
                        {
                            const uint8_t *p = frame + pit->second.first;
                            buf.insert(buf.end(), p, p + pit->second.second);
                            continue;
                        }
                        auto it = byName.find(n);
                        if (it == byName.end())
                        {
                            err = "over 引用未知段: " + n;
                            return false;
                        }
                        const uint8_t *p = frame + it->second->offset;
                        buf.insert(buf.end(), p, p + it->second->bytes);
                    }
                }
                const uint32_t got = sequenceValue(c, buf);
                const uint64_t want = rt.raw.at(0).asU64();
                if (static_cast<uint32_t>(want) != got)
                {
                    err = "段 " + seg.name + " 校验失败";
                    return false;
                }
                continue;
            }

            for (std::size_t i = 0; i < rt.raw.size(); ++i)
            {
                bool ok = false;
                const TypedValue &v = rt.raw.at(i);
                TypedValue rhs;
                int cmp = 0;
                switch (c.op[0])
                {
                case 'e':
                    if (c.op == "eq")
                    {
                        if (!evalExpr(c.rhs, rawCtx, rhs, err))
                            return false;
                        cmp = compareNumeric(v, rhs, err);
                        if (cmp == 2) return false;
                        ok = (cmp == 0);
                    }
                    break;
                case 'n':
                    if (c.op == "ne")
                    {
                        if (!evalExpr(c.rhs, rawCtx, rhs, err))
                            return false;
                        cmp = compareNumeric(v, rhs, err);
                        if (cmp == 2) return false;
                        ok = (cmp != 0);
                    }
                    break;
                case 'l':
                    if (c.op == "lt" || c.op == "le")
                    {
                        if (!evalExpr(c.rhs, rawCtx, rhs, err))
                            return false;
                        cmp = compareNumeric(v, rhs, err);
                        if (cmp == 2) return false;
                        ok = (c.op == "lt") ? (cmp < 0) : (cmp <= 0);
                    }
                    break;
                case 'g':
                    if (c.op == "gt" || c.op == "ge")
                    {
                        if (!evalExpr(c.rhs, rawCtx, rhs, err))
                            return false;
                        cmp = compareNumeric(v, rhs, err);
                        if (cmp == 2) return false;
                        ok = (c.op == "gt") ? (cmp > 0) : (cmp >= 0);
                    }
                    break;
                case 'i':
                    if (c.op == "in")
                    {
                        for (const TypedValue &iv : c.values)
                        {
                            cmp = compareNumeric(v, iv, err);
                            if (cmp == 2) return false;
                            if (cmp == 0) { ok = true; break; }
                        }
                    }
                    break;
                case 'r':
                    if (c.op == "range")
                    {
                        TypedValue lo, hi;
                        if (!evalExpr(c.rhs, rawCtx, lo, err) ||
                            !evalExpr(c.rhs2, rawCtx, hi, err))
                            return false;
                        const int cl = compareNumeric(v, lo, err);
                        const int ch = compareNumeric(v, hi, err);
                        if (cl == 2 || ch == 2) return false;
                        ok = (cl >= 0 && ch <= 0);
                    }
                    break;
                case 'm':
                    if (c.op == "mask")
                    {
                        TypedValue m, expected;
                        if (!evalExpr(c.rhs, rawCtx, m, err) ||
                            !evalExpr(c.rhs2, rawCtx, expected, err))
                            return false;
                        if (isFloatType(v.type) || isFloatType(m.type) || isFloatType(expected.type))
                        {
                            err = "mask 只支持整型";
                            return false;
                        }
                        ok = ((v.asU64() & m.asU64()) == expected.asU64());
                    }
                    break;
                default:
                    break;
                }
                if (!ok)
                {
                    err = "段 " + seg.name + " 校验失败";
                    return false;
                }
            }
        }
    }
    return true;
}

bool matchVariant(const SeriesSpec &series, const std::string &selectorKey,
                  const VariantSpec *&variant, std::string &)
{
    variant = nullptr;
    for (const VariantSpec &v : series.variants)
        for (const std::string &id : v.keys)
            if (id == selectorKey)
            {
                variant = &v;
                return true;
            }
    return true;
}

/* ---------------- 读一帧（唯一内核） ---------------- */

struct FrameRead
{
    bool ok = false;
    std::string err;
    std::vector<RtSeg> fields;
    EvalCtx rawCtx;
    EvalCtx physCtx;
    std::map<std::string, std::pair<uint32_t, uint32_t>> pseudo;
    std::string variantName;
    uint32_t consumed = 0;
};

// 读一帧：从 p 起、最多 avail 字节；consumed 为整帧占用字节。
FrameRead readFrame(const ProtocolDocument &tpl, const uint8_t *p, size_t avail)
{
    FrameRead r;
    std::vector<std::string> order;
    for (const SegSpec &s : tpl.segments)
        order.push_back(s.name);

    uint32_t pos = 0;
    bool payloadSeen = false;
    uint32_t payloadOff = 0, payloadBytes = 0, payloadCount = 0;

    for (const SegSpec &seg : tpl.segments)
    {
        uint32_t count = 0, nbytes = 0;
        if (!evalCount(seg, r.rawCtx, count, nbytes, r.err))
            return r;

        const bool isPayload = tpl.series.enabled && seg.name == tpl.series.payload;
        if (static_cast<uint64_t>(pos) + nbytes > avail)
        {
            r.err = "段 " + seg.name + " 超出帧长度：需要 " + std::to_string(nbytes) +
                    " B，剩余 " + std::to_string(avail - pos) + " B";
            return r;
        }

        if (isPayload)
        {
            payloadSeen = true;
            payloadOff = pos;
            payloadBytes = nbytes;
            payloadCount = count;
            TypedVec col(ValueType::U8);
            col.reserve(nbytes);
            for (uint32_t i = 0; i < nbytes; ++i)
                col.push(TypedValue::fromU(p[pos + i], ValueType::U8));
            r.rawCtx.values[seg.name] = col;
            r.physCtx.values[seg.name] = col;
            r.pseudo[seg.name] = {payloadOff, nbytes};
            pos += nbytes;
            continue;
        }

        RtSeg rt;
        rt.spec = &seg;
        rt.offset = pos;
        rt.bytes = nbytes;
        rt.count = count;
        const uint8_t *sp = p + pos;

        if (seg.type.type == ValueType::Text)
        {
            std::string txt;
            txt.reserve(nbytes);
            for (uint32_t i = 0; i < nbytes; ++i)
                txt.push_back(static_cast<char>(sp[i]));
            rt.raw.reset(ValueType::Text);
            rt.raw.push(TypedValue::fromText(txt));
            rt.phys = rt.raw;
        }
        else
        {
            rt.raw.reset(seg.type.type);
            rt.raw.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                const TypedValue v = readTyped(sp + static_cast<size_t>(i) * seg.type.size,
                                               seg.type, seg.bigEndian);
                rt.raw.push(v);
            }
            const TypedValue firstPhys = rt.raw.size() ? physOf(rt.raw.at(0), seg) : TypedValue{};
            rt.phys.reset(firstPhys.type);
            rt.phys.reserve(count);
            for (std::size_t i = 0; i < rt.raw.size(); ++i)
                rt.phys.push(physOf(rt.raw.at(i), seg));
        }
        r.rawCtx.values[seg.name] = rt.raw;
        r.physCtx.values[seg.name] = rt.phys;
        r.fields.push_back(std::move(rt));
        pos += nbytes;
    }

    if (tpl.series.enabled)
    {
        if (!payloadSeen)
        {
            r.err = "series.payload 未找到";
            return r;
        }
        std::string selKey;
        auto sit = r.rawCtx.values.find(tpl.series.selector ? tpl.series.selector->name : std::string());
        if (tpl.series.selector && tpl.series.selector->kind == Expr::Ref && !tpl.series.selector->b &&
            sit != r.rawCtx.values.end() && sit->second.type == ValueType::Text)
        {
            selKey = stringKey(sit->second.at(0).s);
        }
        else
        {
            int64_t sel = 0;
            if (!tpl.series.selector || !evalExprInt(tpl.series.selector, r.rawCtx, sel, r.err))
            {
                if (r.err.empty())
                    r.err = "series.selector 缺失";
                return r;
            }
            selKey = numberKey(static_cast<double>(sel));
        }
        const VariantSpec *var = nullptr;
        matchVariant(tpl.series, selKey, var, r.err);
        if (!var)
        {
            if (tpl.series.fallback == "reject")
            {
                r.err = "series 未命中：" + selKey;
                return r;
            }
            r.consumed = pos;
            r.ok = true;
            return r;
        }

        r.variantName = var->name;
        uint32_t vpos = payloadOff;
        for (const SegSpec &seg : var->segments)
        {
            uint32_t count = 0, nbytes = 0;
            if (!evalCount(seg, r.rawCtx, count, nbytes, r.err))
                return r;
            if (static_cast<uint64_t>(vpos) + nbytes > avail)
            {
                r.err = "变体段 " + seg.name + " 超出帧长度";
                return r;
            }
            RtSeg rt;
            rt.spec = &seg;
            rt.offset = vpos;
            rt.bytes = nbytes;
            rt.count = count;
            const uint8_t *sp = p + vpos;
            rt.raw.reset(seg.type.type);
            rt.raw.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
                rt.raw.push(readTyped(sp + static_cast<size_t>(i) * seg.type.size, seg.type,
                                      seg.bigEndian));
            rt.phys.reset(rt.raw.size() ? physOf(rt.raw.at(0), seg).type : seg.type.type);
            for (std::size_t i = 0; i < rt.raw.size(); ++i)
                rt.phys.push(physOf(rt.raw.at(i), seg));
            r.rawCtx.values[seg.name] = rt.raw;
            r.physCtx.values[seg.name] = rt.phys;
            r.fields.push_back(std::move(rt));
            vpos += nbytes;
        }
        if (vpos != payloadOff + payloadBytes)
        {
            r.err = "变体段总字节数不等于 payload";
            return r;
        }
    }

    std::sort(r.fields.begin(), r.fields.end(),
              [](const RtSeg &a, const RtSeg &b) { return a.offset < b.offset; });

    std::map<std::string, RtSeg *> byName;
    for (RtSeg &rt : r.fields)
        byName[rt.spec->name] = &rt;

    if (!checkFrame(r.fields, byName, r.pseudo, order, p, r.rawCtx, r.err))
        return r;

    /* length 反算 */
    for (RtSeg &rt : r.fields)
    {
        const SegSpec &seg = *rt.spec;
        if (!seg.length.enabled)
            continue;
        const int64_t declared = rt.raw.size() ? rt.raw.at(0).asI64() : 0;
        int64_t actual = 0;
        if (seg.length.expr)
        {
            if (!evalExprInt(seg.length.expr, r.rawCtx, actual, r.err))
                return r;
        }
        else
        {
            for (const std::string &fn : seg.length.fields)
            {
                auto pit = r.pseudo.find(fn);
                if (pit != r.pseudo.end())
                {
                    actual += (seg.length.unit == "bytes")
                                  ? static_cast<int64_t>(pit->second.second)
                                  : static_cast<int64_t>(payloadCount ? payloadCount
                                                                      : pit->second.second);
                    continue;
                }
                auto it = byName.find(fn);
                if (it == byName.end())
                {
                    r.err = "length 引用未知段: " + fn;
                    return r;
                }
                actual += (seg.length.unit == "bytes") ? static_cast<int64_t>(it->second->bytes)
                                                       : static_cast<int64_t>(it->second->count);
            }
        }
        if (declared != actual)
        {
            r.err = "长度段 " + seg.name + " 与 length 不一致";
            return r;
        }
    }

    r.consumed = pos;
    r.ok = true;
    return r;
}

/* ---------------- 时间戳（整数 ticks） ---------------- */

const int64_t kTicksPerSecond = 1000000; // 统一微秒刻度

bool unitTicks(int64_t raw, const std::string &unit, int64_t &ticks, std::string &err)
{
    switch (unit[0])
    {
    case 's':
        if (raw > (INT64_MAX / kTicksPerSecond) || raw < (INT64_MIN / kTicksPerSecond))
        {
            err = "timestamp 溢出（秒）";
            return false;
        }
        ticks = raw * kTicksPerSecond;
        return true;
    case 'm':
        ticks = raw * 1000;
        return true;
    case 'u':
        ticks = raw;
        return true;
    case 'n':
        ticks = raw / 1000;
        return true;
    default:
        err = "未知时间单位: " + unit;
        return false;
    }
}

} // namespace

bool nextpilot::plotscope::protocol::looksSequentialProtocol(const QByteArray &json)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    return root.contains(QStringLiteral("segments"));
}

/* ---------------- 预览 ---------------- */

namespace
{

QString numList(const TypedVec &v)
{
    QStringList out;
    const int n = std::min<int>(static_cast<int>(v.size()), 12);
    for (int i = 0; i < n; ++i)
    {
        const TypedValue x = v.at(static_cast<size_t>(i));
        if (x.type == ValueType::Text)
            out << QString::fromStdString(x.s);
        else if (isFloatType(x.type))
            out << QString::number(x.f, 'g', 8);
        else if (x.isSigned())
            out << QString::number(static_cast<qlonglong>(x.i));
        else
            out << QString::number(static_cast<qulonglong>(x.u));
    }
    if (static_cast<int>(v.size()) > n)
        out << QStringLiteral("…");
    return out.join(QStringLiteral(", "));
}

} // namespace

nextpilot::plotscope::protocol::SequentialFramePreview nextpilot::plotscope::protocol::previewSequentialFrame(
    const QByteArray &frame, const ProtocolDocument &tpl)
{
    SequentialFramePreview res;
    if (frame.isEmpty())
    {
        res.error = QStringLiteral("样例帧为空");
        return res;
    }

    FrameRead fr = readFrame(tpl, reinterpret_cast<const uint8_t *>(frame.constData()),
                             static_cast<size_t>(frame.size()));
    if (!fr.ok)
    {
        res.error = QString::fromStdString(fr.err);
        for (const RtSeg &rt : fr.fields)
            res.lines << QStringLiteral("%1 @ %2  %3 B")
                             .arg(QString::fromStdString(rt.spec->name))
                             .arg(rt.offset)
                             .arg(rt.bytes);
        return res;
    }

    res.ok = true;
    res.lines << QStringLiteral("帧 %1 B · %2%3")
                     .arg(frame.size())
                     .arg(QString::fromStdString(tpl.name))
                     .arg(fr.variantName.empty()
                              ? QString()
                              : QStringLiteral(" · 变体 %1").arg(QString::fromStdString(fr.variantName)));
    for (const RtSeg &rt : fr.fields)
    {
        const SegSpec &seg = *rt.spec;
        if (seg.type.type == ValueType::Text)
        {
            const QString text = rt.raw.size()
                                     ? QString::fromStdString(rt.raw.at(0).s)
                                     : QString();
            res.lines << QStringLiteral("%1 @ %2  char %3 B  \"%4\"")
                             .arg(QString::fromStdString(seg.name))
                             .arg(rt.offset)
                             .arg(rt.bytes)
                             .arg(text);
        }
        else
        {
            QString line = QStringLiteral("%1 @ %2  %3 × %4 = %5 B  raw=[%6]")
                               .arg(QString::fromStdString(seg.name))
                               .arg(rt.offset)
                               .arg(rt.count)
                               .arg(seg.type.size)
                               .arg(rt.bytes)
                               .arg(numList(rt.raw));
            if (seg.scale != 1.0 || seg.offset != 0.0)
                line += QStringLiteral("  phys=[%1]").arg(numList(rt.phys));
            if (!seg.unit.empty())
                line += QStringLiteral(" %1").arg(QString::fromStdString(seg.unit));
            res.lines << line;
        }
    }

    if (tpl.hasTimestamp && tpl.timestampExpr)
    {
        int64_t rawTs = 0;
        const EvalCtx &ctx = tpl.timestampRaw ? fr.rawCtx : fr.physCtx;
        std::string err;
        int64_t ticks = 0;
        if (evalExprInt(tpl.timestampExpr, ctx, rawTs, err) &&
            unitTicks(rawTs, tpl.timestampUnit, ticks, err))
        {
            res.lines << QStringLiteral("timestamp：%1 s（%2 ticks）")
                             .arg(QString::number(static_cast<double>(ticks) / kTicksPerSecond,
                                                  'f', 6))
                             .arg(QString::number(static_cast<qlonglong>(ticks)));
        }
        else
        {
            res.lines << QStringLiteral("timestamp 求值失败：%1")
                             .arg(QString::fromStdString(err));
        }
    }
    return res;
}

/* ---------------- 生产解码 ---------------- */

nextpilot::plotscope::protocol::ProtocolDecodeResult nextpilot::plotscope::protocol::decodeSequential(
    const QByteArray &bytes, const ProtocolDocument &tpl)
{
    ProtocolDecodeResult result;
    result.ticksPerSecond = kTicksPerSecond;

    const uint8_t *data = reinterpret_cast<const uint8_t *>(bytes.constData());
    const size_t total = static_cast<size_t>(bytes.size());
    std::map<std::string, ProtocolChannel> chans;
    size_t off = 0;
    int64_t frameIndex = 0;
    bool haveBase = false;
    int64_t baseTs = 0;
    uint64_t rejected = 0;
    uint64_t warnings = 0;

    while (off + 1 < total)
    {
        FrameRead fr = readFrame(tpl, data + off, total - off);
        if (!fr.ok)
        {
            if (std::getenv("PS_SEQ_DEBUG"))
                std::fprintf(stderr, "reject off=%zu: %s\n", off, fr.err.c_str());
            ++off;
            ++rejected;
            if (rejected > 1000000)
                break;
            continue;
        }
        if (fr.consumed == 0)
            break;

        // 无时间戳时按帧序给轴（1 帧 = 1 秒刻度），仍是整数 ticks。
        int64_t ts = frameIndex * kTicksPerSecond;
        if (tpl.hasTimestamp)
        {
            int64_t rawTs = 0;
            std::string err;
            const EvalCtx &ctx = tpl.timestampRaw ? fr.rawCtx : fr.physCtx;
            if (!evalExprInt(tpl.timestampExpr, ctx, rawTs, err))
            {
                if (tpl.timestampFallback == "index")
                {
                    rawTs = frameIndex;
                }
                else
                {
                    ++off;
                    ++rejected;
                    continue;
                }
            }
            if (tpl.timestampOrigin == "first_frame")
            {
                if (!haveBase)
                {
                    baseTs = rawTs;
                    haveBase = true;
                }
                rawTs -= baseTs; // 先整数相减，再换算单位
            }
            int64_t ticks = 0;
            if (!unitTicks(rawTs, tpl.timestampUnit, ticks, err))
            {
                ++off;
                ++rejected;
                continue;
            }
            ts = ticks;
        }

        bool anyChannel = false;
        for (RtSeg &rt : fr.fields)
        {
            const SegSpec &seg = *rt.spec;
            if (seg.emitMode != EmitMode::Channel)
                continue;
            if (seg.type.type == ValueType::Text)
                continue;
            // series 的 payload 是容器，其内容由变体段解释，本身不出通道。
            const bool isPayload = tpl.series.enabled && seg.name == tpl.series.payload;
            if (isPayload && !fr.variantName.empty())
                continue;
            if (rt.raw.size() > 256)
                ++warnings;
            const uint32_t limit = std::min<uint32_t>(rt.count, 256);
            const std::string prefix = "frame." + seg.prefix;
            // 通道值列的类型由段类型决定：新建时先把列类型钉住，
            // 否则默认的 F64 列会把每次 push 都降级成 double。
            const TypedVec &src = rt.phys.size() ? rt.phys : rt.raw;
            if (rt.count == 1)
            {
                ProtocolChannel &ch = chans[prefix + seg.name];
                if (ch.t.empty())
                {
                    ch.name = prefix + seg.name;
                    ch.unit = seg.unit;
                    ch.v.reset(src.type);
                }
                ch.t.push_back(ts);
                ch.v.push(src.at(0));
                anyChannel = true;
            }
            else
            {
                for (uint32_t i = 0; i < limit; ++i)
                {
                    const std::string name = prefix + seg.name + "[" + std::to_string(i) + "]";
                    ProtocolChannel &ch = chans[name];
                    if (ch.t.empty())
                    {
                        ch.name = name;
                        ch.unit = seg.unit;
                        ch.v.reset(src.type);
                    }
                    ch.t.push_back(ts);
                    ch.v.push(src.at(i));
                    anyChannel = true;
                }
            }
        }
        if (!anyChannel)
        {
            ++off;
            ++rejected;
            continue;
        }

        off += fr.consumed;
        frameIndex += 1;
    }

    result.rejectedFrames = rejected;
    result.warnings = warnings;
    if (chans.empty())
    {
        result.error = QStringLiteral("顺序协议没有可绘制通道；检查 emit/count 或协议内容");
        return result;
    }

    for (auto &kv : chans)
        result.channels.push_back(std::move(kv.second));
    result.ok = true;
    return result;
}

nextpilot::plotscope::protocol::ProtocolDecodeResult nextpilot::plotscope::protocol::decodeSequentialJson(
    const QByteArray &bytes, const QByteArray &templateJson)
{
    ProtocolDecodeResult result;
    result.ticksPerSecond = kTicksPerSecond;
    QJsonParseError perr;
    const QJsonObject root = QJsonDocument::fromJson(templateJson, &perr).object();
    if (perr.error != QJsonParseError::NoError)
    {
        result.error = QStringLiteral("协议 JSON 解析失败: ") + perr.errorString();
        return result;
    }
    const QStringList issues = nextpilot::plotscope::protocol::validate(root);
    if (!issues.isEmpty())
    {
        result.error = QStringLiteral("协议校验失败: ") + issues.join(QStringLiteral("；"));
        return result;
    }
    ProtocolDocument tpl;
    std::string e;
    if (!parseDocument(root, tpl, e))
    {
        result.error = QString::fromStdString(e);
        return result;
    }
    return decodeSequential(bytes, tpl);
}
