#include "protocol/SequentialCodec.h"
#include "protocol/CompiledProgram.h"
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
#include <cstdio>
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

// 与 parseDocument 生成变体 key 时同一套归一规则，保证 "n:1" / "s:GPGGA" 对得上。
inline std::string numberKey(double v)
{
    std::ostringstream os;
    os << std::setprecision(17) << v;
    return "n:" + os.str();
}

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

TypedValue readTyped(const uint8_t *p, ValueType t, uint32_t size, bool be)
{
    switch (t)
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
        uint64_t v = 0;
        for (uint32_t i = 0; i < size; ++i)
        {
            const uint8_t b = be ? p[i] : p[size - 1 - i];
            v = v * 100 + static_cast<uint64_t>(b >> 4) * 10 + (b & 0x0F);
        }
        return TypedValue::fromU(v, ValueType::Bcd);
    }
    case ValueType::Text: return TypedValue::fromText(std::string());
    }
    return TypedValue{};
}

inline bool isTextVal(const TypedValue &v) { return v.type == ValueType::Text; }

int compareNumeric(const TypedValue &a, const TypedValue &b, std::string &err)
{
    if (isTextVal(a) || isTextVal(b))
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

/* ---------------- 校验算子 ---------------- */

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

uint32_t crcGeneric(const uint8_t *data, size_t n, uint32_t poly, uint32_t init, bool refin,
                    bool refout, uint32_t xorout, int width)
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

uint32_t sequenceValue(const CompiledCheck &c, const std::vector<uint8_t> &buf)
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

/* ---------------- 读一帧 ---------------- */

struct Rt
{
    const CompiledSeg *spec = nullptr;
    uint32_t offset = 0;
    uint32_t bytes = 0;
    uint32_t count = 0;
    TypedVec raw;
    TypedVec phys;
};

struct FrameRead
{
    bool ok = false;
    std::string err;
    std::vector<Rt> fields;
    int32_t variant = -1;
    uint32_t consumed = 0;
};

void fillSlot(const CompiledSlot &slot, const uint8_t *sp, uint32_t count, TypedVec &raw,
              TypedVec &phys, const CompiledSeg &seg)
{
    if (slot.type == ValueType::Text)
    {
        std::string txt;
        txt.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
            txt.push_back(static_cast<char>(sp[i]));
        raw.reset(ValueType::Text);
        raw.push(TypedValue::fromText(txt));
        phys = raw;
        return;
    }
    raw.reset(slot.type);
    raw.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
        raw.push(readTyped(sp + static_cast<size_t>(i) * slot.elemSize, slot.type, slot.elemSize,
                           slot.bigEndian));
    if (seg.identityPhys)
    {
        phys = raw;
        return;
    }
    phys.reset(ValueType::F64);
    phys.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
        phys.push(TypedValue::fromF(raw.at(i).asDouble() * seg.scale + seg.offset, ValueType::F64));
}

bool evalCountOf(const CompiledSeg &seg, const std::vector<TypedVec> &cols, uint32_t &count,
                 uint32_t &bytes, std::string &err)
{
    if (seg.countIsConst)
    {
        count = seg.constCount;
        bytes = seg.constBytes;
        return true;
    }
    TypedValue v;
    if (!runByteCode(seg.count, cols, v, err))
        return false;
    if (isTextVal(v) || isFloatType(v.type))
    {
        err = "段 " + seg.name + " 的 count 必须是整型";
        return false;
    }
    const int64_t n = v.asI64();
    if (n <= 0 || n > 1000000)
    {
        err = "段 " + seg.name + " 的 count 非法";
        return false;
    }
    count = static_cast<uint32_t>(n);
    bytes = count * seg.elemSize;
    if (bytes == 0 || bytes > (1u << 20))
    {
        err = "段 " + seg.name + " 字节数非法";
        return false;
    }
    return true;
}

bool checkFrame(const std::vector<Rt> &fields, const std::map<int32_t, const Rt *> &bySlot,
                const CompiledProgram &prog, const uint8_t *frame, uint32_t payloadOff,
                uint32_t payloadBytes, const std::vector<TypedVec> &rawSlots, std::string &err)
{
    for (const Rt &rt : fields)
    {
        const CompiledSeg &seg = *rt.spec;
        if (seg.hasExpect)
        {
            TypedValue want = seg.expectConst;
            if (!seg.expectIsConst && !runByteCode(seg.expect, rawSlots, want, err))
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

        for (const CompiledCheck &c : seg.checks)
        {
            if (c.isSequence)
            {
                if (rt.raw.size() != 1 || rt.bytes != static_cast<uint32_t>(c.width))
                {
                    err = "段 " + seg.name + " 校验宽度与算子不匹配";
                    return false;
                }
                std::vector<uint8_t> buf;
                for (int32_t slot : c.overSlots)
                {
                    if (slot < 0)
                    {
                        const uint8_t *q = frame + payloadOff;
                        buf.insert(buf.end(), q, q + payloadBytes);
                        continue;
                    }
                    auto it = bySlot.find(slot);
                    if (it == bySlot.end())
                    {
                        err = "over 引用未知段";
                        return false;
                    }
                    const uint8_t *q = frame + it->second->offset;
                    buf.insert(buf.end(), q, q + it->second->bytes);
                }
                const uint32_t got = sequenceValue(c, buf);
                if (static_cast<uint32_t>(rt.raw.at(0).asU64()) != got)
                {
                    err = "段 " + seg.name + " 校验失败";
                    return false;
                }
                continue;
            }

            for (std::size_t i = 0; i < rt.raw.size(); ++i)
            {
                const TypedValue &v = rt.raw.at(i);
                bool ok = false;
                if (c.op == "in")
                {
                    for (const TypedValue &iv : c.values)
                    {
                        const int cmp = compareNumeric(v, iv, err);
                        if (cmp == 2)
                            return false;
                        if (cmp == 0) { ok = true; break; }
                    }
                }
                else if (c.op == "mask")
                {
                    TypedValue m, expected;
                    if (!runByteCode(c.rhs, rawSlots, m, err) ||
                        !runByteCode(c.rhs2, rawSlots, expected, err))
                        return false;
                    if (isFloatType(v.type) || isFloatType(m.type) || isFloatType(expected.type))
                    {
                        err = "mask 只支持整型";
                        return false;
                    }
                    ok = ((v.asU64() & m.asU64()) == expected.asU64());
                }
                else if (c.op == "range")
                {
                    TypedValue lo, hi;
                    if (!runByteCode(c.rhs, rawSlots, lo, err) ||
                        !runByteCode(c.rhs2, rawSlots, hi, err))
                        return false;
                    const int cl = compareNumeric(v, lo, err);
                    const int ch = compareNumeric(v, hi, err);
                    if (cl == 2 || ch == 2)
                        return false;
                    ok = (cl >= 0 && ch <= 0);
                }
                else
                {
                    TypedValue rhs;
                    if (!runByteCode(c.rhs, rawSlots, rhs, err))
                        return false;
                    const int cmp = compareNumeric(v, rhs, err);
                    if (cmp == 2)
                        return false;
                    if (c.op == "eq") ok = (cmp == 0);
                    else if (c.op == "ne") ok = (cmp != 0);
                    else if (c.op == "lt") ok = (cmp < 0);
                    else if (c.op == "le") ok = (cmp <= 0);
                    else if (c.op == "gt") ok = (cmp > 0);
                    else if (c.op == "ge") ok = (cmp >= 0);
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

// 读一帧：调用方复用 rawSlots/physSlots（已按 prog.slotTable 尺寸准备）。
bool readFrame(const CompiledProgram &prog, std::vector<TypedVec> &rawSlots,
               std::vector<TypedVec> &physSlots, const uint8_t *p, size_t avail, FrameRead &r)
{
    for (size_t i = 0; i < prog.slotTable.size(); ++i)
    {
        rawSlots[i].reset(prog.slotTable[i].type);
        physSlots[i].reset(prog.slotTable[i].type);
    }

    uint32_t pos = 0;
    uint32_t payloadOff = 0, payloadBytes = 0, payloadCount = 0;
    const bool hasPayload = prog.hasSeries;

    for (const CompiledSeg &seg : prog.segs)
    {
        uint32_t count = 0, bytes = 0;
        if (!evalCountOf(seg, rawSlots, count, bytes, r.err))
            return false;
        if (static_cast<uint64_t>(pos) + bytes > avail)
        {
            r.err = "段 " + seg.name + " 超出帧长度：需要 " + std::to_string(bytes) + " B，剩余 " +
                    std::to_string(avail - pos) + " B";
            return false;
        }

        const bool isPayload = hasPayload && seg.slot == prog.payloadSlot;
        if (isPayload)
        {
            payloadOff = pos;
            payloadBytes = bytes;
            payloadCount = count;
            TypedVec &raw = rawSlots[static_cast<size_t>(seg.slot)];
            raw.reset(ValueType::U8);
            raw.reserve(bytes);
            for (uint32_t i = 0; i < bytes; ++i)
                raw.push(TypedValue::fromU(p[static_cast<size_t>(pos) + i], ValueType::U8));
            physSlots[static_cast<size_t>(seg.slot)] = raw;
            pos += bytes;
            continue;
        }

        Rt rt;
        rt.spec = &seg;
        rt.offset = pos;
        rt.bytes = bytes;
        rt.count = count;
        fillSlot(prog.slotTable[static_cast<size_t>(seg.slot)], p + pos, count, rt.raw, rt.phys, seg);
        rawSlots[static_cast<size_t>(seg.slot)] = rt.raw;
        physSlots[static_cast<size_t>(seg.slot)] = rt.phys;
        r.fields.push_back(std::move(rt));
        pos += bytes;
    }

    if (hasPayload)
    {
        std::string selKey;
        const TypedVec &selCol = rawSlots[static_cast<size_t>(prog.selectorSlot)];
        if (selCol.size() != 1)
        {
            r.err = "series.selector 必须是单值";
            return false;
        }
        if (selCol.type == ValueType::Text)
            selKey = "s:" + selCol.at(0).s;
        else
            selKey = numberKey(selCol.at(0).asDouble());

        const CompiledVariant *var = nullptr;
        for (const CompiledVariant &v : prog.variants)
        {
            for (const std::string &k : v.keys)
                if (k == selKey)
                {
                    var = &v;
                    break;
                }
            if (var)
                break;
        }
        if (!var)
        {
            if (prog.fallbackReject)
            {
                r.err = "series 未命中：" + selKey;
                return false;
            }
            r.consumed = pos;
            r.ok = true;
            return true;
        }

        r.variant = static_cast<int32_t>(var - prog.variants.data());
        uint32_t vpos = payloadOff;
        for (int32_t vi : var->segs)
        {
            const CompiledSeg &seg = prog.varSegs[static_cast<size_t>(vi)];
            uint32_t count = 0, bytes = 0;
            if (!evalCountOf(seg, rawSlots, count, bytes, r.err))
                return false;
            if (static_cast<uint64_t>(vpos) + bytes > avail)
            {
                r.err = "变体段 " + seg.name + " 超出帧长度";
                return false;
            }
            Rt rt;
            rt.spec = &seg;
            rt.offset = vpos;
            rt.bytes = bytes;
            rt.count = count;
            fillSlot(prog.slotTable[static_cast<size_t>(seg.slot)], p + vpos, count, rt.raw, rt.phys, seg);
            rawSlots[static_cast<size_t>(seg.slot)] = rt.raw;
            physSlots[static_cast<size_t>(seg.slot)] = rt.phys;
            r.fields.push_back(std::move(rt));
            vpos += bytes;
        }
        if (vpos != payloadOff + payloadBytes)
        {
            r.err = "变体段总字节数不等于 payload";
            return false;
        }
    }

    std::map<int32_t, const Rt *> bySlot;
    for (Rt &rt : r.fields)
        bySlot[rt.spec->slot] = &rt;

    if (!checkFrame(r.fields, bySlot, prog, p, payloadOff, payloadBytes, rawSlots, r.err))
        return false;

    // length 反算：本段声明值必须等于后续字段合计（字节数或元素数）
    for (Rt &rt : r.fields)
    {
        const CompiledSeg &seg = *rt.spec;
        if (!seg.length.enabled)
            continue;
        const int64_t declared = rt.raw.size() ? rt.raw.at(0).asI64() : 0;
        int64_t actual = 0;
        if (!seg.length.fields.empty())
        {
            for (int32_t slot : seg.length.fields)
            {
                if (slot < 0)
                {
                    actual += seg.length.bytesUnit ? static_cast<int64_t>(payloadBytes)
                                                   : static_cast<int64_t>(payloadCount);
                    continue;
                }
                auto it = bySlot.find(slot);
                if (it == bySlot.end())
                {
                    r.err = "length 引用未知段";
                    return false;
                }
                actual += seg.length.bytesUnit ? static_cast<int64_t>(it->second->bytes)
                                               : static_cast<int64_t>(it->second->count);
            }
        }
        else
        {
            TypedValue v;
            if (!runByteCode(seg.length.expr, rawSlots, v, r.err))
                return false;
            if (isTextVal(v) || isFloatType(v.type))
            {
                r.err = "length 表达式必须是整型";
                return false;
            }
            actual = v.asI64();
        }
        if (declared != actual)
        {
            r.err = "长度段 " + seg.name + " 与 length 不一致";
            return false;
        }
    }

    r.consumed = pos;
    r.ok = true;
    return true;
}

/* ---------------- 时间戳（整数 ticks） ---------------- */

const int64_t kTicksPerSecond = 1000000;

bool unitTicks(int64_t raw, const std::string &unit, int64_t &ticks, std::string &err)
{
    switch (unit.empty() ? 's' : unit[0])
    {
    case 's':
        if (raw > (INT64_MAX / kTicksPerSecond) || raw < (INT64_MIN / kTicksPerSecond))
        {
            err = "timestamp 溢出（秒）";
            return false;
        }
        ticks = raw * kTicksPerSecond;
        return true;
    case 'm': ticks = raw * 1000; return true;
    case 'u': ticks = raw; return true;
    case 'n': ticks = raw / 1000; return true;
    default:
        err = "未知时间单位: " + unit;
        return false;
    }
}

std::vector<TypedVec> makeSlots(const CompiledProgram &prog)
{
    std::vector<TypedVec> cols;
    cols.resize(prog.slotTable.size());
    for (size_t i = 0; i < prog.slotTable.size(); ++i)
        cols[i].reset(prog.slotTable[i].type);
    return cols;
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

nextpilot::plotscope::protocol::SequentialFramePreview
nextpilot::plotscope::protocol::previewSequentialFrame(const QByteArray &frame,
                                                      const ProtocolDocument &tpl)
{
    SequentialFramePreview res;
    if (frame.isEmpty())
    {
        res.error = QStringLiteral("样例帧为空");
        return res;
    }

    CompiledProgram prog;
    std::string e;
    if (!compileProgram(tpl, prog, e))
    {
        res.error = QString::fromStdString("编译失败：" + e);
        return res;
    }

    std::vector<TypedVec> rawSlots = makeSlots(prog);
    std::vector<TypedVec> physSlots = makeSlots(prog);
    FrameRead fr;
    if (!readFrame(prog, rawSlots, physSlots,
                   reinterpret_cast<const uint8_t *>(frame.constData()),
                   static_cast<size_t>(frame.size()), fr))
    {
        res.error = QString::fromStdString(fr.err);
        for (const Rt &rt : fr.fields)
            res.lines << QStringLiteral("%1 @ %2  %3 B")
                             .arg(QString::fromStdString(rt.spec->name))
                             .arg(rt.offset)
                             .arg(rt.bytes);
        return res;
    }

    res.ok = true;
    const QString varName =
        fr.variant >= 0
            ? QString::fromStdString(prog.variants[static_cast<size_t>(fr.variant)].name)
            : QString();
    res.lines << QStringLiteral("帧 %1 B · %2%3")
                     .arg(frame.size())
                     .arg(QString::fromStdString(tpl.name))
                     .arg(varName.isEmpty() ? QString()
                                            : QStringLiteral(" · 变体 %1").arg(varName));
    for (const Rt &rt : fr.fields)
    {
        const CompiledSeg &seg = *rt.spec;
        if (seg.isText)
        {
            const QString text = rt.raw.size() ? QString::fromStdString(rt.raw.at(0).s) : QString();
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
                               .arg(seg.elemSize)
                               .arg(rt.bytes)
                               .arg(numList(rt.raw));
            if (!seg.identityPhys)
                line += QStringLiteral("  phys=[%1]").arg(numList(rt.phys));
            if (!seg.unit.empty())
                line += QStringLiteral(" %1").arg(QString::fromStdString(seg.unit));
            res.lines << line;
        }
    }

    if (prog.hasTimestamp)
    {
        const std::vector<TypedVec> &ctx = prog.timestampRaw ? rawSlots : physSlots;
        TypedValue tv;
        std::string err;
        int64_t ticks = 0;
        int64_t iv = 0;
        bool got = runByteCode(prog.timestamp, ctx, tv, err) && !isTextVal(tv);
        if (got)
            iv = tv.isSigned() ? tv.asI64() : static_cast<int64_t>(tv.asU64());
        if (got && unitTicks(iv, prog.timestampUnit, ticks, err))
        {
            res.lines << QStringLiteral("timestamp：%1 s（%2 ticks）")
                             .arg(QString::number(static_cast<double>(ticks) / kTicksPerSecond, 'f',
                                                  6))
                             .arg(QString::number(static_cast<qlonglong>(ticks)));
        }
        else
        {
            res.lines << QStringLiteral("timestamp 求值失败：%1").arg(QString::fromStdString(err));
        }
    }
    return res;
}

/* ---------------- 生产解码 ---------------- */

nextpilot::plotscope::protocol::ProtocolDecodeResult
nextpilot::plotscope::protocol::decodeSequentialCompiled(const QByteArray &bytes,
                                                         const CompiledProgram &prog)
{
    ProtocolDecodeResult result;
    result.ticksPerSecond = kTicksPerSecond;

    const uint8_t *data = reinterpret_cast<const uint8_t *>(bytes.constData());
    const size_t total = static_cast<size_t>(bytes.size());
    std::map<std::string, ProtocolChannel> chans;
    std::vector<TypedVec> rawSlots = makeSlots(prog);
    std::vector<TypedVec> physSlots = makeSlots(prog);
    size_t off = 0;
    int64_t frameIndex = 0;
    bool haveBase = false;
    int64_t baseTs = 0;
    uint64_t rejected = 0;
    uint64_t warnings = 0;

    while (off + 1 < total)
    {
        FrameRead fr;
        if (!readFrame(prog, rawSlots, physSlots, data + off, total - off, fr))
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
        if (prog.hasTimestamp)
        {
            const std::vector<TypedVec> &ctx = prog.timestampRaw ? rawSlots : physSlots;
            TypedValue tv;
            std::string err;
            bool got = runByteCode(prog.timestamp, ctx, tv, err) && !isTextVal(tv);
            int64_t rawTs = 0;
            if (got)
                rawTs = tv.isSigned() ? tv.asI64() : static_cast<int64_t>(tv.asU64());
            else if (prog.timestampIndexFallback)
                rawTs = frameIndex;
            else
            {
                ++off;
                ++rejected;
                continue;
            }
            if (prog.timestampOrigin == "first_frame")
            {
                if (!haveBase)
                {
                    baseTs = rawTs;
                    haveBase = true;
                }
                rawTs -= baseTs;
            }
            int64_t ticks = 0;
            if (!unitTicks(rawTs, prog.timestampUnit, ticks, err))
            {
                ++off;
                ++rejected;
                continue;
            }
            ts = ticks;
        }

        bool anyChannel = false;
        for (Rt &rt : fr.fields)
        {
            const CompiledSeg &seg = *rt.spec;
            if (seg.emitMode != 0)
                continue;
            if (seg.isText)
                continue;
            const bool isPayload = prog.hasSeries && seg.slot == prog.payloadSlot;
            if (isPayload && fr.variant >= 0)
                continue;
            if (rt.raw.size() > 256)
                ++warnings;
            const uint32_t limit = std::min<uint32_t>(rt.count, 256);
            const std::string prefix = seg.prefix;
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

nextpilot::plotscope::protocol::ProtocolDecodeResult
nextpilot::plotscope::protocol::decodeSequential(const QByteArray &bytes,
                                                 const ProtocolDocument &tpl)
{
    ProtocolDecodeResult result;
    result.ticksPerSecond = kTicksPerSecond;

    CompiledProgram prog;
    std::string ce;
    if (!compileProgram(tpl, prog, ce))
    {
        result.error = QString::fromStdString("编译失败：" + ce);
        return result;
    }
    return decodeSequentialCompiled(bytes, prog);
}

nextpilot::plotscope::protocol::ProtocolDecodeResult
nextpilot::plotscope::protocol::decodeSequentialJson(const QByteArray &bytes,
                                                     const QByteArray &templateJson)
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
