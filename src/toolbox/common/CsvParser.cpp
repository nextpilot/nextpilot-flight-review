#include "CsvParser.h"

#include "ParserHub.h"

#include <QByteArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QString>
#include <QStringList>

#include <cstring>
#include <vector>

namespace nextpilot::plotscope::ext
{

namespace
{

// 每批行数上限：C++ 侧缓冲恒定，与文件行数无关。
constexpr uint32_t kChunkRows = 4096;

// 解析配置（由宿主经 configure 传入，字段与 CsvFormatParser::schema() 一致）。
// 默认值：逗号分隔 / 第一行表头 / 第一列为时间列（秒）；不带 options 的 data.load 调用即此行为。
struct CsvConfig
{
    QChar delimiter = QLatin1Char(',');
    bool hasHeader = true;
    int skipRows = 0;
    int timeMode = 0;   // 0=行号, 1=指定列, 2=日期列+时间列合并
    int timeIndex = 0;  // timeMode=1 时的时间列下标
    int dateIndex = 0;  // timeMode=2
    int clockIndex = 1; // timeMode=2
};

// 从宿主透传的 JSON 解析出配置。只认 schema 声明的键；缺字段用默认。
CsvConfig configFromJson(const QByteArray &json)
{
    CsvConfig c;
    if (json.isEmpty())
        return c;
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    if (o.isEmpty())
        return c;

    const QString delim = o.value(QStringLiteral("delimiter")).toString();
    // "auto" 是对话框里"自动探测"的占位值：探测结果已预填成实际字符，
    // 若仍是 auto 说明没探测到，退回默认逗号（绝不能取 'a' 当分隔符）。
    if (!delim.isEmpty() && delim != QLatin1String("auto"))
        c.delimiter = delim.at(0);
    if (o.contains(QStringLiteral("has_header")))
        c.hasHeader = o.value(QStringLiteral("has_header")).toBool(true);
    c.skipRows = qMax(0, o.value(QStringLiteral("skip_rows")).toInt(0));

    const QJsonObject time = o.value(QStringLiteral("time")).toObject();
    const QString mode = time.value(QStringLiteral("mode")).toString(QStringLiteral("row"));
    if (mode == QLatin1String("column"))
    {
        c.timeMode = 1;
        c.timeIndex = qMax(0, time.value(QStringLiteral("index")).toInt(0));
    }
    else if (mode == QLatin1String("combined"))
    {
        c.timeMode = 2;
        c.dateIndex = qMax(0, time.value(QStringLiteral("date")).toInt(0));
        c.clockIndex = qMax(0, time.value(QStringLiteral("time")).toInt(1));
    }
    return c;
}

// 解析器实例状态：只保留"跨 feed 的残余字节"与列缓冲，不含任何存储。
//
// 内存纪律：常驻 = 一个分块缓冲（kChunkRows 行 × 列数），与文件行数无关。
struct CsvCtx
{
    CsvConfig config;
    int skippedRows = 0;          // 已跳过的开头行数（skip_rows）
    int dataRowIndex = 0;         // 数据行序号（row 模式的时间基准，从 0 起）
    std::string pending;          // 未凑成完整行（含换行）的残余字节
    QByteArray lineBuf;           // 正在累积的一行
    QStringList headers;          // 表头列名（含第一列时间列的名字）
    QStringList valueNames;       // 数值列名（数据列对应的表头）
    std::vector<int> valueCellIndex; // 每个数据列对应的 cell 下标
    int valueCols = 0;
    bool headerDone = false;

    std::vector<int64_t> times;                    // 当前批的时间（纳秒）
    std::vector<std::vector<double>> values;       // 当前批各列的值
    std::vector<QByteArray> nameBytes;             // 列名 UTF-8（指针有效期用）
    uint64_t consumed = 0;                          // 已消费字节数（进度上报）
};

// 按配置的分隔符切一行（支持双引号包裹，兼容老实现）。
QStringList splitLine(const QString &line, QChar sep)
{
    QStringList out;
    QString cell;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i)
    {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('"'))
        {
            if (quoted && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"'))
            {
                cell.append(QLatin1Char('"'));
                ++i;
            }
            else
                quoted = !quoted;
        }
        else if (ch == sep && !quoted)
        {
            out.append(cell.trimmed());
            cell.clear();
        }
        else
            cell.append(ch);
    }
    out.append(cell.trimmed());
    return out;
}

// 时间列配置 → 产出 (时间秒, 该行是否作为数据行)。
// dataCols 是"哪些 cell 下标是数据列"，在表头阶段算好（见 buildValueColumns）。
//
// 时间模式：
//   - 0 row（默认）：时间 = 数据行序号（秒），所有列都是数据列；
//   - 1 column：time.index 指定列是时间（秒浮点），其余列是数据；
//   - 2 combined：date + clock 两列数值相加作为时间，其余列是数据。
bool parseCsvLine(CsvCtx *c, const QStringList &cells, std::vector<int64_t> &times,
                  std::vector<std::vector<double>> &values)
{
    if (cells.isEmpty())
        return true;

    const CsvConfig &cfg = c->config;
    double tSec = 0.0;
    if (cfg.timeMode == 0)
    {
        tSec = static_cast<double>(c->dataRowIndex);
    }
    else if (cfg.timeMode == 1)
    {
        if (cfg.timeIndex >= cells.size())
            return true;
        const QString s = cells.at(cfg.timeIndex).trimmed();
        if (s.isEmpty())
            return true;
        tSec = s.toDouble();
    }
    else // combined
    {
        if (cfg.dateIndex >= cells.size() || cfg.clockIndex >= cells.size())
            return true;
        tSec = cells.at(cfg.dateIndex).trimmed().toDouble()
             + cells.at(cfg.clockIndex).trimmed().toDouble();
    }

    // c->valueCellIndex[i] = 第 i 个数据列对应的 cell 下标（表头阶段算好）。
    for (int col = 0; col < c->valueCols; ++col)
    {
        const int cellIdx = c->valueCellIndex.at(col);
        const double v = (cellIdx >= 0 && cellIdx < cells.size())
                       ? cells.at(cellIdx).trimmed().toDouble() : 0.0;
        values[col].push_back(v);
    }
    times.push_back(static_cast<int64_t>(tSec * 1000000000.0));
    ++c->dataRowIndex;
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
    // 最终表名由宿主按扩展层声明的策略映射（见 IMessageParser::preferredTableName）。
    batch.table = "csv";
    batch.time_ns = c->times.data();
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
    // skip_rows：跳过开头 N 行（在表头判定之前）。
    if (c->skippedRows < c->config.skipRows)
    {
        ++c->skippedRows;
        return true;
    }

    const QStringList cells = splitLine(line, c->config.delimiter);

    if (!c->headerDone)
    {
        // 无表头模式：用第一行的列数推断结构，生成 Column 1..N，
        // 并把这一行也作为数据解析（存进 deferred 待表头建好后处理）。
        if (!c->config.hasHeader)
        {
            if (cells.size() < 2)
            {
                if (err)
                {
                    err->code = 3;
                    std::strncpy(err->message, "CSV 列数不足", sizeof(err->message) - 1);
                    err->message[sizeof(err->message) - 1] = '\0';
                }
                return false;
            }
            c->headers.clear();
            for (int i = 0; i < cells.size(); ++i)
                c->headers.push_back(QStringLiteral("Column %1").arg(i + 1));
        }
        else
        {
            c->headers = cells;
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
        }

        // 建立"数据列 → cell 下标"的映射（时间列不作为数据列）。
        c->valueCellIndex.clear();
        c->valueNames.clear();
        for (int cellIdx = 0; cellIdx < c->headers.size(); ++cellIdx)
        {
            bool isTime = false;
            if (c->config.timeMode == 1)
                isTime = (cellIdx == c->config.timeIndex);
            else if (c->config.timeMode == 2)
                isTime = (cellIdx == c->config.dateIndex || cellIdx == c->config.clockIndex);
            else if (c->config.timeMode == 0 && c->config.hasHeader)
                isTime = (cellIdx == 0); // row 模式 + 有表头：第一列为时间列
            // row 模式 + 无表头：所有列都是数据列（时间用行号）。
            if (!isTime)
            {
                c->valueCellIndex.push_back(cellIdx);
                c->valueNames.push_back(c->headers.at(cellIdx));
            }
        }
        c->valueCols = static_cast<int>(c->valueCellIndex.size());
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
        c->values.assign(static_cast<size_t>(c->valueCols), std::vector<double>());
        c->headerDone = true;

        // 无表头：第一行的 cells 也是第一条数据，补解析它。
        if (!c->config.hasHeader && !parseCsvLine(c, cells, c->times, c->values))
            return false;
        if (c->times.size() >= kChunkRows)
            return emitBatch(c, sink, err);
        return true;
    }

    if (!parseCsvLine(c, cells, c->times, c->values))
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
                c->valueCellIndex.clear();
                c->valueCols = 0;
                c->headerDone = false;
                c->skippedRows = 0;
                c->dataRowIndex = 0;
                c->times.clear();
                c->values.clear();
                c->nameBytes.clear();
                c->consumed = 0;
                // config 保留：复用的下一个文件沿用同一套解析参数。
            }
        };

        v.info = [](void *) -> const PsParserInfo * { return &CsvParser::parserInfo(); };
        v.capabilities = [](void *) -> uint64_t { return 0; };

        // configure：把对话框收集的解析参数灌进本实例（在 create 后、feed 前调用）。
        v.configure = [](void *ctx, const char *opts_json, PsError *err) -> int32_t {
            auto *c = static_cast<CsvCtx *>(ctx);
            if (!c)
            {
                if (err)
                {
                    err->code = 1;
                    std::strncpy(err->message, "configure: 非法实例", sizeof(err->message) - 1);
                    err->message[sizeof(err->message) - 1] = '\0';
                }
                return -1;
            }
            c->config = configFromJson(QByteArray(opts_json ? opts_json : ""));
            return 0;
        };

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

} // namespace nextpilot::plotscope::ext

// 静态自注册：与外部插件走同一张 ParserHub。
PS_REGISTER_PARSER(::nextpilot::plotscope::ext::CsvParser)
