#include "BlfParser.h"
#include "ps_parser_spi.h"

#include <QFile>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

/*
 * T6.3 Vector BLF（.blf）解析器验收（SPI 直连，headless）：
 *   - 元数据：id=blf / streaming=0 / can_sniff=1（LOGG 头）
 *   - fixture demo_can.blf：5 个 CAN ID、共 3020 帧（含扩展帧 0x18FEF100）
 *   - 出口口径：每个有变化字节的 CAN ID 一表，表名 CAN<bus>_<IDHEX>
 *   - 全常量 ID（0x4D0，20 帧）按 main 判据跳过，不出表
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
    std::vector<std::vector<double>> values;
    int64_t t0 = 0;
    int64_t tLast = 0;
};

struct Collector
{
    std::vector<BatchView> batches;
    bool monotonic = true;
    std::map<std::string, int64_t> lastByTable;

    int32_t onBatch(const PsFrameBatch *batch)
    {
        BatchView v;
        v.table = batch->table ? batch->table : "";
        v.rows = batch->rows;
        for (uint32_t i = 0; i < batch->columns; ++i)
        {
            v.cols.push_back(batch->column_data[i].name ? batch->column_data[i].name : "");
            std::vector<double> col;
            const double *src = static_cast<const double *>(batch->column_data[i].values);
            if (src)
                col.assign(src, src + batch->rows);
            v.values.push_back(std::move(col));
        }
        if (batch->rows)
        {
            v.t0 = batch->time_ns[0];
            v.tLast = batch->time_ns[batch->rows - 1];
            auto &last = lastByTable[v.table];
            for (uint32_t r = 0; r < batch->rows; ++r)
            {
                const int64_t t = batch->time_ns[r];
                if (t < last)
                    monotonic = false;
                last = t;
            }
        }
        batches.push_back(std::move(v));
        return 0;
    }
};

int32_t onBatchC(void *ctx, const PsFrameBatch *batch)
{
    return static_cast<Collector *>(ctx)->onBatch(batch);
}

QByteArray readFixture(const QString &name)
{
#ifndef PS_TEST_FIXTURE_DIR
#define PS_TEST_FIXTURE_DIR ""
#endif
    const QString dir = QString::fromUtf8(PS_TEST_FIXTURE_DIR);
    QFile f(dir + QStringLiteral("/") + name);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

const BatchView *find(const Collector &c, const std::string &table)
{
    for (const BatchView &b : c.batches)
        if (b.table == table)
            return &b;
    return nullptr;
}
} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    using namespace nextpilot::plotscope::ext::blf;

    const PsParserInfo &info = parserInfo();
    OK(info.id && std::strcmp(info.id, "blf") == 0, "info.id == blf");
    OK(info.streaming == 0, "streaming == 0（整文件随机访问）");
    OK(info.can_sniff == 1, "can_sniff == 1");
    const PsParserVtable *vt = parserVtable();
    OK(vt && vt->abi_version == PS_PARSER_ABI_VERSION, "vtable abi 一致");
    OK(vt && ps_parser_has_configure(vt) == 0, "无 configure 槽（BLF 无解析参数）");

    const QByteArray blf = readFixture(QStringLiteral("demo_can.blf"));
    OK(!blf.isEmpty(), "fixture demo_can.blf 可读");

    void *ctx = vt->create();
    OK(vt->sniff(ctx, reinterpret_cast<const uint8_t *>(blf.constData()), 4) == 1,
       "sniff 认出 LOGG 头");
    const char junk[8] = "NOTBLF";
    OK(vt->sniff(ctx, reinterpret_cast<const uint8_t *>(junk), 6) == 0, "sniff 拒绝非 BLF");

    Collector col;
    PsParserSink sink{};
    sink.ctx = &col;
    sink.frame_batch = onBatchC;
    PsError err{};
    OK(vt->feed(ctx, reinterpret_cast<const uint8_t *>(blf.constData()),
                static_cast<uint32_t>(blf.size()), &sink, &err) == 0,
       "feed 接受整文件字节");
    OK(col.batches.empty(), "feed 阶段不出批");
    OK(vt->finish(ctx, &sink, &err) == 0, "finish 解析成功");

    std::printf("[blf] 批数=%zu\n", col.batches.size());
    uint32_t totalRows = 0;
    for (const BatchView &b : col.batches)
    {
        totalRows += b.rows;
        std::printf("[blf] 表=%s 行=%u 列=%zu t0=%lld tLast=%lld\n", b.table.c_str(), b.rows,
                    b.cols.size(), (long long)b.t0, (long long)b.tLast);
    }

    // 5 个 ID 里 0x4D0 全常量（20 帧同数据）→ 按 main 判据跳过，剩 4 张表
    OK(col.batches.size() == 4, "4 张表（常量 ID 跳过）");
    OK(col.monotonic, "时间轴纳秒单调非降");

    std::set<std::string> tables;
    for (const BatchView &b : col.batches)
        tables.insert(b.table);
    OK(tables.count("CAN2_1A0") == 1 && tables.count("CAN2_2B0") == 1 &&
           tables.count("CAN2_3C0") == 1 && tables.count("CAN3_18FEF100") == 1,
       "表名 = CAN<bus>_<IDHEX>（含扩展帧 18FEF100）");
    OK(tables.count("CAN2_4D0") == 0, "全常量 ID 不出表");

    OK(totalRows == 3000, "落表行数 3000（3020 帧 - 20 常量帧）");

    const BatchView *a0 = find(col, "CAN2_1A0");
    OK(a0 != nullptr, "CAN2_1A0 表存在");
    if (a0)
    {
        OK(a0->rows == 750, "CAN2_1A0 750 行");
        OK(a0->t0 == 0, "首帧对齐到 0");
        OK(a0->tLast == 11984000000, "末帧 11984000000 ns（≈12 s）");
        OK(a0->cols.size() == 4, "列 = b0,b1,dlc,period");
        OK(a0->cols[0] == "b0" && a0->cols[1] == "b1" && a0->cols[2] == "dlc" &&
               a0->cols[3] == "period",
           "列名顺序 b0 / b1 / dlc / period");
        // 只保留变化字节：0x1A0 的数据只有前两字节在变
        // 数据字节列不应有空洞；period 首行按 main 口径是 NaN（无前一帧）
        bool okRange = !a0->values.empty();
        for (size_t i = 0; okRange && i < a0->values.size(); ++i)
            for (size_t r = 0; r < a0->values[i].size(); ++r)
            {
                if (i == a0->values.size() - 1 && r == 0)
                    continue;
                if (std::isnan(a0->values[i][r]))
                    okRange = false;
            }
        OK(okRange, "数据字节与 dlc 列无 NaN 空洞");
        if (!a0->values.empty() && !a0->values[0].empty())
            OK(std::fabs(a0->values[0].front() - 0x80) < 1e-9 &&
                   std::fabs(a0->values[0].back() - 0xA4) < 1e-9,
               "b0 首末值 0x80 / 0xA4");
        if (a0->values.size() > 3 && !a0->values[3].empty())
            OK(std::fabs(a0->values[3][1] - 16.0) < 1e-6, "period ≈ 16 ms（10 ms 级 CAN 周期）");
    }

    const BatchView *ext = find(col, "CAN3_18FEF100");
    OK(ext != nullptr && ext->rows == 750, "扩展帧表 CAN3_18FEF100 750 行");
    if (ext)
        OK(ext->t0 == 4000000, "扩展帧相对首帧的偏移 4000000 ns（保留原始相对时间）");

    vt->destroy(ctx);

    // 非法文件
    void *ctx2 = vt->create();
    PsError err2{};
    Collector bad;
    PsParserSink sink2{};
    sink2.ctx = &bad;
    sink2.frame_batch = onBatchC;
    const char notBlf[160] = {0};
    vt->feed(ctx2, reinterpret_cast<const uint8_t *>(notBlf), 160, &sink2, &err2);
    OK(vt->finish(ctx2, &sink2, &err2) < 0, "非 BLF 字节 finish 失败");
    OK(err2.message[0] != 0, "失败带错误信息");
    std::printf("[blf] 非法文件错误=%s\n", err2.message);
    vt->destroy(ctx2);

    if (g_fail == 0)
        std::printf("BLF 解析器测试全部通过\n");
    return g_fail == 0 ? 0 : 1;
}
