#include "ps_extension_api.h"
#include "LegacyFileInput.hpp"
#include "PsLoaderCommon.h"

#include <QFile>
#include <QFileInfo>
#include <QString>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

struct Value
{
    enum Kind { Null, Number, Bool, String, Array, Object } kind = Null;
    double num = 0;
    bool b = false;
    std::string str;
    std::vector<Value> arr;
    std::map<std::string, Value> obj;
};

struct Reader
{
    const uint8_t *p = nullptr;
    const uint8_t *end = nullptr;
    bool ok = true;

    bool need(size_t n)
    {
        if (static_cast<size_t>(end - p) < n)
        {
            ok = false;
            return false;
        }
        return true;
    }
    uint8_t u8()
    {
        if (!need(1)) return 0;
        return *p++;
    }
    uint16_t u16()
    {
        if (!need(2)) return 0;
        uint16_t v; std::memcpy(&v, p, 2); p += 2; return v;
    }
    uint32_t u32()
    {
        if (!need(4)) return 0;
        uint32_t v; std::memcpy(&v, p, 4); p += 4; return v;
    }
    uint64_t u64()
    {
        if (!need(8)) return 0;
        uint64_t v; std::memcpy(&v, p, 8); p += 8; return v;
    }
    float f32()
    {
        if (!need(4)) return 0;
        float v; std::memcpy(&v, p, 4); p += 4; return v;
    }
    double f64()
    {
        if (!need(8)) return 0;
        double v; std::memcpy(&v, p, 8); p += 8; return v;
    }
};

bool cborItem(Reader &r, Value &v, int depth);

bool cborLength(Reader &r, uint8_t info, uint64_t &len)
{
    if (info < 24) { len = info; return true; }
    if (info == 24) { len = r.u8(); return r.ok; }
    if (info == 25) { len = r.u16(); return r.ok; }
    if (info == 26) { len = r.u32(); return r.ok; }
    if (info == 27) { len = r.u64(); return r.ok; }
    r.ok = false;
    return false;
}

bool cborItem(Reader &r, Value &v, int depth)
{
    if (depth > 24 || !r.ok)
        return false;
    const uint8_t b = r.u8();
    if (!r.ok) return false;
    const uint8_t major = b >> 5;
    const uint8_t info = b & 0x1F;
    uint64_t len = 0;
    if (major <= 5 && !cborLength(r, info, len))
        return false;
    switch (major)
    {
    case 0:
        v.kind = Value::Number; v.num = static_cast<double>(len); return true;
    case 1:
        v.kind = Value::Number; v.num = -1.0 - static_cast<double>(len); return true;
    case 2:
    case 3:
    {
        if (!r.need(static_cast<size_t>(len))) return false;
        v.kind = Value::String;
        v.str.assign(reinterpret_cast<const char *>(r.p), static_cast<size_t>(len));
        r.p += len;
        return true;
    }
    case 4:
        v.kind = Value::Array;
        for (uint64_t i = 0; i < len; ++i)
        {
            Value e;
            if (!cborItem(r, e, depth + 1)) return false;
            v.arr.push_back(std::move(e));
        }
        return true;
    case 5:
        v.kind = Value::Object;
        for (uint64_t i = 0; i < len; ++i)
        {
            Value k, val;
            if (!cborItem(r, k, depth + 1) || !cborItem(r, val, depth + 1)) return false;
            if (k.kind == Value::String)
                v.obj[k.str] = std::move(val);
        }
        return true;
    case 6: // tag：忽略标签，继续读值
        return cborItem(r, v, depth + 1);
    case 7:
        if (info == 20) { v.kind = Value::Bool; v.b = false; return true; }
        if (info == 21) { v.kind = Value::Bool; v.b = true; return true; }
        if (info == 22 || info == 23) { v.kind = Value::Null; return true; }
        if (info == 25)
        {
            const uint16_t h = r.u16();
            if (!r.ok) return false;
            const int sign = (h >> 15) & 1;
            const int exp = (h >> 10) & 0x1F;
            const int mant = h & 0x3FF;
            double val = 0;
            if (exp == 0)
                val = mant / 1024.0 * std::pow(2.0, -14.0);
            else if (exp == 31)
                val = mant ? std::nan("") : INFINITY;
            else
                val = (1.0 + mant / 1024.0) * std::pow(2.0, exp - 15.0);
            v.kind = Value::Number;
            v.num = sign ? -val : val;
            return true;
        }
        if (info == 26) { v.kind = Value::Number; v.num = r.f32(); return r.ok; }
        if (info == 27) { v.kind = Value::Number; v.num = r.f64(); return r.ok; }
        return false;
    default:
        return false;
    }
}

bool msgpackItem(Reader &r, Value &v, int depth);

bool msgpackLength(Reader &r, uint8_t lenBytes, uint64_t &len)
{
    if (lenBytes == 1) { len = r.u8(); return r.ok; }
    if (lenBytes == 2) { len = r.u16(); return r.ok; }
    if (lenBytes == 4) { len = r.u32(); return r.ok; }
    if (lenBytes == 8) { len = r.u64(); return r.ok; }
    r.ok = false;
    return false;
}

bool msgpackItem(Reader &r, Value &v, int depth)
{
    if (depth > 24 || !r.ok)
        return false;
    const uint8_t b = r.u8();
    if (!r.ok) return false;
    if (b <= 0x7F) { v.kind = Value::Number; v.num = b; return true; }
    if (b >= 0xE0) { v.kind = Value::Number; v.num = static_cast<double>(b) - 256.0; return true; }
    if ((b & 0xE0) == 0xA0)
    {
        const size_t n = b & 0x1F;
        if (!r.need(n)) return false;
        v.kind = Value::String;
        v.str.assign(reinterpret_cast<const char *>(r.p), n);
        r.p += n;
        return true;
    }
    if ((b & 0xF0) == 0x80 || (b & 0xF0) == 0x90)
    {
        const size_t n = b & 0x0F;
        if ((b & 0xF0) == 0x80)
        {
            v.kind = Value::Object;
            for (size_t i = 0; i < n; ++i)
            {
                Value k, val;
                if (!msgpackItem(r, k, depth + 1) || !msgpackItem(r, val, depth + 1)) return false;
                if (k.kind == Value::String) v.obj[k.str] = std::move(val);
            }
        }
        else
        {
            v.kind = Value::Array;
            for (size_t i = 0; i < n; ++i)
            {
                Value e;
                if (!msgpackItem(r, e, depth + 1)) return false;
                v.arr.push_back(std::move(e));
            }
        }
        return true;
    }
    switch (b)
    {
    case 0xC0: v.kind = Value::Null; return true;
    case 0xC2: v.kind = Value::Bool; v.b = false; return true;
    case 0xC3: v.kind = Value::Bool; v.b = true; return true;
    case 0xCA: v.kind = Value::Number; v.num = r.f32(); return r.ok;
    case 0xCB: v.kind = Value::Number; v.num = r.f64(); return r.ok;
    case 0xCC: v.kind = Value::Number; v.num = r.u8(); return r.ok;
    case 0xCD: v.kind = Value::Number; v.num = r.u16(); return r.ok;
    case 0xCE: v.kind = Value::Number; v.num = r.u32(); return r.ok;
    case 0xCF: v.kind = Value::Number; v.num = static_cast<double>(r.u64()); return r.ok;
    case 0xD0: v.kind = Value::Number; v.num = static_cast<int8_t>(r.u8()); return r.ok;
    case 0xD1: v.kind = Value::Number; v.num = static_cast<int16_t>(r.u16()); return r.ok;
    case 0xD2: v.kind = Value::Number; v.num = static_cast<int32_t>(r.u32()); return r.ok;
    case 0xD3: v.kind = Value::Number; v.num = static_cast<double>(static_cast<int64_t>(r.u64())); return r.ok;
    default: break;
    }
    if (b == 0xC4 || b == 0xC5 || b == 0xC6)
    {
        uint64_t n = 0;
        if (b == 0xC4) n = r.u8();
        else if (b == 0xC5) n = r.u16();
        else n = r.u32();
        if (!r.ok || !r.need(static_cast<size_t>(n))) return false;
        v.kind = Value::String;
        v.str.assign(reinterpret_cast<const char *>(r.p), static_cast<size_t>(n));
        r.p += n;
        return true;
    }
    if (b == 0xDE || b == 0xDF || b == 0xDC || b == 0xDD)
    {
        uint64_t n = 0;
        if (b == 0xDE) n = r.u16();
        else if (b == 0xDF) n = r.u32();
        else if (b == 0xDC) n = r.u32();
        else n = r.u16();
        Value tmp;
        tmp.kind = (b == 0xDE || b == 0xDF) ? Value::Object : Value::Array;
        for (uint64_t i = 0; i < n; ++i)
        {
            if (tmp.kind == Value::Object)
            {
                Value k, val;
                if (!msgpackItem(r, k, depth + 1) || !msgpackItem(r, val, depth + 1)) return false;
                if (k.kind == Value::String) tmp.obj[k.str] = std::move(val);
            }
            else
            {
                Value e;
                if (!msgpackItem(r, e, depth + 1)) return false;
                tmp.arr.push_back(std::move(e));
            }
        }
        v = std::move(tmp);
        return true;
    }
    return false;
}

void flatten(const std::string &prefix, const Value &v, double t,
             std::map<std::string, std::vector<std::pair<double, double>>> &out)
{
    if (v.kind == Value::Object)
    {
        for (const auto &kv : v.obj)
        {
            if (kv.first == "time" || kv.first == "t" || kv.first == "timestamp")
                continue;
            const std::string name = prefix.empty() ? kv.first : prefix + "." + kv.first;
            flatten(name, kv.second, t, out);
        }
    }
    else if (v.kind == Value::Array)
    {
        for (size_t i = 0; i < v.arr.size(); ++i)
            flatten(prefix + "[" + std::to_string(i) + "]", v.arr[i], t, out);
    }
    else if (v.kind == Value::Number && !prefix.empty())
    {
        out[prefix].push_back({t, v.num});
    }
}

double sampleTime(const Value &v, double fallback)
{
    if (v.kind != Value::Object)
        return fallback;
    for (const char *k : {"time", "t", "timestamp"})
    {
        auto it = v.obj.find(k);
        if (it != v.obj.end() && it->second.kind == Value::Number)
            return it->second.num;
    }
    return fallback;
}

void collectSamples(const Value &root, std::vector<Value> &samples)
{
    if (root.kind == Value::Array)
    {
        bool allObj = true;
        for (const Value &e : root.arr)
            if (e.kind != Value::Object) { allObj = false; break; }
        if (allObj)
        {
            for (const Value &e : root.arr)
                samples.push_back(e);
            return;
        }
    }
    samples.push_back(root);
}

int32_t loadBinary(const char *path, const char *, const PsHostApi *host, void **dataset,
                   char *err, uint32_t errCap)
{
    psLoaderSetHost(host);
    QFile f(QString::fromUtf8(path));
    if (!f.open(QIODevice::ReadOnly))
    {
        psWriteErr(err, errCap, "无法打开二进制文件");
        return 1;
    }
    const uint64_t fileSize = static_cast<uint64_t>(f.size());
    QByteArray bytes;
    bytes.reserve(static_cast<int>(fileSize));
    while (!f.atEnd())
    {
        const QByteArray chunk = f.read(1 << 20);
        if (chunk.isEmpty())
            break;
        bytes.append(chunk);
        psLoaderProgress(static_cast<uint64_t>(bytes.size()) * 10 / std::max<uint64_t>(1, fileSize), 100);
    }
    if (bytes.isEmpty())
    {
        psWriteErr(err, errCap, "文件为空");
        return 1;
    }
    const QString suffix = QFileInfo(QString::fromUtf8(path)).suffix().toLower();
    const bool cbor = suffix == QLatin1String("cbor");
    Reader r{reinterpret_cast<const uint8_t *>(bytes.constData()),
             reinterpret_cast<const uint8_t *>(bytes.constData()) + bytes.size(), true};

    std::vector<Value> roots;
    const uint8_t *parseBegin = r.p;
    while (r.p < r.end && r.ok && roots.size() < 1'000'000)
    {
        psLoaderProgress(10 + static_cast<uint64_t>(r.p - parseBegin) * 85 /
                                  std::max<uint64_t>(1, static_cast<uint64_t>(r.end - parseBegin)), 100);
        Value v;
        const bool ok = cbor ? cborItem(r, v, 0) : msgpackItem(r, v, 0);
        if (!ok)
        {
            if (roots.empty())
            {
                psWriteErr(err, errCap, cbor ? "CBOR 解析失败" : "MessagePack 解析失败");
                return 2;
            }
            break;
        }
        roots.push_back(std::move(v));
    }
    if (roots.empty())
    {
        psWriteErr(err, errCap, "没有可解析的记录");
        return 2;
    }

    std::vector<Value> samples;
    for (const Value &root : roots)
        collectSamples(root, samples);
    std::map<std::string, std::vector<std::pair<double, double>>> acc;
    double index = 0;
    for (const Value &s : samples)
    {
        const double t = sampleTime(s, index++);
        flatten("", s, t, acc);
    }
    if (acc.empty())
    {
        psWriteErr(err, errCap, "没有数值字段");
        return 3;
    }

    std::vector<PsChannelData> data;
    data.reserve(acc.size());
    for (auto &kv : acc)
    {
        PsChannelData ch;
        ch.name = (cbor ? "cbor." : "msgpack.") + kv.first;
        for (const auto &p : kv.second)
        {
            ch.t.push_back(p.first);
            ch.v.push_back(p.second);
        }
        data.push_back(std::move(ch));
    }
    std::string perr;
    PsDataset *ds = psBuildDataset(data, perr, host);
    if (!ds)
    {
        psWriteErr(err, errCap, perr);
        return 4;
    }
    *dataset = ds;
    return 0;
}

const PsLoaderVtable kBinaryLoader = {
    PS_EXTENSION_ABI_VERSION,
    "data_loader_binary",
    "CBOR / MessagePack",
    "CBOR/MessagePack (*.cbor *.msgpack *.mpk);;全部 (*)",
    ".cbor,.msgpack,.mpk",
    "CBOR / MessagePack",
    0,
    0,
    {0, 0, 0, 0, 0, 0},
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    loadBinary,
};

const PsDataSourceVtable *binaryInputVtable()
{
    static const PsDataSourceVtable vt = ps::VtableBuilder::make<LegacyFileInput<&kBinaryLoader>>(R"({"id":"data_loader_binary","name":"CBOR / MessagePack","version":"1.0.0","sdk":"1.0","kind":"file"})");
    return &vt;
}

const PsExtensionDescriptor kBinaryExtension = {
    PS_EXTENSION_ABI_VERSION,
    R"({"id":"data_loader_binary","name":"CBOR / MessagePack","version":"1.0.0","sdk":"1.0","family":"data_source","kind":"file","entry":"ps_extension_entry_data_loader_binary"})",
    PS_FAMILY_DATA_SOURCE,
    PS_CAP_FILE | PS_CAP_CACHE,
    &kBinaryLoader,
    nullptr,
    nullptr,
    binaryInputVtable(),
};

} // namespace

extern "C" const PsExtensionDescriptor *ps_extension_entry_data_loader_binary(void)
{
    return &kBinaryExtension;
}
