#include "ApbinParser.h"
#include "ps_parser_spi.h"

#include <QFile>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

/*
 * T6.3 ArduPilot DataFlash（apbin）解析器验收（SPI 直连，headless）：
 *   - 元数据：id=apbin / streaming=0 / can_sniff=1（A3 95 头）
 *   - fixture demo_apbin.bin：FMT 定义 ATT(1) / FMTU(2) / PARM(3) / UNIT(4)
 *   - 出口口径：按 FMT 出表，TimeUS 当时间轴（微秒，相对首帧），字符串字段跳过
 *   - 无 TimeUS 的消息（FMTU / UNIT）不成表
 */
static int g_fail = 0;
#define OK(cond, msg)                                                              \
    do                                                                             \
    {                                                                              \
        if (cond)                                                                  \
            std::printf("[ok] %s\n", msg);                                          \
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
    std::vector<std::string> units;
    int64_t t0 = 0;
    int64_t tLast = 0;
};

struct Collector
{
    std::vector<BatchView> batches;
    bool monotonic = true;

    int32_t onBatch(const PsFrameBatch *batch)
    {
        BatchView v;
        v.table = batch->table ? batch->table : "";
        v.rows = batch->rows;
        for (uint32_t i = 0; i < batch->columns; ++i)
        {
            v.cols.push_back(batch->column_data[i].name ? batch->column_data[i].name : "");
            v.units.push_back(batch->column_data[i].unit ? batch->column_data[i].unit : "");
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
            int64_t last = v.t0;
            for (uint32_t r = 0; r < batch->rows; ++r)
            {
                if (batch->time_ns[r] < last)
                    monotonic = false;
                last = batch->time_ns[r];
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
    using namespace nextpilot::plotscope::ext::apbin;

    const PsParserInfo &info = parserInfo();
    OK(info.id && std::strcmp(info.id, "apbin") == 0, "info.id == apbin");
    OK(info.streaming == 0, "streaming == 0（整文件随机访问）");
    OK(info.can_sniff == 1, "can_sniff == 1");
    const PsParserVtable *vt = parserVtable();
    OK(vt && vt->abi_version == PS_PARSER_ABI_VERSION, "vtable abi 一致");

    const QByteArray bin = readFixture(QStringLiteral("demo_apbin.bin"));
    OK(!bin.isEmpty(), "fixture demo_apbin.bin 可读");

    void *ctx = vt->create();
    OK(vt->sniff(ctx, reinterpret_cast<const uint8_t *>(bin.constData()), 3) == 1,
       "sniff 认出 A3 95 头");
    const char junk[8] = "NOTBIN\0";
    OK(vt->sniff(ctx, reinterpret_cast<const uint8_t *>(junk), 3) == 0, "sniff 拒绝非 DataFlash");

    Collector col;
    PsParserSink sink{};
    sink.ctx = &col;
    sink.frame_batch = onBatchC;
    PsError err{};
    OK(vt->feed(ctx, reinterpret_cast<const uint8_t *>(bin.constData()),
                static_cast<uint32_t>(bin.size()), &sink, &err) == 0,
       "feed 接受整文件字节");
    OK(col.batches.empty(), "feed 阶段不出批");
    OK(vt->finish(ctx, &sink, &err) == 0, "finish 解析成功");

    std::printf("[apbin] 批数=%zu\n", col.batches.size());
    for (const BatchView &b : col.batches)
        std::printf("[apbin] 表=%s 行=%u 列=%zu t0=%lld tLast=%lld\n", b.table.c_str(), b.rows,
                    b.cols.size(), (long long)b.t0, (long long)b.tLast);

    OK(col.monotonic, "时间轴纳秒单调非降");
    OK(find(col, "FMTU") == nullptr && find(col, "UNIT") == nullptr,
       "无 TimeUS 的消息（FMTU / UNIT）不成表");

    const BatchView *att = find(col, "ATT");
    OK(att != nullptr, "FMT 名成表名（ATT，尾空格已 trim）");
    if (att)
    {
        OK(att->rows == 2, "ATT 2 行");
        OK(att->cols.size() == 2 && att->cols[0] == "Roll" && att->cols[1] == "Pitch",
           "列 = Roll / Pitch（TimeUS 作时间轴不占列）");
        // 全文件首帧是 PARM(TimeUS=0)，ATT 从 1 s 开始 → 保留原始相对偏移
        OK(att->t0 == 1000000000 && att->tLast == 2000000000,
           "时间轴 1000000000 / 2000000000 ns（相对全文件首帧，保留偏移）");
        if (att->values.size() == 2)
        {
            OK(std::fabs(att->values[0][0] - 1.5) < 1e-6 &&
                   std::fabs(att->values[0][1] - 2.5) < 1e-6,
               "Roll = 1.5 / 2.5（f32 解码）");
            OK(std::fabs(att->values[1][0] + 2.0) < 1e-6 &&
                   std::fabs(att->values[1][1] + 4.0) < 1e-6,
               "Pitch = -2.0 / -4.0");
        }
    }

    const BatchView *parm = find(col, "PARM");
    OK(parm != nullptr, "PARM 成表（含 TimeUS，故按表落库）");
    if (parm)
    {
        OK(parm->rows == 1 && parm->cols.size() == 1 && parm->cols[0] == "Value",
           "PARM 1 行 1 列（Name 是字符串字段，跳过）");
        if (parm->values.size() == 1 && !parm->values[0].empty())
            OK(std::fabs(parm->values[0][0] - 42.5) < 1e-6, "PARM Value = 42.5");
    }
    vt->destroy(ctx);

    // 非法文件
    void *ctx2 = vt->create();
    PsError err2{};
    Collector bad;
    PsParserSink sink2{};
    sink2.ctx = &bad;
    sink2.frame_batch = onBatchC;
    const char notBin[64] = {0};
    vt->feed(ctx2, reinterpret_cast<const uint8_t *>(notBin), 64, &sink2, &err2);
    OK(vt->finish(ctx2, &sink2, &err2) < 0, "非 DataFlash 字节 finish 失败");
    OK(err2.message[0] != 0, "失败带错误信息");
    std::printf("[apbin] 非法文件错误=%s\n", err2.message);
    vt->destroy(ctx2);

    // 只有 FMT 定义、没有数据帧：应失败（不假装成功出空表）
    void *ctx3 = vt->create();
    PsError err3{};
    Collector only;
    PsParserSink sink3{};
    sink3.ctx = &only;
    sink3.frame_batch = onBatchC;
    const std::vector<uint8_t> fmtOnly(bin.constData(), bin.constData() + 91); // 头部不完整
    vt->feed(ctx3, fmtOnly.data(), static_cast<uint32_t>(fmtOnly.size()), &sink3, &err3);
    OK(vt->finish(ctx3, &sink3, &err3) < 0 || only.batches.empty(),
       "只有 FMT 定义时不出表（不造空表）");
    vt->destroy(ctx3);

    if (g_fail == 0)
        std::printf("DataFlash（apbin）解析器测试全部通过\n");
    return g_fail == 0 ? 0 : 1;
}
