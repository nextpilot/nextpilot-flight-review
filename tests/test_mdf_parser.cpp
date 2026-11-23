#include "MdfParser.h"
#include "ps_parser_spi.h"

#include <QDir>
#include <QFile>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

/*
 * T6.1 MDF 4.x 解析器验收（SPI 视图，headless）：
 *   - 元数据：id=mdf4 / streaming=0 / can_sniff=1
 *   - sniff 认头、拒非 MDF
 *   - 喂 fixture demo_compressed.mf4 全字节 → finish 出批，列/行/时间轴自洽
 *   - ##DZ 压缩块（含转置还原）能出数据，行数与 ##DT 原文一致
 */

static int g_fail = 0;
#define OK(cond, msg)                                                              \
    do                                                                             \
    {                                                                              \
        if (cond)                                                                  \
            std::printf("[ok] %s\n", msg);                                         \
        else                                                                       \
        {                                                                          \
            std::printf("[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__);           \
            ++g_fail;                                                              \
        }                                                                          \
    } while (0)

namespace
{
struct BatchView
{
    std::string table;
    uint32_t rows = 0;
    std::vector<std::string> cols;
    std::vector<std::string> units;
    int64_t t0 = 0;
    int64_t tLast = 0;
    double v0 = 0;
};

struct Collector
{
    std::vector<BatchView> batches;
    uint32_t totalRows = 0;
    bool monotonic = true;
    int64_t prev = 0;
    bool first = true;
};

int32_t onBatch(void *ctx, const PsFrameBatch *batch)
{
    auto *c = static_cast<Collector *>(ctx);
    BatchView v;
    v.table = batch->table ? batch->table : "";
    v.rows = batch->rows;
    for (uint32_t i = 0; i < batch->columns; ++i)
    {
        v.cols.push_back(batch->column_data[i].name ? batch->column_data[i].name : "");
        v.units.push_back(batch->column_data[i].unit ? batch->column_data[i].unit : "");
    }
    if (batch->rows)
    {
        v.t0 = batch->time_ns[0];
        v.tLast = batch->time_ns[batch->rows - 1];
        if (batch->columns && batch->column_data[0].values)
            v.v0 = static_cast<const double *>(batch->column_data[0].values)[0];
        for (uint32_t r = 0; r < batch->rows; ++r)
        {
            const int64_t t = batch->time_ns[r];
            if (!c->first && t < c->prev)
                c->monotonic = false;
            c->prev = t;
            c->first = false;
        }
    }
    c->totalRows += batch->rows;
    c->batches.push_back(v);
    return 0;
}

#ifndef PS_TEST_FIXTURE_DIR
#define PS_TEST_FIXTURE_DIR ""
#endif

QByteArray readFixture(const QString &name)
{
    QString dir = QString::fromUtf8(PS_TEST_FIXTURE_DIR);
    if (dir.isEmpty())
        dir = qEnvironmentVariable("PS_TEST_FIXTURE_DIR");
    QFile f(dir + QStringLiteral("/") + name);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}
} // namespace

int main()
{
    // ① 元数据
    const PsParserInfo &info = nextpilot::plotscope::ext::mdf::parserInfo();
    OK(info.id && std::strcmp(info.id, "mdf4") == 0, "info.id == mdf4");
    OK(info.streaming == 0, "streaming == 0（随机访问格式）");
    OK(info.can_sniff == 1, "can_sniff == 1");
    const PsParserVtable *vt = nextpilot::plotscope::ext::mdf::parserVtable();
    OK(vt && vt->abi_version == PS_PARSER_ABI_VERSION, "vtable abi 版本与宿主一致");
    OK(vt && ps_parser_has_configure(vt) == 0, "无 configure 槽（无解析参数）");

    // ② sniff
    const QByteArray mf4 = readFixture(QStringLiteral("demo_compressed.mf4"));
    OK(!mf4.isEmpty(), "fixture demo_compressed.mf4 可读");
    void *ctx = vt->create();
    OK(ctx != nullptr, "create 成功");
    OK(ctx && vt->sniff(ctx, reinterpret_cast<const uint8_t *>(mf4.constData()), 16) == 1,
       "sniff 认出 MDF 4.x 头");
    const char junk[16] = "NOTMDF  xx";
    OK(vt->sniff(ctx, reinterpret_cast<const uint8_t *>(junk), 10) == 0, "sniff 拒绝非 MDF");

    // ③ 解析 ##DZ fixture
    Collector col;
    PsParserSink sink{};
    sink.ctx = &col;
    sink.frame_batch = onBatch;
    PsError err{};
    const int32_t feedRc = vt->feed(ctx, reinterpret_cast<const uint8_t *>(mf4.constData()),
                                    static_cast<uint32_t>(mf4.size()), &sink, &err);
    OK(feedRc == 0, "feed 接受整文件字节（只累积，不出批）");
    OK(col.batches.empty(), "feed 阶段不产生批（finish 才解析）");
    const int32_t finRc = vt->finish(ctx, &sink, &err);
    OK(finRc == 0, "finish 解析成功");

    std::printf("[mdf] 批数=%zu 总行=%u\n", col.batches.size(), col.totalRows);
    for (size_t i = 0; i < col.batches.size(); ++i)
    {
        const BatchView &b = col.batches[i];
        std::printf("[mdf] 批%zu 表=%s 行=%u 列=%zu t0=%lld tLast=%lld"
                    " 列0=%s(%s) v0=%g\n",
                    i, b.table.c_str(), b.rows, b.cols.size(), (long long)b.t0,
                    (long long)b.tLast, b.cols.empty() ? "" : b.cols[0].c_str(),
                    b.units.empty() ? "" : b.units[0].c_str(), b.v0);
    }

    OK(!col.batches.empty(), "##DZ 压缩块解出至少一批");
    OK(col.totalRows > 0, "总行数 > 0");
    OK(col.monotonic, "时间轴纳秒单调非降");
    bool hasUnit = false, hasTable = false, tableDup = false;
    std::vector<std::string> tables;
    for (const BatchView &b : col.batches)
    {
        for (const std::string &u : b.units)
            if (!u.empty())
                hasUnit = true;
        if (!b.table.empty())
            hasTable = true;
        for (const std::string &t : tables)
            if (t == b.table)
                tableDup = true;
        tables.push_back(b.table);
    }
    OK(hasUnit, "至少一列带回单位（##CN links[6] 生效）");
    OK(hasTable, "表名非空");
    OK(!tableDup, "批之间表名不重复（同名组按 #n 去重）");
    // fixture 已知：demo_compressed.mf4 = 100 行 × roll/pitch，10 ms 采样。
    OK(col.totalRows == 100, "行数 = 100（##DZ 转置还原后行数正确）");
    OK(col.batches.size() == 1 && col.batches[0].cols.size() == 2,
       "一个通道组 = 一批 2 列（roll/pitch）");
    OK(col.batches[0].cols[0] == "roll" && col.batches[0].cols[1] == "pitch",
       "列名取 ##CN 通道名");
    OK(col.batches[0].t0 == 0 && col.batches[0].tLast == 990000000,
       "时间轴 = master 通道秒 → 纳秒（0 .. 990000000）");

    // ④ 重复 reset 后可用
    vt->reset(ctx);
    Collector col2;
    sink.ctx = &col2;
    const int32_t fin2 = vt->finish(ctx, &sink, &err);
    OK(fin2 < 0, "reset 后空字节 finish 报错（不是静默成功）");
    vt->destroy(ctx);

    // ⑤ 非法文件：错误要写清楚，不能静默成功
    void *ctx2 = vt->create();
    PsError err2{};
    const char bad[64] = {0};
    Collector col3;
    PsParserSink sink3{};
    sink3.ctx = &col3;
    sink3.frame_batch = onBatch;
    vt->feed(ctx2, reinterpret_cast<const uint8_t *>(bad), 64, &sink3, &err2);
    const int32_t finBad = vt->finish(ctx2, &sink3, &err2);
    OK(finBad < 0, "非 MDF 字节 finish 失败");
    OK(err2.message[0] != 0, "失败带错误信息");
    std::printf("[mdf] 非法文件错误=%s\n", err2.message);
    vt->destroy(ctx2);

    if (g_fail == 0)
        std::printf("MDF 解析器测试全部通过\n");
    return g_fail == 0 ? 0 : 1;
}
