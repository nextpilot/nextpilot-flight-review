#include "CustomProtocolParser.h"

#include "ParserHub.h"
#include "protocol/SequentialCodec.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace nextpilot::plotscope::ext
{
namespace
{

// 解析器实例状态。模板从 configure 传入（v2 参数槽），绝不读全局文件——
// 否则无法单测、无法重放、多实例串味（ARCHITECTURE §3.2 硬约束）。
struct Ctx
{
    QByteArray bytes;       // 累积的全部输入字节
    QByteArray templateJson; // configure 传入的协议模板
    bool configured = false;
    std::vector<std::string> nameBytes; // PsFrameColumn.name 的 utf8 副本，回调期间有效
    std::vector<std::string> unitBytes;
    // 每列按原生类型各存一段连续内存（bytesCopy 的字节视图），回调期间指针有效。
    std::vector<std::vector<uint8_t>> values;
    std::vector<std::vector<std::string>> textBytes; // TEXT 列的字符串本体
    std::vector<std::vector<const char *>> textPtrs; // TEXT 列给 SPI 的 const char* 数组
    std::vector<int64_t> times;                     // 微秒
};

void setErr(PsError *err, int32_t code, const char *msg)
{
    if (!err)
        return;
    err->code = code;
    std::strncpy(err->message, msg, sizeof(err->message) - 1);
    err->message[sizeof(err->message) - 1] = '\0';
}

// TypedVec 的列类型 → SPI 列类型（一一对应，无降级）。
PsValueType spiType(const nextpilot::plotscope::protocol::TypedVec &col)
{
    using nextpilot::plotscope::protocol::ValueType;
    switch (col.type)
    {
    case ValueType::U8: return PS_VALUE_U8;
    case ValueType::I8: return PS_VALUE_I8;
    case ValueType::U16: return PS_VALUE_U16;
    case ValueType::I16: return PS_VALUE_I16;
    case ValueType::U32: return PS_VALUE_U32;
    case ValueType::I32: return PS_VALUE_I32;
    case ValueType::U64: return PS_VALUE_U64;
    case ValueType::I64: return PS_VALUE_I64;
    case ValueType::F32: return PS_VALUE_F32;
    case ValueType::F64: return PS_VALUE_F64;
    case ValueType::Bool: return PS_VALUE_BOOL;
    case ValueType::Bcd: return PS_VALUE_U32;
    case ValueType::Text: return PS_VALUE_TEXT;
    }
    return PS_VALUE_F64;
}

// 把解码结果吐成一批（每条通道一列，共享时间轴）。
bool emitBatch(const nextpilot::plotscope::protocol::ProtocolDecodeResult &decoded, Ctx *c,
          const PsParserSink *sink, PsError *err)
{
    if (decoded.channels.isEmpty())
        return true;

    c->nameBytes.clear();
    c->unitBytes.clear();
    c->times.clear();
    c->values.clear();   // vector<vector<...>> 由列类型各自持有
    c->textPtrs.clear();
    c->values.reserve(decoded.channels.size());

    // 时间轴：ticks 按 ticksPerSecond 换成整数微秒，全程整数运算。
    const auto &first = decoded.channels.first();
    c->times.resize(first.t.size());
    for (size_t i = 0; i < first.t.size(); ++i)
        c->times[i] = static_cast<int64_t>(first.t[i]) * 1000000 / decoded.ticksPerSecond;

    std::vector<PsFrameColumn> cols;
    cols.reserve(decoded.channels.size());
    for (int i = 0; i < decoded.channels.size(); ++i)
    {
        const auto &ch = decoded.channels.at(i);
        c->nameBytes.push_back(ch.name);
        c->unitBytes.push_back(ch.unit);
    }

    // 每列按原生类型单独存一段连续内存，指针在回调期间有效。
    for (int i = 0; i < decoded.channels.size(); ++i)
    {
        const auto &ch = decoded.channels.at(i);
        const PsValueType vt = spiType(ch.v);
        PsFrameColumn col{};
        col.name = c->nameBytes[static_cast<size_t>(i)].c_str();
        col.unit = c->unitBytes[static_cast<size_t>(i)].empty()
                       ? nullptr
                       : c->unitBytes[static_cast<size_t>(i)].c_str();
        col.type = vt;
        col.nulls = nullptr;
        if (vt == PS_VALUE_TEXT)
        {
            c->textPtrs.emplace_back();
            auto &ptrs = c->textPtrs.back();
            auto &texts = c->textBytes.emplace_back();
            texts = ch.v.textCopy();
            ptrs.reserve(texts.size());
            for (auto &s : texts)
                ptrs.push_back(s.c_str());
            col.values = ptrs.data();
        }
        else
        {
            c->values.push_back(ch.v.bytesCopy());
            col.values = c->values.back().data();
        }
        cols.push_back(col);
    }

    PsFrameBatch batch{};
    batch.table = "custom"; // 表名由宿主按驱动策略改写
    batch.time_us = c->times.empty() ? nullptr : c->times.data();
    batch.rows = static_cast<uint32_t>(c->times.size());
    batch.columns = static_cast<uint32_t>(cols.size());
    batch.column_data = cols.empty() ? nullptr : cols.data();

    const int32_t rc = sink->frame_batch(sink->ctx, &batch);
    if (rc != 0)
    {
        setErr(err, 2, "宿主拒绝了一批数据");
        return false;
    }
    return true;
}

bool decodeAndEmit(Ctx *c, const PsParserSink *sink, PsError *err)
{
    if (!c->configured)
    {
        setErr(err, 3, "自定义协议未配置模板（configure 未调用）");
        return false;
    }
    // 用便捷入口从 JSON 模板做 validate + parse + decode（与编辑器同一份内核）。
    auto decoded = nextpilot::plotscope::protocol::decodeSequentialJson(c->bytes, c->templateJson);
    if (!decoded.ok)
    {
        setErr(err, 4, decoded.error.toUtf8().constData());
        return false;
    }
    return emitBatch(decoded, c, sink, err);
}

} // namespace

const PsParserInfo &CustomProtocolParser::parserInfo()
{
    static const PsParserInfo info = [] {
        PsParserInfo i{};
        i.abi_version = PS_PARSER_ABI_VERSION;
        i.id = "custom-protocol";
        i.display_name = "自定义顺序协议";
        // 无扩展名：自定义协议的数据文件是任意二进制，靠 format 参数显式选择。
        i.extensions = "";
        // 自定义协议无魔数、且需模板参数，不参与后缀/内容自动识别。
        i.can_sniff = 0;
        // 帧不定长，全流缓冲后一次解码——不支持增量产出。
        i.streaming = 0;
        return i;
    }();
    return info;
}

bool CustomProtocolParser::parseBuffer(const QByteArray &templateJson, const uint8_t *data,
                                       uint32_t size, PsParserSink *sink, PsError *err)
{
    const PsParserVtable *vt = parserVtable();
    void *ctx = vt->create();
    QByteArray opts = QJsonDocument(QJsonObject{{QStringLiteral("template"),
                                                 QString::fromUtf8(templateJson)}}).toJson();
    if (vt->configure(ctx, opts.constData(), err) != 0)
    {
        vt->destroy(ctx);
        return false;
    }
    const int32_t rc = vt->feed(ctx, data, size, sink, err);
    if (rc == 0)
        vt->finish(ctx, sink, err);
    vt->destroy(ctx);
    return rc == 0;
}

const PsParserVtable *CustomProtocolParser::parserVtable()
{
    static const PsParserVtable vt = [] {
        PsParserVtable v{};
        v.abi_version = PS_PARSER_ABI_VERSION;
        v.struct_size = sizeof(PsParserVtable);

        v.create = []() -> void * { return new Ctx(); };
        v.destroy = [](void *ctx) { delete static_cast<Ctx *>(ctx); };

        // 自定义协议无魔数，一律不靠嗅探认领（靠 format 参数）。
        v.sniff = [](void *, const uint8_t *, uint32_t) -> int32_t { return 0; };

        v.feed = [](void *ctx, const uint8_t *data, uint32_t size, const PsParserSink *sink,
                    PsError *err) -> int32_t {
            auto *c = static_cast<Ctx *>(ctx);
            if (!c || !data || !sink || !sink->frame_batch)
            {
                setErr(err, 1, "feed: 非法参数");
                return -1;
            }
            // 帧不定长，无法边喂边解——全流缓冲，finish 时一次解码。
            // 内存纪律：这里缓冲的是原始字节（输入本身就是这么大），不物化额外副本。
            c->bytes.append(reinterpret_cast<const char *>(data), static_cast<int>(size));
            if (sink->progress)
                sink->progress(sink->ctx, static_cast<int64_t>(c->bytes.size()), 0);
            return 0;
        };

        v.finish = [](void *ctx, const PsParserSink *sink, PsError *err) -> int32_t {
            auto *c = static_cast<Ctx *>(ctx);
            if (!c)
                return 0;
            return decodeAndEmit(c, sink, err) ? 0 : -1;
        };

        v.reset = [](void *ctx) {
            auto *c = static_cast<Ctx *>(ctx);
            if (c)
                c->bytes.clear();
            // configured/templateJson 保留：复用的实例沿用同一模板。
        };

        v.info = [](void *) -> const PsParserInfo * { return &CustomProtocolParser::parserInfo(); };
        v.capabilities = [](void *) -> uint64_t { return 0; };

        // configure：从 options JSON 取 template（协议模板文本）。
        v.configure = [](void *ctx, const char *opts_json, PsError *err) -> int32_t {
            auto *c = static_cast<Ctx *>(ctx);
            if (!c)
            {
                setErr(err, 1, "configure: 非法实例");
                return -1;
            }
            const QJsonObject o = QJsonDocument::fromJson(opts_json ? opts_json : "").object();
            const QString tpl = o.value(QStringLiteral("template")).toString();
            if (tpl.isEmpty())
            {
                setErr(err, 2, "configure: 缺少 template 参数");
                return -1;
            }
            c->templateJson = tpl.toUtf8();
            c->configured = true;
            return 0;
        };

        return v;
    }();
    return &vt;
}

} // namespace nextpilot::plotscope::ext

PS_REGISTER_PARSER(::nextpilot::plotscope::ext::CustomProtocolParser)
