#include "ps_plugin_api.h"
#include "LegacyFileInput.hpp"
#include "PsLoaderCommon.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

struct CrcCheck
{
    bool enabled = false;
    uint32_t from = 0;
    uint32_t to = 0;
    uint16_t poly = 0x1021;
    uint16_t init = 0xFFFF;
};

struct Field
{
    std::string name;
    std::string unit;
    uint32_t byte = 0;
    uint32_t bit = 0;
    uint32_t bits = 0;
    bool signedInt = false;
    bool floatVal = false;
    double scale = 1.0;
    double offset = 0.0;
    bool isTime = false;
    bool hidden = false;
    bool hasEquals = false;
    uint64_t equalsValue = 0;
    bool hasMin = false;
    bool hasMax = false;
    double minValue = 0.0;
    double maxValue = 0.0;
    CrcCheck check;
};

struct SyncByte
{
    uint32_t offset = 0;
    uint8_t value = 0;
};

struct LengthSpec
{
    bool enabled = false;
    uint32_t byte = 0;
    uint32_t bytes = 2;
    bool bigEndian = false;
    bool includesHeader = true;
    std::string fieldName;
};

struct Variant
{
    uint32_t id = 0;
    std::string name;
    std::vector<Field> fields;
};

struct Template
{
    uint32_t frameSize = 0;   // 固定帧长；变长帧时为上限
    LengthSpec length;
    std::vector<SyncByte> sync;
    uint32_t syncSpan = 0;
    std::vector<Field> fields;
    int crcOffset = -1;       // -1 = 不校验
    uint16_t crcPoly = 0x1021;
    uint16_t crcInit = 0xFFFF;
    bool hasVariants = false;
    uint32_t msgIdByte = 0;
    uint32_t msgIdBit = 0;
    uint32_t msgIdBits = 8;
    std::vector<Variant> variants;
};

uint16_t crc16(const uint8_t *data, size_t n, uint16_t poly, uint16_t init)
{
    uint16_t crc = init;
    for (size_t i = 0; i < n; ++i)
    {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ poly)
                                 : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}


bool parseFieldObject(const QJsonObject &f, Field &fd, std::string &err)
{
    fd.name = f.value(QStringLiteral("name")).toString().toStdString();
    if (fd.name.empty())
        return true;
    fd.unit = f.value(QStringLiteral("unit")).toString().toStdString();
    fd.byte = static_cast<uint32_t>(f.value(QStringLiteral("byte")).toInt());
    fd.bit = static_cast<uint32_t>(f.value(QStringLiteral("bit")).toInt());
    fd.bits = static_cast<uint32_t>(f.value(QStringLiteral("bits")).toInt(8));
    fd.signedInt = f.value(QStringLiteral("signed")).toBool(false);
    fd.floatVal = f.value(QStringLiteral("float")).toBool(false);
    fd.scale = f.value(QStringLiteral("scale")).toDouble(1.0);
    fd.offset = f.value(QStringLiteral("offset")).toDouble(0.0);
    fd.isTime = f.value(QStringLiteral("time")).toBool(false);
    fd.hidden = f.value(QStringLiteral("hidden")).toBool(false);
    if (f.contains(QStringLiteral("equals")))
    {
        fd.hasEquals = true;
        fd.equalsValue = static_cast<uint64_t>(f.value(QStringLiteral("equals")).toDouble());
    }
    if (f.contains(QStringLiteral("min")))
    {
        fd.hasMin = true;
        fd.minValue = f.value(QStringLiteral("min")).toDouble();
    }
    if (f.contains(QStringLiteral("max")))
    {
        fd.hasMax = true;
        fd.maxValue = f.value(QStringLiteral("max")).toDouble();
    }
    const QJsonObject chk = f.value(QStringLiteral("check")).toObject();
    if (!chk.isEmpty() && chk.value(QStringLiteral("type")).toString() == QLatin1String("crc16"))
    {
        fd.check.enabled = true;
        fd.check.from = static_cast<uint32_t>(chk.value(QStringLiteral("from")).toInt());
        fd.check.to = static_cast<uint32_t>(chk.value(QStringLiteral("to")).toInt());
        fd.check.poly = static_cast<uint16_t>(chk.value(QStringLiteral("poly")).toInt(0x1021));
        fd.check.init = static_cast<uint16_t>(chk.value(QStringLiteral("init")).toInt(0xFFFF));
    }
    if (fd.bits == 0 || fd.bits > 64)
    {
        err = "字段位宽非法";
        return false;
    }
    return true;
}

bool parseTemplate(const QByteArray &json, Template &t, std::string &err)
{
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (doc.isNull() || !doc.isObject())
    {
        err = "协议模板 JSON 解析失败";
        return false;
    }
    const QJsonObject o = doc.object();
    t.frameSize = static_cast<uint32_t>(o.value(QStringLiteral("frame_size")).toInt());
    const QJsonObject lj = o.value(QStringLiteral("length")).toObject();
    if (!lj.isEmpty())
    {
        t.length.enabled = true;
        t.length.byte = static_cast<uint32_t>(lj.value(QStringLiteral("byte")).toInt(2));
        t.length.fieldName = lj.value(QStringLiteral("field")).toString().toStdString();
        t.length.bytes = static_cast<uint32_t>(lj.value(QStringLiteral("bytes")).toInt(2));
        t.length.bigEndian = lj.value(QStringLiteral("endian")).toString() == QLatin1String("big");
        t.length.includesHeader = lj.value(QStringLiteral("includes_header")).toBool(true);
        if (t.length.bytes < 1 || t.length.bytes > 4 || t.length.byte + t.length.bytes > 4096)
        {
            err = "length 字段非法";
            return false;
        }
    }
    if (t.frameSize > 4096)
    {
        err = "frame_size 超出 4096";
        return false;
    }
    if (!t.frameSize && !t.length.enabled)
    {
        err = "固定帧需要 frame_size，变长帧需要 length";
        return false;
    }
    if (t.length.enabled && !t.frameSize)
        t.frameSize = 4096;
    int syncIndex = 0;
    for (const QJsonValue &v : o.value(QStringLiteral("sync")).toArray())
    {
        SyncByte sb;
        if (v.isObject())
        {
            const QJsonObject so = v.toObject();
            sb.offset = static_cast<uint32_t>(so.value(QStringLiteral("offset")).toInt(syncIndex));
            sb.value = static_cast<uint8_t>(so.value(QStringLiteral("value")).toInt());
        }
        else
        {
            sb.offset = static_cast<uint32_t>(syncIndex);
            sb.value = static_cast<uint8_t>(v.toInt());
        }
        t.sync.push_back(sb);
        t.syncSpan = std::max(t.syncSpan, sb.offset + 1);
        ++syncIndex;
    }
    const QJsonObject crc = o.value(QStringLiteral("crc16_ccitt")).toObject();
    if (!crc.isEmpty())
    {
        t.crcOffset = crc.value(QStringLiteral("offset")).toInt(-1);
        t.crcPoly = static_cast<uint16_t>(crc.value(QStringLiteral("poly")).toInt(0x1021));
        t.crcInit = static_cast<uint16_t>(crc.value(QStringLiteral("init")).toInt(0xFFFF));
    }
    for (const QJsonValue &v : o.value(QStringLiteral("fields")).toArray())
    {
        Field fd;
        if (!parseFieldObject(v.toObject(), fd, err))
            return false;
        if (!fd.name.empty())
            t.fields.push_back(fd);
    }
    const QJsonObject mid = o.value(QStringLiteral("msgid")).toObject();
    if (!mid.isEmpty())
    {
        t.hasVariants = true;
        t.msgIdByte = static_cast<uint32_t>(mid.value(QStringLiteral("byte")).toInt());
        t.msgIdBit = static_cast<uint32_t>(mid.value(QStringLiteral("bit")).toInt());
        t.msgIdBits = static_cast<uint32_t>(mid.value(QStringLiteral("bits")).toInt(8));
        const QString fieldName = mid.value(QStringLiteral("field")).toString();
        if (!fieldName.isEmpty())
        {
            bool found = false;
            for (const Field &fd : t.fields)
                if (fd.name == fieldName.toStdString())
                {
                    t.msgIdByte = fd.byte;
                    t.msgIdBit = fd.bit;
                    t.msgIdBits = fd.bits;
                    found = true;
                    break;
                }
            if (!found)
            {
                err = "msgid.field 未找到";
                return false;
            }
        }
        for (const QJsonValue &vv : o.value(QStringLiteral("variants")).toArray())
        {
            const QJsonObject vo = vv.toObject();
            Variant var;
            var.id = static_cast<uint32_t>(vo.value(QStringLiteral("id")).toInt());
            var.name = vo.value(QStringLiteral("name")).toString().toStdString();
            var.fields = t.fields;
            for (const QJsonValue &fv : vo.value(QStringLiteral("fields")).toArray())
            {
                Field fd;
                if (!parseFieldObject(fv.toObject(), fd, err))
                    return false;
                if (!fd.name.empty())
                    var.fields.push_back(fd);
            }
            t.variants.push_back(std::move(var));
        }
        if (t.variants.empty())
        {
            err = "variants 为空";
            return false;
        }
    }
    if (t.fields.empty() && t.variants.empty())
    {
        err = "fields 为空";
        return false;
    }
    if (t.length.enabled && !t.length.fieldName.empty())
    {
        bool found = false;
        for (const Field &fd : t.fields)
        {
            if (fd.name == t.length.fieldName)
            {
                t.length.byte = fd.byte;
                found = true;
                break;
            }
        }
        if (!found)
        {
            err = "length.field 未找到";
            return false;
        }
    }
    return true;
}

uint64_t readBits(const uint8_t *frame, uint32_t bitOffset, uint32_t bits)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < bits; ++i)
    {
        const uint32_t abs = bitOffset + i;
        const uint32_t byte = abs / 8;
        const uint32_t bit = abs % 8;
        if (frame[byte] & (1u << bit))
            v |= (1ull << i);
    }
    return v;
}

double fieldValue(const uint8_t *frame, const Field &f)
{
    if (f.floatVal)
    {
        if (f.bits == 32)
        {
            float v;
            std::memcpy(&v, frame + f.byte, 4);
            return v;
        }
        if (f.bits == 64)
        {
            double v;
            std::memcpy(&v, frame + f.byte, 8);
            return v;
        }
    }
    const uint64_t raw = readBits(frame, f.byte * 8 + f.bit, f.bits);
    double v = 0;
    if (f.signedInt && f.bits < 64 && (raw & (1ull << (f.bits - 1))))
        v = static_cast<double>(static_cast<int64_t>(raw | (~0ull << f.bits)));
    else
        v = static_cast<double>(raw);
    return v * f.scale + f.offset;
}

int32_t loadFrame(const char *path, const char *optsJson, const PsHostApi *host, void **dataset,
                  char *err, uint32_t errCap)
{
    const QByteArray opts = QByteArray::fromRawData(optsJson ? optsJson : "", optsJson ? int(std::strlen(optsJson)) : 0);
    const QJsonObject optsObj = QJsonDocument::fromJson(opts).object();
    QString protoPath = optsObj.value(QStringLiteral("protocol")).toString();
    if (protoPath.isEmpty())
    {
        psWriteErr(err, errCap, "需要 protocol 指向 .psproto 模板");
        return 1;
    }
    QFile pf(protoPath);
    if (!pf.open(QIODevice::ReadOnly))
    {
        psWriteErr(err, errCap, "无法打开协议模板");
        return 1;
    }
    Template tpl;
    std::string perr;
    if (!parseTemplate(pf.readAll(), tpl, perr))
    {
        psWriteErr(err, errCap, perr);
        return 2;
    }

    QFile f(QString::fromUtf8(path));
    if (!f.open(QIODevice::ReadOnly))
    {
        psWriteErr(err, errCap, "无法打开原始帧文件");
        return 1;
    }
    const QByteArray bytes = f.readAll();

    std::map<std::string, std::vector<std::pair<double, double>>> acc;
    double frameIndex = 0;
    uint64_t rejected = 0;
    for (size_t off = 0; off + std::max<size_t>(2, tpl.syncSpan ? tpl.syncSpan : 1) <= static_cast<size_t>(bytes.size());)
    {
        if (!tpl.length.enabled && off + static_cast<size_t>(tpl.frameSize) > static_cast<size_t>(bytes.size()))
            break;
        const uint8_t *frame = reinterpret_cast<const uint8_t *>(bytes.constData()) + off;
        bool match = true;
        for (const SyncByte &sb : tpl.sync)
            if (frame[sb.offset] != sb.value) { match = false; break; }
        if (!match)
        {
            ++off;
            continue;
        }
        uint32_t frameLen = tpl.frameSize;
        if (tpl.length.enabled)
        {
            if (off + tpl.length.byte + tpl.length.bytes > static_cast<size_t>(bytes.size()))
                break;
            uint32_t raw = 0;
            const uint8_t *lp = frame + tpl.length.byte;
            if (tpl.length.bigEndian)
                for (uint32_t i = 0; i < tpl.length.bytes; ++i) raw = (raw << 8) | lp[i];
            else
                for (uint32_t i = 0; i < tpl.length.bytes; ++i) raw |= uint32_t(lp[i]) << (8 * i);
            frameLen = tpl.length.includesHeader ? raw : raw + tpl.length.byte + tpl.length.bytes;
            if (!frameLen || frameLen > tpl.frameSize || off + frameLen > static_cast<size_t>(bytes.size()))
            {
                ++rejected;
                ++off;
                continue;
            }
        }
        if (tpl.crcOffset >= 0 && static_cast<uint32_t>(tpl.crcOffset) + 2 <= frameLen)
        {
            const uint16_t want = crc16(frame, tpl.crcOffset, tpl.crcPoly, tpl.crcInit);
            uint16_t got;
            std::memcpy(&got, frame + tpl.crcOffset, 2);
            if (got != want)
            {
                ++rejected;
                off += 1;
                continue;
            }
        }
        const std::vector<Field> *fields = &tpl.fields;
        const Variant *variant = nullptr;
        if (tpl.hasVariants)
        {
            const uint64_t msgId = readBits(frame, tpl.msgIdByte * 8 + tpl.msgIdBit, tpl.msgIdBits);
            for (const Variant &v : tpl.variants)
                if (v.id == msgId)
                {
                    variant = &v;
                    break;
                }
            if (!variant)
            {
                ++rejected;
                ++off;
                continue;
            }
            fields = &variant->fields;
        }
        bool valid = true;
        for (const Field &fd : *fields)
        {
            if (fd.byte + (fd.bits + 7) / 8 > frameLen)
                continue;
            const uint64_t raw = readBits(frame, fd.byte * 8 + fd.bit, fd.bits);
            if (fd.hasEquals && raw != fd.equalsValue)
            {
                valid = false;
                break;
            }
            const double v = fieldValue(frame, fd);
            if ((fd.hasMin && v < fd.minValue) || (fd.hasMax && v > fd.maxValue))
            {
                valid = false;
                break;
            }
            if (fd.check.enabled)
            {
                if (fd.check.from >= frameLen || fd.check.to >= frameLen || fd.check.to < fd.check.from)
                {
                    valid = false;
                    break;
                }
                const uint16_t want = crc16(frame + fd.check.from, fd.check.to - fd.check.from + 1,
                                           fd.check.poly, fd.check.init);
                if (raw != want)
                {
                    valid = false;
                    break;
                }
            }
        }
        if (!valid)
        {
            ++rejected;
            ++off;
            continue;
        }
        double t = frameIndex;
        for (const Field &fd : *fields)
        {
            if (fd.byte + (fd.bits + 7) / 8 > frameLen || fd.hidden)
                continue;
            const double v = fieldValue(frame, fd);
            if (fd.isTime)
                t = v;
            else
                acc[fd.name].push_back({t, v});
        }
        ++frameIndex;
        off += frameLen;
    }
    if (acc.empty())
    {
        psWriteErr(err, errCap, rejected ? "所有帧 CRC 校验失败" : "没有匹配的帧");
        return 3;
    }

    std::vector<PsChannelData> data;
    for (auto &kv : acc)
    {
        PsChannelData ch;
        ch.name = "frame." + kv.first;
        for (const auto &var : tpl.variants)
            for (const Field &fd : var.fields)
                if (fd.name == kv.first && !var.name.empty())
                    ch.name = "frame." + var.name + "." + kv.first;
        for (const Field &fd : tpl.fields)
            if (fd.name == kv.first)
                ch.unit = fd.unit;
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

int32_t frameSchema(char *out, uint32_t cap)
{
    const char *json = R"json([{"key":"protocol","label":"协议模板 .psproto","type":"file","filter":"协议模板 (*.psproto *.json)"}])json";
    if (!out || cap < std::strlen(json) + 1)
        return 1;
    std::strcpy(out, json);
    return 0;
}

const PsLoaderVtable kFrameLoader = {
    PS_PLUGIN_ABI_VERSION,
    "data_loader_frame",
    "帧协议模板",
    "原始帧 (*.bin *.raw *.frames);;全部 (*)",
    ".frames,.bin,.raw",
    "帧协议模板",
    0,
    1,
    {0, 0, 0, 0, 0, 0},
    frameSchema,
    nullptr,
    nullptr,
    nullptr,
    loadFrame,
};

const PsDataInputVtable *frameInputVtable()
{
    static const PsDataInputVtable vt = ps::VtableBuilder::make<LegacyFileInput<&kFrameLoader>>(R"({"id":"data_loader_frame","name":"帧协议模板","version":"1.0.0","sdk":"1.0","kind":"file"})");
    return &vt;
}

const PsPluginDescriptor kFramePlugin = {
    PS_PLUGIN_ABI_VERSION,
    R"({"id":"data_loader_frame","name":"帧协议模板","version":"1.0.0","sdk":"1.0","family":"data_input","kind":"file","entry":"ps_plugin_entry_data_loader_frame"})",
    PS_FAMILY_DATA_INPUT,
    PS_CAP_FILE | PS_CAP_CACHE,
    &kFrameLoader,
    nullptr,
    nullptr,
    frameInputVtable(),
};

} // namespace

extern "C" const PsPluginDescriptor *ps_plugin_entry_data_loader_frame(void)
{
    return &kFramePlugin;
}
