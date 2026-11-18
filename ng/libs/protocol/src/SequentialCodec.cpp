#include "protocol/SequentialCodec.h"
#include "protocol/ProtocolSpec.h"
#include "protocol/ProtocolDocument.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace ps::protocol;
using ps::protocol::ProtocolChannel;


/* ---------------- 表达式 ---------------- */

/* ---------------- 模板 ---------------- */

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

double readElem(const uint8_t *p, const TypeInfo &ti, bool be)
{
    switch (ti.type)
    {
    case FType::U8: return double(p[0]);
    case FType::STR: return double(p[0]);
    case FType::I8: return double(static_cast<int8_t>(p[0]));
    case FType::U16: return double(readUInt(p, 2, be));
    case FType::I16:
    {
        uint16_t raw = static_cast<uint16_t>(readUInt(p, 2, be));
        return double(static_cast<int16_t>(raw));
    }
    case FType::U32: return double(readUInt(p, 4, be));
    case FType::I32:
    {
        uint32_t raw = static_cast<uint32_t>(readUInt(p, 4, be));
        return double(static_cast<int32_t>(raw));
    }
    case FType::U64: return double(readUInt(p, 8, be));
    case FType::I64:
    {
        uint64_t raw = readUInt(p, 8, be);
        return double(static_cast<int64_t>(raw));
    }
    case FType::F32:
    {
        uint32_t raw = static_cast<uint32_t>(readUInt(p, 4, be));
        float f = 0;
        std::memcpy(&f, &raw, sizeof(f));
        return double(f);
    }
    case FType::F64:
    {
        uint64_t raw = readUInt(p, 8, be);
        double d = 0;
        std::memcpy(&d, &raw, sizeof(d));
        return d;
    }
    case FType::BOOL: return p[0] ? 1.0 : 0.0;
    case FType::BCD:
    {
        uint8_t b = p[0];
        return double((b >> 4) * 10 + (b & 0x0F));
    }
    }
    return 0;
}

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
        uint64_t sum = 0;
        for (uint8_t b : buf)
            sum += b;
        return static_cast<uint32_t>(c.op == "sum8" ? (sum & 0xFF) : (sum & 0xFFFF));
    }
    if (c.op == "xor8")
    {
        uint8_t x = 0;
        for (uint8_t b : buf)
            x ^= b;
        return x;
    }
    if (c.op == "parity")
    {
        uint8_t x = 0;
        for (uint8_t b : buf)
            x ^= b;
        return (x & 1u) ? 1u : 0u;
    }
    return crcGeneric(buf.data(), buf.size(), c.poly, c.init, c.refin, c.refout, c.xorout,
                      c.width > 0 ? c.width * 8 : 16);
}




struct RtSeg
{
    const SegSpec *spec = nullptr;
    uint32_t offset = 0;
    uint32_t bytes = 0;
    uint32_t count = 0;
    std::vector<double> raw;
    std::vector<double> phys;
};

bool evalSize(const SegSpec &seg, const EvalCtx &ctx, uint32_t &count, uint32_t &bytes,
              std::string &err)
{
    double v = 0;
    if (!evalExpr(seg.sizeExpr, ctx, v, err))
        return false;
    if (!(v > 0) || v != std::trunc(v) || v > 1000000)
    {
        err = "段 " + seg.name + " size 非法";
        return false;
    }
    count = static_cast<uint32_t>(v);
    bytes = count * seg.type.size;
    if (bytes == 0 || bytes > 1u << 20)
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
        if (seg.hasValue)
        {
            double want = 0;
            if (!evalExpr(seg.valueExpr, rawCtx, want, err))
                return false;
            for (double v : rt.raw)
                if (v != want)
                {
                    err = "段 " + seg.name + " 常量不匹配";
                    return false;
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
                const double want = rt.raw[0];
                if (static_cast<uint32_t>(want) != got)
                {
                    err = "段 " + seg.name + " 校验失败";
                    return false;
                }
                continue;
            }

            for (double v : rt.raw)
            {
                bool ok = false;
                double rhs = 0;
                if (c.op == "eq")
                    ok = evalExpr(c.rhs, rawCtx, rhs, err) && v == rhs;
                else if (c.op == "ne")
                    ok = evalExpr(c.rhs, rawCtx, rhs, err) && v != rhs;
                else if (c.op == "lt")
                    ok = evalExpr(c.rhs, rawCtx, rhs, err) && v < rhs;
                else if (c.op == "le")
                    ok = evalExpr(c.rhs, rawCtx, rhs, err) && v <= rhs;
                else if (c.op == "gt")
                    ok = evalExpr(c.rhs, rawCtx, rhs, err) && v > rhs;
                else if (c.op == "ge")
                    ok = evalExpr(c.rhs, rawCtx, rhs, err) && v >= rhs;
                else if (c.op == "in")
                {
                    for (double iv : c.values)
                        if (v == iv) { ok = true; break; }
                }
                else if (c.op == "range")
                {
                    double lo = 0, hi = 0;
                    ok = evalExpr(c.rhs, rawCtx, lo, err) &&
                         evalExpr(c.rhs2, rawCtx, hi, err) && v >= lo && v <= hi;
                }
                else if (c.op == "mask")
                {
                    double m = 0, expected = 0;
                    ok = evalExpr(c.rhs, rawCtx, m, err) &&
                         evalExpr(c.rhs2, rawCtx, expected, err) &&
                         (static_cast<int64_t>(v) & static_cast<int64_t>(m)) == static_cast<int64_t>(expected);
                }
                else
                {
                    err = "未知校验算子: " + c.op;
                    return false;
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
                  const VariantSpec *&variant, std::string &err)
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

} // namespace

bool ps::protocol::looksSequentialProtocol(const QByteArray &json)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    return root.contains(QStringLiteral("segments"));
}

namespace
{

QString numList(const std::vector<double> &v)
{
    QStringList out;
    const int n = std::min<int>(static_cast<int>(v.size()), 12);
    for (int i = 0; i < n; ++i)
        out << QString::number(v[static_cast<size_t>(i)], 'g', 8);
    if (static_cast<int>(v.size()) > n)
        out << QStringLiteral("…");
    return out.join(QStringLiteral(", "));
}

} // namespace

ps::protocol::SequentialFramePreview ps::protocol::previewSequentialFrame(
    const QByteArray &frame, const ProtocolDocument &tpl)
{
    ps::protocol::SequentialFramePreview res;
    const uint8_t *data = reinterpret_cast<const uint8_t *>(frame.constData());
    const size_t total = static_cast<size_t>(frame.size());
    if (total == 0)
    {
        res.error = QStringLiteral("样例帧为空");
        return res;
    }

    EvalCtx rawCtx, physCtx;
    std::vector<RtSeg> fields;
    std::map<std::string, RtSeg *> byName;
    std::map<std::string, std::pair<uint32_t, uint32_t>> pseudo;
    std::vector<std::string> order;
    for (const SegSpec &s : tpl.segments)
        order.push_back(s.name);

    uint32_t pos = 0;
    bool payloadSeen = false;
    uint32_t payloadOff = 0, payloadBytes = 0, payloadCount = 0;
    std::string e;

    for (const SegSpec &seg : tpl.segments)
    {
        uint32_t count = 0, nbytes = 0;
        if (!evalSize(seg, rawCtx, count, nbytes, e))
        {
            res.error = QString::fromStdString(e);
            return res;
        }
        if (static_cast<uint64_t>(pos) + nbytes > total)
        {
            res.error = QStringLiteral("段 %1 超出帧长度：需要 %2 B，剩余 %3 B")
                            .arg(QString::fromStdString(seg.name))
                            .arg(nbytes)
                            .arg(total - pos);
            return res;
        }
        RtSeg rt;
        rt.spec = &seg;
        rt.offset = pos;
        rt.bytes = nbytes;
        rt.count = count;
        const uint8_t *p = data + pos;

        if (!tpl.series.enabled || seg.role != "payload")
        {
            for (uint32_t i = 0; i < count; ++i)
                rt.raw.push_back(seg.type.bcd && seg.type.size > 1
                                     ? double(readUInt(p + static_cast<size_t>(i) * seg.type.size,
                                                       seg.type.size, seg.bigEndian))
                                     : readElem(p + static_cast<size_t>(i) * seg.type.size,
                                                seg.type, seg.bigEndian));
            for (double v : rt.raw)
                rt.phys.push_back(v * seg.scale + seg.offset);
            rawCtx.values[seg.name] = rt.raw;
            physCtx.values[seg.name] = rt.phys;
            if (seg.type.type == FType::STR)
            {
                std::string txt;
                for (uint32_t i = 0; i < nbytes; ++i)
                    txt.push_back(static_cast<char>(p[i]));
                rawCtx.strings[seg.name] = txt;
                physCtx.strings[seg.name] = txt;
            }
            fields.push_back(std::move(rt));
        }

        if (tpl.series.enabled && seg.role == "payload")
        {
            payloadSeen = true;
            payloadOff = pos;
            payloadBytes = nbytes;
            payloadCount = count;
            std::vector<double> pv;
            for (uint32_t i = 0; i < nbytes; ++i)
                pv.push_back(p[i]);
            rawCtx.values["payload"] = pv;
            physCtx.values["payload"] = pv;
            pseudo["payload"] = {payloadOff, nbytes};
        }
        else if (seg.role == "payload")
        {
            payloadSeen = true;
            payloadOff = pos;
            payloadBytes = nbytes;
            payloadCount = count;
        }
        pos += nbytes;
    }

    if (tpl.series.enabled)
    {
        if (!payloadSeen)
        {
            res.error = QStringLiteral("series.payload 未找到");
            return res;
        }
        std::string selKey;
        if (tpl.series.selector && tpl.series.selector->kind == Expr::Ref &&
            !tpl.series.selector->b && rawCtx.strings.count(tpl.series.selector->name))
        {
            selKey = stringKey(rawCtx.strings[tpl.series.selector->name]);
        }
        else
        {
            double sel = 0;
            if (!tpl.series.selector || !evalExpr(tpl.series.selector, rawCtx, sel, e))
            {
                res.error = QString::fromStdString(e.empty() ? "series.selector 缺失" : e);
                return res;
            }
            if (sel != std::trunc(sel))
            {
                res.error = QStringLiteral("series.selector 不是整数");
                return res;
            }
            selKey = numberKey(sel);
        }
        const VariantSpec *var = nullptr;
        matchVariant(tpl.series, selKey, var, e);
        if (!var)
        {
            if (tpl.series.fallback == "reject")
            {
                res.error = QStringLiteral("series 未命中：%1").arg(QString::fromStdString(selKey));
                return res;
            }
        }
        else
        {
            res.lines << QStringLiteral("变体：%1").arg(QString::fromStdString(var->name));
            uint32_t vpos = payloadOff;
            EvalCtx vctx = rawCtx;
            for (const SegSpec &seg : var->segments)
            {
                uint32_t count = 0, nbytes = 0;
                if (!evalSize(seg, vctx, count, nbytes, e))
                {
                    res.error = QString::fromStdString(e);
                    return res;
                }
                if (vpos + nbytes > total)
                {
                    res.error = QStringLiteral("变体段 %1 超出帧长度").arg(QString::fromStdString(seg.name));
                    return res;
                }
                RtSeg rt;
                rt.spec = &seg;
                rt.offset = vpos;
                rt.bytes = nbytes;
                rt.count = count;
                const uint8_t *p = data + vpos;
                for (uint32_t i = 0; i < count; ++i)
                    rt.raw.push_back(seg.type.bcd && seg.type.size > 1
                                         ? double(readUInt(p + static_cast<size_t>(i) * seg.type.size,
                                                           seg.type.size, seg.bigEndian))
                                         : readElem(p + static_cast<size_t>(i) * seg.type.size,
                                                    seg.type, seg.bigEndian));
                for (double v : rt.raw)
                    rt.phys.push_back(v * seg.scale + seg.offset);
                vctx.values[seg.name] = rt.raw;
                physCtx.values[seg.name] = rt.phys;
                fields.push_back(std::move(rt));
                vpos += nbytes;
            }
            if (vpos != payloadOff + payloadBytes)
            {
                res.error = QStringLiteral("变体段总字节数 %1 != payload %2")
                                .arg(vpos - payloadOff)
                                .arg(payloadBytes);
                return res;
            }
        }
    }

    std::sort(fields.begin(), fields.end(),
              [](const RtSeg &a, const RtSeg &b) { return a.offset < b.offset; });
    for (RtSeg &rt : fields)
        byName[rt.spec->name] = &rt;

    if (!checkFrame(fields, byName, pseudo, order, data, rawCtx, e))
    {
        res.error = QString::fromStdString(e);
        for (const RtSeg &rt : fields)
            res.lines << QStringLiteral("%1 @ %2  %3 B")
                             .arg(QString::fromStdString(rt.spec->name))
                             .arg(rt.offset)
                             .arg(rt.bytes);
        return res;
    }

    res.ok = true;
    res.lines << QStringLiteral("帧 %1 B · %2").arg(frame.size()).arg(QString::fromStdString(tpl.name));
    for (const RtSeg &rt : fields)
    {
        const SegSpec &seg = *rt.spec;
        if (seg.type.type == FType::STR)
        {
            QString text = QString::fromStdString(rawCtx.strings[seg.name]);
            res.lines << QStringLiteral("%1 @ %2  str %3 B  \"%4\"")
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
        double ts = 0;
        const EvalCtx &ctx = tpl.timestampRaw ? rawCtx : physCtx;
        if (evalExpr(tpl.timestampExpr, ctx, ts, e))
        {
            if (tpl.timestampUnit == "ms")
                ts /= 1000.0;
            else if (tpl.timestampUnit == "us")
                ts /= 1000000.0;
            res.lines << QStringLiteral("timestamp：%1 s").arg(QString::number(ts, 'f', 6));
        }
        else
        {
            res.lines << QStringLiteral("timestamp 求值失败：%1")
                             .arg(QString::fromStdString(e));
        }
    }
    return res;
}


ps::protocol::ProtocolDecodeResult ps::protocol::decodeSequential(
    const QByteArray &bytes, const ProtocolDocument &tpl)
{
    ProtocolDecodeResult result;
    std::string e;

    const uint8_t *data = reinterpret_cast<const uint8_t *>(bytes.constData());
    const size_t total = static_cast<size_t>(bytes.size());
    std::map<std::string, ProtocolChannel> chans;
    size_t off = 0;
    double frameIndex = 0;
    bool haveBase = false;
    double baseTs = 0;
    uint64_t rejected = 0;
    uint64_t warnings = 0;

    while (off + 1 < total)
    {
        EvalCtx rawCtx;
        EvalCtx physCtx;
        std::vector<RtSeg> fields;
        std::map<std::string, RtSeg *> byName;
        std::map<std::string, std::pair<uint32_t, uint32_t>> pseudo;
        std::vector<std::string> order;
        for (const SegSpec &s : tpl.segments)
            order.push_back(s.name);

        uint32_t pos = 0;
        bool payloadSeen = false;
        bool variantHit = false;
        uint32_t payloadOff = 0, payloadBytes = 0, payloadCount = 0;
        bool ok = true;
        for (const SegSpec &seg : tpl.segments)
        {
            uint32_t count = 0, nbytes = 0;
            if (!evalSize(seg, rawCtx, count, nbytes, e))
            {
                ok = false;
                break;
            }
            if (tpl.series.enabled && seg.role == "payload")
            {
                payloadSeen = true;
                payloadOff = pos;
                payloadBytes = nbytes;
                payloadCount = count;
                if (static_cast<uint64_t>(off) + pos + nbytes > total)
                {
                    e = "payload 越过文件末尾";
                    ok = false;
                    break;
                }
                pos += nbytes;
                std::vector<double> pv;
                const uint8_t *p = data + off + payloadOff;
                for (uint32_t i = 0; i < nbytes; ++i)
                    pv.push_back(p[i]);
                rawCtx.values["payload"] = pv;
                physCtx.values["payload"] = pv;
                pseudo["payload"] = {payloadOff, nbytes};
                continue;
            }
            if (seg.role == "payload")
            {
                payloadSeen = true;
                payloadOff = pos;
                payloadBytes = nbytes;
                payloadCount = count;
            }
            if (static_cast<uint64_t>(pos) + nbytes + off > total)
            {
                e = "帧越过文件末尾";
                ok = false;
                break;
            }
            RtSeg rt;
            rt.spec = &seg;
            rt.offset = pos;
            rt.bytes = nbytes;
            rt.count = count;
            const uint8_t *p = data + off + pos;
            for (uint32_t i = 0; i < count; ++i)
            {
                const uint8_t *ep = p + static_cast<size_t>(i) * seg.type.size;
                rt.raw.push_back(seg.type.bcd && seg.type.size > 1
                                     ? double(readUInt(ep, seg.type.size, seg.bigEndian))
                                     : readElem(ep, seg.type, seg.bigEndian));
            }
            rawCtx.values[seg.name] = rt.raw;
            if (seg.type.type == FType::STR)
            {
                std::string txt;
                for (uint32_t i = 0; i < nbytes; ++i)
                    txt.push_back(static_cast<char>(p[i]));
                rawCtx.strings[seg.name] = txt;
                physCtx.strings[seg.name] = txt;
            }
            for (double v : rt.raw)
                rt.phys.push_back(v * seg.scale + seg.offset);
            std::vector<double> pv;
            for (double v : rt.raw)
                pv.push_back(v * seg.scale + seg.offset);
            physCtx.values[seg.name] = pv;
            fields.push_back(std::move(rt));
            pos += nbytes;
        }
        if (!ok)
        {
            if (std::getenv("PS_SEQ_DEBUG"))
                std::fprintf(stderr, "reject off=%zu: %s\n", off, e.c_str());
            ++off;
            ++rejected;
            if (rejected > 1000000)
                break;
            continue;
        }

        if (tpl.series.enabled)
        {
            if (!payloadSeen)
            {
                e = "series.payload 未找到";
                ok = false;
            }
            else
            {
                std::string selKey;
                if (tpl.series.selector->kind == Expr::Ref && !tpl.series.selector->b &&
                    rawCtx.strings.count(tpl.series.selector->name))
                {
                    selKey = stringKey(rawCtx.strings[tpl.series.selector->name]);
                }
                else
                {
                    double sel = 0;
                    if (!evalExpr(tpl.series.selector, rawCtx, sel, e))
                        ok = false;
                    else if (sel != std::trunc(sel))
                    {
                        e = "series.selector 不是整数";
                        ok = false;
                    }
                    else
                        selKey = numberKey(sel);
                }
                const VariantSpec *var = nullptr;
                if (ok && !matchVariant(tpl.series, selKey, var, e))
                    ok = false;
                if (ok && var)
                {
                    variantHit = true;
                }
                if (ok && !var)
                {
                    if (tpl.series.fallback == "reject")
                    {
                        e = "series 未命中 msgid";
                        ok = false;
                    }
                }
                else if (ok)
                {
                    uint32_t vpos = payloadOff;
                    EvalCtx vctx = rawCtx;
                    for (const SegSpec &seg : var->segments)
                    {
                        uint32_t count = 0, nbytes = 0;
                        if (!evalSize(seg, vctx, count, nbytes, e))
                        {
                            ok = false;
                            break;
                        }
                        RtSeg rt;
                        rt.spec = &seg;
                        rt.offset = vpos;
                        rt.bytes = nbytes;
                        rt.count = count;
                        const uint8_t *p = data + off + vpos;
                        for (uint32_t i = 0; i < count; ++i)
                            rt.raw.push_back(readElem(p + static_cast<size_t>(i) * seg.type.size,
                                                      seg.type, seg.bigEndian));
                        vctx.values[seg.name] = rt.raw;
                        for (double v : rt.raw)
                            rt.phys.push_back(v * seg.scale + seg.offset);
                        std::vector<double> pv;
                        for (double v : rt.raw)
                            pv.push_back(v * seg.scale + seg.offset);
                        physCtx.values[seg.name] = pv;
                        fields.push_back(std::move(rt));
                        vpos += nbytes;
                    }
                    if (ok && vpos != payloadOff + payloadBytes)
                    {
                        e = "变体段总字节数不等于 payload";
                        ok = false;
                    }
                }
            }
        }
        if (ok && tpl.series.enabled && payloadSeen && !variantHit &&
            tpl.series.fallback == "raw")
        {
            RtSeg rt;
            for (const SegSpec &seg : tpl.segments)
                if (seg.role == "payload")
                {
                    rt.spec = &seg;
                    break;
                }
            rt.offset = payloadOff;
            rt.bytes = payloadBytes;
            rt.count = payloadCount;
            const uint8_t *p = data + off + payloadOff;
            for (uint32_t i = 0; i < payloadBytes; ++i)
                rt.raw.push_back(p[i]);
            fields.push_back(std::move(rt));
        }
        if (!ok)
        {
            if (std::getenv("PS_SEQ_DEBUG"))
                std::fprintf(stderr, "reject off=%zu: %s\n", off, e.c_str());
            ++off;
            ++rejected;
            if (rejected > 1000000)
                break;
            continue;
        }

        std::sort(fields.begin(), fields.end(), [](const RtSeg &a, const RtSeg &b) { return a.offset < b.offset; });
        byName.clear();
        for (RtSeg &rt : fields)
            byName[rt.spec->name] = &rt;

        if (!checkFrame(fields, byName, pseudo, order, data + off, rawCtx, e))
        {
            if (std::getenv("PS_SEQ_DEBUG"))
                std::fprintf(stderr, "reject off=%zu: %s\n", off, e.c_str());
            ++off;
            ++rejected;
            if (rejected > 1000000)
                break;
            continue;
        }

        /* counts 反算 */
        for (RtSeg &rt : fields)
        {
            const SegSpec &seg = *rt.spec;
            if (!seg.counts.enabled || seg.role != "length")
                continue;
            double declared = rt.raw.empty() ? 0 : rt.raw[0];
            double actual = 0;
            if (seg.counts.expr)
            {
                if (!evalExpr(seg.counts.expr, rawCtx, actual, e))
                {
                    ok = false;
                    break;
                }
            }
            else
            {
                for (const std::string &fn : seg.counts.fields)
                {
                    auto pit = pseudo.find(fn);
                    if (pit != pseudo.end())
                    {
                        actual += (seg.counts.unit == "bytes")
                                      ? pit->second.second
                                      : (payloadCount ? payloadCount : pit->second.second);
                        continue;
                    }
                    auto it = byName.find(fn);
                    if (it == byName.end())
                    {
                        e = "counts 引用未知段: " + fn;
                        ok = false;
                        break;
                    }
                    actual += (seg.counts.unit == "bytes") ? it->second->bytes : it->second->count;
                }
            }
            if (ok && declared != actual)
            {
                e = "长度段 " + seg.name + " 与 counts 不一致";
                ok = false;
            }
            if (!ok)
                break;
        }
        if (!ok)
        {
            if (std::getenv("PS_SEQ_DEBUG"))
                std::fprintf(stderr, "reject off=%zu: %s\n", off, e.c_str());
            ++off;
            ++rejected;
            continue;
        }

        double ts = frameIndex;
        if (tpl.hasTimestamp)
        {
            double v = 0;
            if (!evalExpr(tpl.timestampExpr, tpl.timestampRaw ? rawCtx : physCtx, v, e))
            {
                if (tpl.timestampFallback == "index")
                    v = frameIndex;
                else
                {
                    ++off;
                    ++rejected;
                    continue;
                }
            }
            if (tpl.timestampUnit == "ms")
                v *= 0.001;
            else if (tpl.timestampUnit == "us")
                v *= 0.000001;
            if (tpl.timestampOrigin == "first_frame")
            {
                if (!haveBase)
                {
                    baseTs = v;
                    haveBase = true;
                }
                ts = v - baseTs;
            }
            else
            {
                ts = v;
            }
        }

        bool anyChannel = false;
        for (RtSeg &rt : fields)
        {
            const SegSpec &seg = *rt.spec;
            const bool isPayload = seg.role == "payload";
            const bool skipRole = seg.role == "sync" || seg.role == "length" || seg.role == "checksum";
            if (seg.type.type == FType::STR)
                continue;
            if (seg.hidden || !seg.plot || skipRole || seg.hasValue)
                continue;
            if (isPayload && tpl.series.enabled && variantHit)
                continue;
            if (rt.raw.size() > 256)
                ++warnings;
            const uint32_t limit = std::min<uint32_t>(rt.count, 256);
            const std::string prefix = "frame." + (seg.fromVariant ? seg.prefix : std::string());
            if (rt.count == 1)
            {
                ProtocolChannel &ch = chans[prefix + seg.name];
                ch.name = prefix + seg.name;
                ch.unit = seg.unit;
                ch.t.push_back(ts);
                ch.v.push_back(rt.phys.empty() ? rt.raw[0] : rt.phys[0]);
                anyChannel = true;
            }
            else
            {
                for (uint32_t i = 0; i < limit; ++i)
                {
                    const std::string name = prefix + seg.name + "[" + std::to_string(i) + "]";
                    ProtocolChannel &ch = chans[name];
                    ch.name = name;
                    ch.unit = seg.unit;
                    ch.t.push_back(ts);
                    ch.v.push_back(rt.phys.empty() ? rt.raw[i] : rt.phys[i]);
                    anyChannel = true;
                }
            }
        }
        if (!anyChannel)
        {
            if (std::getenv("PS_SEQ_DEBUG"))
                std::fprintf(stderr, "reject off=%zu: %s\n", off, e.c_str());
            ++off;
            ++rejected;
            continue;
        }

        off += pos;
        frameIndex += 1;
    }

    result.rejectedFrames = rejected;
    result.warnings = warnings;
    if (chans.empty())
    {
        result.error = QStringLiteral("顺序协议没有可绘制通道；检查 role/plot/hidden 或协议内容");
        return result;
    }

    for (auto &kv : chans)
        result.channels.push_back(std::move(kv.second));
    result.ok = true;
    return result;
}

ps::protocol::ProtocolDecodeResult ps::protocol::decodeSequentialJson(
    const QByteArray &bytes, const QByteArray &templateJson)
{
    ProtocolDecodeResult result;
    QJsonParseError perr;
    const QJsonObject root = QJsonDocument::fromJson(templateJson, &perr).object();
    if (perr.error != QJsonParseError::NoError)
    {
        result.error = QStringLiteral("协议 JSON 解析失败: ") + perr.errorString();
        return result;
    }
    const QStringList issues = ps::protocol::validate(root);
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
