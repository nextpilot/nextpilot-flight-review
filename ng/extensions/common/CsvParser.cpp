#include "CsvParser.h"

#include "ParserRegistry.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstring>
#include <vector>

namespace ng::ext
{

namespace
{

// 每批行数上限：C++ 侧缓冲恒定，与文件行数无关。
constexpr uint32_t kChunkRows = 4096;

// 解析器实例状态：只保留"跨 feed 的残余字节"与列缓冲，不含任何存储。
//
// 内存纪律：常驻 = 一个分块缓冲（kChunkRows 行 × 列数），与文件行数无关。
struct CsvCtx
{
    std::string pending;          // 未凑成完整行（含换行）的残余字节
    QByteArray lineBuf;           // 正在累积的一行
    QStringList headers;          // 表头列名（含第一列时间列的名字）
    QStringList valueNames;       // 数值列名（= 表头去掉第一列）
    int valueCols = 0;
    bool headerDone = false;

    std::vector<int64_t> times;                    // 当前批的时间（微秒）
    std::vector<std::vector<double>> values;       // 当前批各列的值
    std::vector<QByteArray> nameBytes;             // 列名 UTF-8（指针有效期用）
    uint64_t consumed = 0;                          // 已消费字节数（进度上报）
};

// 解析一行：返回 false 表示格式错误。
//
// 规则（老 CsvExtension 的语义，逐条保留）：
//   - 空行跳过；
//   - 第一格为空视为跳过（老实现：cells.first().trimmed().isEmpty()）；
//   - 第一列是时间（秒，浮点），转微秒；
//   - 其余列按 f64 解析，解析不出按 0（老实现 toDouble() 的行为）。
bool parseCsvLine(const QStringList &cells, int valueCols, std::vector<int64_t> &times,
                  std::vector<std::vector<double>> &values)
{
    if (cells.isEmpty())
        return true;
    if (cells.first().trimmed().isEmpty())
        return true; // 跳过空行/无时间戳的行

    const double tSec = cells.first().trimmed().toDouble();
    times.push_back(static_cast<int64_t>(tSec * 1000000.0));

    for (int c = 0; c < valueCols; ++c)
        values[c].push_back((c + 1 < cells.size()) ? cells.at(c + 1).trimmed().toDouble() : 0.0);
    return true;
}

// 吐一批：列的原生字节布局与 PsFrameColumn 同构，零转换。
bool emitBatch(CsvCtx *c, const PsParserSink *sink, PsError *err)
{
    if (c->times.empty())
        return true;

    // 列名指针只在回调期间有效：utf8 副本必须活到 frame_batch 返回。
    c->nameBytes.clear();
    c->nameBytes.reserve(static_cast<size_t>(c->valueCols));
    std::vector<PsFrameColumn> cols;
    cols.reserve(static_cast<size_t>(c->valueCols));
    for (int i = 0; i < c->valueCols; ++i)
    {
        c->nameBytes.push_back(c->valueNames.at(i).toUtf8());
        PsFrameColumn col{};
        col.name = c->nameBytes.back().constData();
        col.unit = "";
        col.type = PS_VALUE_F64;
        col.values = c->values[static_cast<size_t>(i)].data();
        col.nulls = nullptr;
        cols.push_back(col);
    }

    PsFrameBatch batch{};
    // 表名固定 "csv"：解析器只认字节，不知道文件叫什么。
    // 最终表名由宿主按扩展层声明的策略映射（见 IFormatDriver::preferredTableName）。
    batch.table = "csv";
    batch.time_us = c->times.data();
    batch.rows = static_cast<uint32_t>(c->times.size());
    batch.columns = static_cast<uint32_t>(cols.size());
    batch.column_data = cols.empty() ? nullptr : cols.data();

    const int32_t rc = sink->frame_batch(sink->ctx, &batch);
    if (rc != 0)
    {
        if (err)
        {
            err->code = 2;
            std::strncpy(err->message, "宿主拒绝了一批数据", sizeof(err->message) - 1);
            err->message[sizeof(err->message) - 1] = '\0';
        }
        return false;
    }

    c->times.clear();
    for (auto &v : c->values)
        v.clear();
    return true;
}

// 处理一行完整文本（含可能的 \r）。返回 false 表示出错。
bool handleLine(CsvCtx *c, const QString &line, const PsParserSink *sink, PsError *err)
{
    if (!c->headerDone)
    {
        // 第一行是表头。
        c->headers = line.split(QLatin1Char(','));
        for (QString &h : c->headers)
            h = h.trimmed();
        if (c->headers.isEmpty() || c->headers.first().isEmpty())
        {
            if (err)
            {
                err->code = 3;
                std::strncpy(err->message, "CSV 无表头", sizeof(err->message) - 1);
                err->message[sizeof(err->message) - 1] = '\0';
            }
            return false;
        }
        // 第一列是时间（名字只作参考，不作为数值列），其余是数值列。
        c->valueCols = static_cast<int>(c->headers.size()) - 1;
        if (c->valueCols <= 0)
        {
            if (err)
            {
                err->code = 3;
                std::strncpy(err->message, "CSV 无数据列", sizeof(err->message) - 1);
                err->message[sizeof(err->message) - 1] = '\0';
            }
            return false;
        }
        c->valueNames.clear();
        for (int i = 1; i < c->headers.size(); ++i)
            c->valueNames.push_back(c->headers.at(i));
        c->values.assign(static_cast<size_t>(c->valueCols), std::vector<double>());
        c->headerDone = true;
        return true;
    }

    const QStringList cells = line.split(QLatin1Char(','));
    if (!parseCsvLine(cells, c->valueCols, c->times, c->values))
        return false;

    if (c->times.size() >= kChunkRows)
        return emitBatch(c, sink, err);
    return true;
}

} // namespace

const PsParserInfo &CsvParser::parserInfo()
{
    static const PsParserInfo info = [] {
        PsParserInfo i{};
        i.abi_version = PS_PARSER_ABI_VERSION;
        i.id = "csv";
        i.display_name = "CSV 表格";
        i.extensions = ".csv";
        // 纯文本没有魔数，嗅探不可靠 —— 只认后缀。
        i.can_sniff = 0;
        i.streaming = 1;
        return i;
    }();
    return info;
}

const PsParserVtable *CsvParser::parserVtable()
{
    static const PsParserVtable vt = [] {
        PsParserVtable v{};
        v.abi_version = PS_PARSER_ABI_VERSION;
        v.struct_size = sizeof(PsParserVtable);

        v.create = []() -> void * { return new CsvCtx(); };
        v.destroy = [](void *ctx) { delete static_cast<CsvCtx *>(ctx); };

        // 探测：CSV 是纯文本，没有魔数 —— 一律不认。
        // （否则任何文本文件都会被当成 CSV，宿主按后缀选即可。）
        v.sniff = [](void *ctx, const uint8_t *data, uint32_t size) -> int32_t {
            Q_UNUSED(ctx);
            Q_UNUSED(data);
            Q_UNUSED(size);
            return 0;
        };

        // 喂数据：按行切分（跨 feed 保留半行），解析成批。
        v.feed = [](void *ctx, const uint8_t *data, uint32_t size, const PsParserSink *sink,
                    PsError *err) -> int32_t {
            auto *c = static_cast<CsvCtx *>(ctx);
            if (!c || !data || !sink || !sink->frame_batch)
            {
                if (err)
                {
                    err->code = 1;
                    std::strncpy(err->message, "feed: 非法参数", sizeof(err->message) - 1);
                    err->message[sizeof(err->message) - 1] = '\0';
                }
                return -1;
            }

            c->consumed += size;
            c->pending.append(reinterpret_cast<const char *>(data), size);

            // 按 '\n' 切分；行尾 '\r' 去掉（兼容 CRLF）。
            size_t pos = 0;
            while (true)
            {
                const size_t nl = c->pending.find('\n', pos);
                if (nl == std::string::npos)
                    break;
                const QString line =
                    QString::fromUtf8(c->pending.data() + pos, static_cast<int>(nl - pos));
                pos = nl + 1;
                if (!handleLine(c, line.endsWith(QLatin1Char('\r'))
                                        ? line.chopped(1)
                                        : line,
                                sink, err))
                    return -1;
            }
            c->pending.erase(0, pos);

            if (sink->progress)
                sink->progress(sink->ctx, c->consumed, 0);
            return 0;
        };

        // 收尾：处理末尾没有换行的最后一行，并冲刷未满的批。
        v.finish = [](void *ctx, const PsParserSink *sink, PsError *err) -> int32_t {
            auto *c = static_cast<CsvCtx *>(ctx);
            if (!c)
                return 0;
            if (!c->pending.empty())
            {
                QString line = QString::fromUtf8(c->pending.data(),
                                                 static_cast<int>(c->pending.size()));
                c->pending.clear();
                if (line.endsWith(QLatin1Char('\r')))
                    line.chopped(1);
                if (!handleLine(c, line, sink, err))
                    return -1;
            }
            if (!c->headerDone)
            {
                if (err)
                {
                    err->code = 3;
                    std::strncpy(err->message, "CSV 数据不足（没有表头）", sizeof(err->message) - 1);
                    err->message[sizeof(err->message) - 1] = '\0';
                }
                return -1;
            }
            if (c->times.empty())
            {
                if (err)
                {
                    err->code = 4;
                    std::strncpy(err->message, "CSV 没有数据行", sizeof(err->message) - 1);
                    err->message[sizeof(err->message) - 1] = '\0';
                }
                return -1;
            }
            return emitBatch(c, sink, err) ? 0 : -1;
        };

        v.reset = [](void *ctx) {
            auto *c = static_cast<CsvCtx *>(ctx);
            if (c)
            {
                c->pending.clear();
                c->headers.clear();
                c->valueNames.clear();
                c->valueCols = 0;
                c->headerDone = false;
                c->times.clear();
                c->values.clear();
                c->nameBytes.clear();
                c->consumed = 0;
            }
        };

        v.info = [](void *) -> const PsParserInfo * { return &CsvParser::parserInfo(); };
        v.capabilities = [](void *) -> uint64_t { return 0; };

        return v;
    }();
    return &vt;
}

bool CsvParser::parseBuffer(const uint8_t *data, uint32_t size, PsParserSink *sink, PsError *err)
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
NG_REGISTER_PARSER(::ng::ext::CsvParser)
