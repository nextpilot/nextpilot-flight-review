#include "PspxParser.h"

#include "ParserRegistry.h"

#include <cstring>
#include <vector>

namespace ng::ext
{

namespace
{

// 解析器实例状态：只保留"跨 feed 的残余字节"，不含任何存储。
struct PspxCtx
{
    std::vector<uint8_t> pending; // 尚未凑齐一条记录的残余字节
    uint64_t consumed = 0;        // 已消费字节数（进度上报）
    bool headerDone = false;      // 头部是否已解析（流式：第二次 feed 不再查魔数）
    uint32_t declaredRows = 0;    // 头部声明的行数
    size_t bodyStart = 0;         // 记录区在 pending 中的起始偏移（头部之后）
};

// 小端读取助手。
inline bool readU32(const uint8_t *p, uint32_t avail, uint32_t &out)
{
    if (avail < 4)
        return false;
    out = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
          | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    return true;
}

inline bool readI64(const uint8_t *p, uint32_t avail, int64_t &out)
{
    if (avail < 8)
        return false;
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i)
        v = (v << 8) | p[i];
    out = static_cast<int64_t>(v);
    return true;
}

inline bool readF64(const uint8_t *p, uint32_t avail, double &out)
{
    if (avail < 8)
        return false;
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i)
        v = (v << 8) | p[i];
    std::memcpy(&out, &v, sizeof(double));
    return true;
}

void writeError(PsError *err, const char *msg)
{
    if (!err)
        return;
    err->code = 1;
    std::strncpy(err->message, msg, sizeof(err->message) - 1);
    err->message[sizeof(err->message) - 1] = '\0';
}

// 记录一条：i64 time_us + f64 value
constexpr uint32_t kRecordSize = 16;

} // namespace

const PsParserInfo &PspxParser::parserInfo()
{
    static const PsParserInfo info = [] {
        PsParserInfo i{};
        i.abi_version = PS_PARSER_ABI_VERSION;
        i.id = "pspx";
        i.display_name = "PSPX 测试格式";
        i.extensions = ".pspx";
        i.can_sniff = 1;
        i.streaming = 1;
        return i;
    }();
    return info;
}

const PsParserVtable *PspxParser::parserVtable()
{
    static const PsParserVtable vt = [] {
        PsParserVtable v{};
        v.abi_version = PS_PARSER_ABI_VERSION;
        v.struct_size = sizeof(PsParserVtable);

        v.create = []() -> void * { return new PspxCtx(); };
        v.destroy = [](void *ctx) { delete static_cast<PspxCtx *>(ctx); };

        // 探测：魔数 "PSPX"
        v.sniff = [](void *ctx, const uint8_t *data, uint32_t size) -> int32_t {
            Q_UNUSED(ctx);
            if (!data || size < 4)
                return 0;
            return (data[0] == 'P' && data[1] == 'S' && data[2] == 'P' && data[3] == 'X') ? 1 : 0;
        };

        // 喂数据：把字节喂进来，吐出批。
        v.feed = [](void *ctx, const uint8_t *data, uint32_t size, const PsParserSink *sink,
                    PsError *err) -> int32_t {
            auto *c = static_cast<PspxCtx *>(ctx);
            if (!c || !data || !sink || !sink->frame_batch)
            {
                writeError(err, "feed: 非法参数");
                return -1;
            }

            // 累积到 pending，然后按记录切分。
            c->pending.insert(c->pending.end(), data, data + size);
            c->consumed += size;

            const uint8_t *buf = c->pending.data();
            uint32_t avail = static_cast<uint32_t>(c->pending.size());

            // 头：魔数 4 字节 + 行数 4 字节（只在首次 feed 时解析）
            if (!c->headerDone)
            {
                if (avail < 8)
                    return 0; // 等更多数据
                if (!(buf[0] == 'P' && buf[1] == 'S' && buf[2] == 'P' && buf[3] == 'X'))
                {
                    writeError(err, "pspx: 魔数不匹配");
                    return -1;
                }
                readU32(buf + 4, avail - 4, c->declaredRows);
                c->headerDone = true;
                c->bodyStart = 8; // 头部之后即记录区
            }
            const uint32_t rows = c->declaredRows;

            // 逐条切出记录
            std::vector<int64_t> times;
            std::vector<double> values;
            times.reserve(rows);
            values.reserve(rows);

            uint32_t pos = static_cast<uint32_t>(c->bodyStart);
            while (pos + kRecordSize <= avail)
            {
                int64_t t = 0;
                double v = 0;
                if (!readI64(buf + pos, avail - pos, t) || !readF64(buf + pos + 8, avail - pos - 8, v))
                    break;
                times.push_back(t);
                values.push_back(v);
                pos += kRecordSize;
            }

            // 吐批：表名固定 "pspx"，一列 f64
            if (!times.empty() && sink->frame_batch)
            {
                PsFrameColumn col{};
                col.name = "value";
                col.unit = nullptr;
                col.type = PS_VALUE_F64;
                col.values = values.data();
                col.nulls = nullptr;

                PsFrameBatch batch{};
                batch.table = "pspx";
                batch.time_us = times.data();
                batch.rows = static_cast<uint32_t>(times.size());
                batch.columns = 1;
                batch.column_data = &col;

                const int32_t rc = sink->frame_batch(sink->ctx, &batch);
                if (rc != 0)
                    return rc; // 宿主拒绝，停止本次 feed
            }

            if (sink->progress)
                sink->progress(sink->ctx, c->consumed, 0);

            // 保留未处理干净的尾巴；pending 里剩下的都是记录区，bodyStart 归零
            if (pos < avail)
                c->pending.erase(c->pending.begin(), c->pending.begin() + pos);
            else
                c->pending.clear();
            c->bodyStart = 0;
            return 0;
        };

        // 收尾：无残余
        v.finish = [](void *ctx, const PsParserSink *, PsError *err) -> int32_t {
            auto *c = static_cast<PspxCtx *>(ctx);
            if (c && !c->pending.empty())
            {
                // 尾部不足一条记录：视为格式错误
                writeError(err, "pspx: 结尾存在不完整记录");
                c->pending.clear();
                return -1;
            }
            return 0;
        };

        v.reset = [](void *ctx) {
            auto *c = static_cast<PspxCtx *>(ctx);
            if (c)
            {
                c->pending.clear();
                c->consumed = 0;
                c->headerDone = false;
                c->declaredRows = 0;
                c->bodyStart = 0;
            }
        };

        v.info = [](void *) -> const PsParserInfo * { return &PspxParser::parserInfo(); };
        v.capabilities = [](void *) -> uint64_t { return 0; };

        return v;
    }();
    return &vt;
}

bool PspxParser::parseBuffer(const uint8_t *data, uint32_t size, PsParserSink *sink, PsError *err)
{
    const PsParserVtable *vt = parserVtable();
    void *ctx = vt->create();
    const int32_t rc = vt->feed(ctx, data, size, sink, err);
    if (rc == 0)
        vt->finish(ctx, sink, err);
    vt->destroy(ctx);
    return rc == 0;
}

} // namespace ng::ext

// 静态自注册：与外部插件走同一张 ParserRegistry。
NG_REGISTER_PARSER(::ng::ext::PspxParser)
