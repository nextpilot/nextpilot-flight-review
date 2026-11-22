#include "Rosbag2Parser.h"
#include "ps_parser_spi.h"

#include <QFile>

#include <cmath>
#include <map>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

/*
 * T6.2 ROS2 bag（.db3）解析器验收（SPI 直连，headless）：
 *   - 元数据：id=rosbag2 / streaming=0 / can_sniff=1（SQLite 头）
 *   - fixture demo_rosbag2.db3：/imu 与 /speed 各 100 条、10 ms 间隔
 *   - 表名 = topic 去前导 '/'（imu / speed），列 = CDR 解出的字段
 *   - configure {"topics":"/speed"} 只出一张表
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
    int64_t t0 = 0;
    int64_t tLast = 0;
    std::vector<double> firstCol;
};

struct Collector
{
    std::vector<BatchView> batches;
    bool monotonic = true;
    // 单调性**按表**检查：不同 topic 各自从 0 起算，跨表比较会误报回退。
    std::map<std::string, int64_t> lastByTable;

    int32_t onBatch(const PsFrameBatch *batch)
    {
        BatchView v;
        v.table = batch->table ? batch->table : "";
        v.rows = batch->rows;
        for (uint32_t i = 0; i < batch->columns; ++i)
            v.cols.push_back(batch->column_data[i].name ? batch->column_data[i].name : "");
        if (batch->rows)
        {
            v.t0 = batch->time_us[0];
            v.tLast = batch->time_us[batch->rows - 1];
            if (batch->columns && batch->column_data[0].values)
                v.firstCol.assign(static_cast<const double *>(batch->column_data[0].values),
                                  static_cast<const double *>(batch->column_data[0].values) +
                                      batch->rows);
            auto &last = lastByTable[v.table];
            for (uint32_t r = 0; r < batch->rows; ++r)
            {
                const int64_t t = batch->time_us[r];
                if (lastByTable.count(v.table) && last > 0 && t < last)
                    monotonic = false;
                last = t;
            }
        }
        batches.push_back(v);
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
} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    using namespace nextpilot::plotscope::ext::rosbag2;

    const PsParserInfo &info = parserInfo();

    OK(info.id && std::strcmp(info.id, "rosbag2") == 0, "info.id == rosbag2");
    OK(info.streaming == 0, "streaming == 0（整文件随机访问）");
    OK(info.can_sniff == 1, "can_sniff == 1");
    const PsParserVtable *vt = parserVtable();
    OK(vt && vt->abi_version == PS_PARSER_ABI_VERSION, "vtable abi 一致");
    OK(vt && ps_parser_has_configure(vt) == 1, "有 configure 槽（topics 过滤）");

    const QByteArray db = readFixture(QStringLiteral("demo_rosbag2.db3"));
    OK(!db.isEmpty(), "fixture demo_rosbag2.db3 可读");

    void *ctx = vt->create();
    OK(vt->sniff(ctx, reinterpret_cast<const uint8_t *>(db.constData()), 16) == 1,
       "sniff 认出 SQLite 头");
    const char junk[16] = "NOTSQLITE";
    OK(vt->sniff(ctx, reinterpret_cast<const uint8_t *>(junk), 10) == 0, "sniff 拒绝非 SQLite");

    Collector col;
    PsParserSink sink{};
    sink.ctx = &col;
    sink.frame_batch = onBatchC;
    PsError err{};
    OK(vt->feed(ctx, reinterpret_cast<const uint8_t *>(db.constData()),
                static_cast<uint32_t>(db.size()), &sink, &err) == 0,
       "feed 接受整文件字节");
    OK(col.batches.empty(), "feed 阶段不出批");
    OK(vt->finish(ctx, &sink, &err) == 0, "finish 解析成功");

    std::printf("[rosbag2] 批数=%zu\n", col.batches.size());
    for (const BatchView &b : col.batches)
        std::printf("[rosbag2] 表=%s 行=%u 列=%zu t0=%lld tLast=%lld\n", b.table.c_str(), b.rows,
                    b.cols.size(), (long long)b.t0, (long long)b.tLast);

    OK(col.batches.size() == 2, "两个 topic = 两批");
    OK(col.monotonic, "时间轴微秒单调非降");

    const BatchView *imu = nullptr, *speed = nullptr;
    for (const BatchView &b : col.batches)
    {
        if (b.table == "imu")
            imu = &b;
        if (b.table == "speed")
            speed = &b;
    }
    OK(imu != nullptr && speed != nullptr, "表名 = topic 去前导 '/'（imu / speed）");
    if (imu && speed)
    {
        OK(imu->rows == 100 && speed->rows == 100, "每个 topic 100 行");
        OK(imu->t0 == 0 && imu->tLast == 990000, "imu 时间轴 0..990000 µs（10 ms）");
        bool hasOrient = false, hasAng = false, hasAcc = false;
        for (const std::string &c : imu->cols)
        {
            if (c == "orientation.w") hasOrient = true;
            if (c == "angular_velocity.x") hasAng = true;
            if (c == "linear_acceleration.z") hasAcc = true;
        }
        OK(hasOrient && hasAng && hasAcc, "Imu 解出 orientation/angular_velocity/linear_acceleration");
        OK(imu->cols.size() == 10, "Imu 恰好 10 个数值列");
        OK(!speed->cols.empty() && speed->cols[0] == "data", "Float64 列名 data");
        OK(!speed->firstCol.empty() && std::fabs(speed->firstCol.front() - 0.0) < 1e-9 &&
               std::fabs(speed->firstCol.back() - 49.5) < 1e-9,
           "Float64 首末值 0 / 49.5");
    }

    // configure 过滤：只要 /speed
    vt->reset(ctx);
    Collector filtered;
    PsParserSink sink2{};
    sink2.ctx = &filtered;
    sink2.frame_batch = onBatchC;
    PsError err2{};
    OK(vt->configure(ctx, R"({"topics":"/speed"})", &err2) == 0, "configure 接受 topics 过滤");
    vt->feed(ctx, reinterpret_cast<const uint8_t *>(db.constData()),
             static_cast<uint32_t>(db.size()), &sink2, &err2);
    OK(vt->finish(ctx, &sink2, &err2) == 0, "过滤后解析成功");
    OK(filtered.batches.size() == 1 && filtered.batches[0].table == "speed",
       "topics=/speed 只出 speed 一张表");
    vt->destroy(ctx);

    // 非法文件
    void *ctx2 = vt->create();
    PsError err3{};
    Collector bad;
    PsParserSink sink3{};
    sink3.ctx = &bad;
    sink3.frame_batch = onBatchC;
    const char notDb[64] = {0};
    vt->feed(ctx2, reinterpret_cast<const uint8_t *>(notDb), 64, &sink3, &err3);
    OK(vt->finish(ctx2, &sink3, &err3) < 0, "非 SQLite 字节 finish 失败");
    OK(err3.message[0] != 0, "失败带错误信息");
    std::printf("[rosbag2] 非法文件错误=%s\n", err3.message);
    vt->destroy(ctx2);

    if (g_fail == 0)
        std::printf("ROS2 bag 解析器测试全部通过\n");
    return g_fail == 0 ? 0 : 1;
}
