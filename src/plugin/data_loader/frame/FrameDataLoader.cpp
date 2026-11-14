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
};

struct Template
{
    uint32_t frameSize = 0;
    std::vector<uint8_t> sync;
    std::vector<Field> fields;
    int crcOffset = -1;       // -1 = 不校验
    uint16_t crcPoly = 0x1021;
    uint16_t crcInit = 0xFFFF;
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
    if (!t.frameSize || t.frameSize > 4096)
    {
        err = "frame_size 缺失或超出 1..4096";
        return false;
    }
    for (const QJsonValue &v : o.value(QStringLiteral("sync")).toArray())
        t.sync.push_back(static_cast<uint8_t>(v.toInt()));
    const QJsonObject crc = o.value(QStringLiteral("crc16_ccitt")).toObject();
    if (!crc.isEmpty())
    {
        t.crcOffset = crc.value(QStringLiteral("offset")).toInt(-1);
        t.crcPoly = static_cast<uint16_t>(crc.value(QStringLiteral("poly")).toInt(0x1021));
        t.crcInit = static_cast<uint16_t>(crc.value(QStringLiteral("init")).toInt(0xFFFF));
    }
    for (const QJsonValue &v : o.value(QStringLiteral("fields")).toArray())
    {
        const QJsonObject f = v.toObject();
        Field fd;
        fd.name = f.value(QStringLiteral("name")).toString().toStdString();
        if (fd.name.empty())
            continue;
        fd.unit = f.value(QStringLiteral("unit")).toString().toStdString();
        fd.byte = static_cast<uint32_t>(f.value(QStringLiteral("byte")).toInt());
        fd.bit = static_cast<uint32_t>(f.value(QStringLiteral("bit")).toInt());
        fd.bits = static_cast<uint32_t>(f.value(QStringLiteral("bits")).toInt(8));
        fd.signedInt = f.value(QStringLiteral("signed")).toBool(false);
        fd.floatVal = f.value(QStringLiteral("float")).toBool(false);
        fd.scale = f.value(QStringLiteral("scale")).toDouble(1.0);
        fd.offset = f.value(QStringLiteral("offset")).toDouble(0.0);
        fd.isTime = f.value(QStringLiteral("time")).toBool(false);
        if (fd.bits == 0 || fd.bits > 64)
        {
            err = "字段位宽非法";
            return false;
        }
        t.fields.push_back(fd);
    }
    if (t.fields.empty())
    {
        err = "fields 为空";
        return false;
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
    for (size_t off = 0; off + tpl.frameSize <= static_cast<size_t>(bytes.size());)
    {
        const uint8_t *frame = reinterpret_cast<const uint8_t *>(bytes.constData()) + off;
        bool match = true;
        for (size_t i = 0; i < tpl.sync.size(); ++i)
            if (frame[i] != tpl.sync[i]) { match = false; break; }
        if (!match)
        {
            ++off;
            continue;
        }
        if (tpl.crcOffset >= 0 && static_cast<uint32_t>(tpl.crcOffset) + 2 <= tpl.frameSize)
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
        double t = frameIndex;
        for (const Field &fd : tpl.fields)
        {
            if (fd.byte + (fd.bits + 7) / 8 > tpl.frameSize)
                continue;
            const double v = fieldValue(frame, fd);
            if (fd.isTime)
                t = v;
            else
                acc[fd.name].push_back({t, v});
        }
        ++frameIndex;
        off += tpl.frameSize;
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
