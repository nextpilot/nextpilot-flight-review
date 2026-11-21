// DataTableModel 验收（M2.2 2.2.2）：懒加载分页 + 100 万行 <100ms 硬线。
//
// 只链 ng_data_tablemodel + ng_dataset + Qt6::Core：
// 模型若偷偷依赖 Widgets/模块层，这里直接编译失败——分层硬承诺。
#include "DataTableModel.h"
#include "Dataset.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QVector>

#include <cmath>
#include <cstdio>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            return 1;                                                          \
        }                                                                      \
    } while (0)

using namespace nextpilot::plotscope::dataset;
using nextpilot::plotscope::data::DataTableModel;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // 1. 小表：基本行列数/表头/取值正确。
    {
        const QVector<double> t{0.0, 1.0, 2.0, 3.0};
        const QVector<double> v{10.0, 20.0, 30.0, 40.0};
        Dataset ds = Dataset::create(QStringLiteral("small"), QStringLiteral("csv"));
        CHECK(ds.addTable(QStringLiteral("t"), t) >= 0);
        CHECK(ds.addF64Column(QStringLiteral("t"), QStringLiteral("y"),
                              QStringLiteral("V"), v));

        DataTableModel m;
        m.setDataset(&ds, QStringLiteral("t"));
        CHECK(m.rowCount() == 4);
        CHECK(m.columnCount() == 2); // 时间 + y
        CHECK(m.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString() == QStringLiteral("时间(s)"));
        CHECK(m.headerData(1, Qt::Horizontal, Qt::DisplayRole).toString() == QStringLiteral("y [V]"));
        CHECK(m.headerData(0, Qt::Vertical, Qt::DisplayRole).toInt() == 1);
        CHECK(m.index(0, 0).data().toString().startsWith(QStringLiteral("0.000000")));
        CHECK(m.index(2, 1).data().toDouble() == 30.0);
        CHECK(m.pageFetchCount() == 1); // 首次取值触发一次页读取

        // 解绑 → 空模型不崩。
        m.setDataset(nullptr, QString());
        CHECK(m.rowCount() == 0);
        CHECK(m.columnCount() == 0);

        fprintf(stdout, "[ok] 小表：行列表头/取值/页计数正确，解绑安全\n");
    }

    // 2. null 列 → NaN → 显示为空串。
    {
        const QVector<double> t{0.0, 1.0};
        QVector<double> v{1.0, 2.0};
        QVector<bool> nulls{false, true};
        Dataset ds = Dataset::create(QStringLiteral("n"), QStringLiteral("csv"));
        ds.addTable(QStringLiteral("t"), t);
        ds.addF64Column(QStringLiteral("t"), QStringLiteral("y"), QStringLiteral("u"), v, &nulls);
        DataTableModel m;
        m.setDataset(&ds, QStringLiteral("t"));
        CHECK(m.index(1, 1).data().toString().isEmpty());
        fprintf(stdout, "[ok] null 单元显示为空串\n");
    }

    // 3. 100 万行硬验收线：跳转到尾部页 + 顺序翻页，单次页读取 <100ms。
    //    并验证懒加载：只取触过的页，不做全量预读。
    {
        constexpr int N = 1'000'000;
        QVector<double> t(N);
        QVector<double> v(N);
        for (int i = 0; i < N; ++i)
        {
            t[i] = i * 0.001;
            v[i] = i * 0.5 - 100.0;
        }
        QElapsedTimer build;
        build.start();
        Dataset ds = Dataset::create(QStringLiteral("big"), QStringLiteral("csv"));
        CHECK(ds.addTable(QStringLiteral("big"), t) >= 0);
        CHECK(ds.addF64Column(QStringLiteral("big"), QStringLiteral("y"),
                              QStringLiteral("m"), v));
        const qint64 buildMs = build.elapsed();
        fprintf(stdout, "[perf] 构建百万行数据集 %lld ms\n", buildMs);

        DataTableModel m;
        m.setDataset(&ds, QStringLiteral("big"));
        CHECK(m.rowCount() == N);
        CHECK(m.pageFetchCount() == 0); // 未取值前零页读取——懒加载的硬证据

        // 跳转到最后一行（典型"跳页"路径）。
        QElapsedTimer timer;
        timer.start();
        const QVariant tail = m.index(N - 1, 1).data();
        const qint64 jumpMs = timer.elapsed();
        CHECK(tail.toDouble() == (N - 1) * 0.5 - 100.0);
        CHECK(jumpMs < 100); // 2.2.2 验收：跳转 <100ms
        CHECK(m.pageFetchCount() == 1);

        // 跳回首页 + 中间页（LRU 未命中重新读），各自 <100ms。
        timer.restart();
        CHECK(m.index(0, 1).data().toDouble() == -100.0);
        const qint64 homeMs = timer.elapsed();
        CHECK(homeMs < 100);
        timer.restart();
        const int mid = 500'000;
        CHECK(m.index(mid, 1).data().toDouble() == mid * 0.5 - 100.0);
        const qint64 midMs = timer.elapsed();
        CHECK(midMs < 100);
        CHECK(m.pageFetchCount() == 3);

        // 缓存命中路径：重复读同一页不再产生页读取。
        const int fetches = m.pageFetchCount();
        (void)m.index(mid + 1, 1).data();
        CHECK(m.pageFetchCount() == fetches);
        CHECK(m.lastPageMs() < 100);

        fprintf(stdout, "[perf] 百万行：跳尾 %lld ms / 回首 %lld ms / 跳中 %lld ms（<100ms 验收线）\n",
                jumpMs, homeMs, midMs);
        fprintf(stdout, "[ok] 100 万行懒加载分页验收通过\n");
    }

    // 4. 列统计摘要一致性（2.2.4 验收）：kernel 统计 vs 测试内独立公式。
    {
        constexpr int N = 997; // 质数，避免与页大小对齐产生的巧合
        QVector<double> t(N);
        QVector<double> v(N);
        double sum = 0.0;
        double vmin = 1e300;
        double vmax = -1e300;
        for (int i = 0; i < N; ++i)
        {
            t[i] = i * 0.01;
            // 一组有奇偶结构的值：-3, 3, -5, 5 ...（均值非零、方差好手算）
            v[i] = (i % 2 == 0 ? -1.0 : 1.0) * (3.0 + (i % 10));
            sum += v[i];
            vmin = qMin(vmin, v[i]);
            vmax = qMax(vmax, v[i]);
        }
        const double mean = sum / N;
        double ss = 0.0;
        for (int i = 0; i < N; ++i)
            ss += (v[i] - mean) * (v[i] - mean);
        const double stdDev = std::sqrt(ss / N); // 总体标准差（kernel 语义，见 stats.rs）

        Dataset ds = Dataset::create(QStringLiteral("st"), QStringLiteral("csv"));
        ds.addTable(QStringLiteral("t"), t);
        ds.addF64Column(QStringLiteral("t"), QStringLiteral("y"), QStringLiteral("m"), v);
        QVector<double> read;
        CHECK(ds.readColumnRows(QStringLiteral("t"), QStringLiteral("y"), 0, N, read) == N);
        const auto st = ds.stats(read, false);
        CHECK(st.has_value());
        CHECK(st->count == static_cast<quint32>(N));
        CHECK(qAbs(st->mean - mean) < 1e-9);
        CHECK(qAbs(st->stdDev - stdDev) < 1e-9);
        CHECK(qAbs(st->min - vmin) < 1e-12);
        CHECK(qAbs(st->max - vmax) < 1e-12);
        fprintf(stdout, "[ok] 列统计与独立公式一致（N=%d mean=%.6f σ=%.6f）\n", N, mean, stdDev);
    }

    fprintf(stdout, "[ok] ng_data_table 全部验收通过\n");
    return 0;
}
