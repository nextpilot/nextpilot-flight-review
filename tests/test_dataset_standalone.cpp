// ps_dataset 独立链接验收：证明数据地基可脱离 ps_core 单独复用。
//
// 本 target 在 CMake 里**只链 ps_dataset + Qt6::Core**，不链 ps_core、
// 不链任何 module/extension/host。若哪天 Dataset 又偷偷依赖了 core 业务类型，
// 这里会直接链接失败——这就是抽库的硬承诺，不是口头声明。
#include "Dataset.h"          // 来自 ps_dataset（不是 ps_core）
#include "DatasetHub.h"
#include "RegistryTree.h"
#include "Result.h"           // 来自 ps_foundation

#include <QCoreApplication>
#include <QString>
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

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // 1. Dataset 门面：建表 → 追加列 → schema 投影 → 窗口读取，全走 Rust kernel。
    Dataset ds = Dataset::create(QStringLiteral("standalone"), QStringLiteral("csv"));
    CHECK(!ds.isNull());
    CHECK(ds.name() == QStringLiteral("standalone"));

    const QVector<double> t{0.0, 1.0, 2.0, 3.0};
    const QVector<double> v{10.0, 20.0, 30.0, 40.0};
    const int tableIdx = ds.addTable(QStringLiteral("ch"), t);
    CHECK(tableIdx >= 0);
    CHECK(ds.addF64Column(QStringLiteral("ch"), QStringLiteral("y"),
                          QStringLiteral("V"), v));

    const Table *table = ds.table(QStringLiteral("ch"));
    CHECK(table != nullptr);
    CHECK(table->rowCount() == 4);
    CHECK(table->columnIndex(QStringLiteral("y")) >= 0);

    // readWindow 用微秒时间轴；[0s, 3s] 内应取回 4 点。
    auto view = ds.readWindow(QStringLiteral("ch"), QStringLiteral("y"),
                              0, 3'000'000, 4096);
    CHECK(view.has_value());
    CHECK(view->size() == 4);
    CHECK(view->value.last() == 40.0);

    // 2. DatasetHub 仓库：存/取/删，Result 来自 foundation，不依赖 core。
    RegistryTree tree;
    DatasetHub repo(tree); // M2.8：构造即上树
    Result<DatasetId> added = repo.addWithId(ds, QStringLiteral("standalone"));
    CHECK(added.ok());
    const DatasetId id = added.value();
    CHECK(repo.count() == 1);
    CHECK(repo.get(id) != nullptr);
    CHECK(repo.get(id)->name() == QStringLiteral("standalone"));

    CHECK(repo.remove(id));
    CHECK(repo.count() == 0);
    CHECK(repo.get(id) == nullptr);

    // 3. 表格查看器路径：按行号随机读取，区间取值与全量一致。
    {
        constexpr int N = 1000;
        QVector<double> tt(N);
        QVector<double> a(N);
        QVector<double> b(N);
        QVector<bool> nulls(N, false);
        for (int i = 0; i < N; ++i)
        {
            tt[i] = i * 0.001;           // 1 kHz 采样
            a[i] = i * 2.0 + 1.0;
            b[i] = i % 7 == 3 ? 0.0 : i * 0.5; // 每 7 行埋一个 null
            nulls[i] = (i % 7 == 3);
        }
        Dataset big = Dataset::create(QStringLiteral("big"), QStringLiteral("csv"));
        CHECK(big.addTable(QStringLiteral("t1"), tt) >= 0);
        CHECK(big.addF64Column(QStringLiteral("t1"), QStringLiteral("a"), QStringLiteral("m"), a));
        CHECK(big.addF64Column(QStringLiteral("t1"), QStringLiteral("b"), QStringLiteral("m"), b, &nulls));

        // 随机区间（页式读取，起点/长度都打乱）与全量逐一比对。
        QVector<double> timeSec;
        QVector<double> va;
        QVector<double> vb;
        const int starts[] = {0, 1, 37, 256, 500, 987, 999, 1000, 1234}
        ;
        for (const int start : starts)
        {
            const int gotT = big.readTimeRows(QStringLiteral("t1"), start, 64, timeSec);
            QVector<double> vaPage;
            const int gotA = big.readColumnRows(QStringLiteral("t1"), QStringLiteral("a"),
                                                start, 64, vaPage);
            va = vaPage;
            QVector<double> vbPage;
            const int gotB = big.readColumnRows(QStringLiteral("t1"), QStringLiteral("b"),
                                                start, 64, vbPage);
            vb = vbPage;
            const int expect = qMax(0, qMin(64, N - start));
            CHECK(gotT == expect);
            CHECK(gotA == expect);
            CHECK(gotB == expect);
            for (int i = 0; i < expect; ++i)
            {
                const int row = start + i;
                // 时间经 µs 量化往返（t*1e6 → int64 → ×1e-6），只要求 µs 级一致。
                CHECK(std::fabs(timeSec.at(i) - tt.at(row)) < 1e-9);
                CHECK(va.at(i) == a.at(row));
                const bool isNull = (row % 7 == 3);
                CHECK(std::isnan(vb.at(i)) == isNull);
                if (!isNull)
                    CHECK(vb.at(i) == b.at(row));
            }
        }
        // 不存在的表/列返回 -1；参数非法回 0/-1 不崩。
        CHECK(big.readColumnRows(QStringLiteral("nope"), QStringLiteral("a"), 0, 10, va) == -1);
        CHECK(big.readColumnRows(QStringLiteral("t1"), QStringLiteral("nope"), 0, 10, va) == -1);
        CHECK(big.readColumnRows(QStringLiteral("t1"), QStringLiteral("a"), 999, 10, va) == 1);
        CHECK(big.readColumnRows(QStringLiteral("t1"), QStringLiteral("a"), 1001, 10, va) == 0);
        CHECK(big.readColumnRows(QStringLiteral("t1"), QStringLiteral("a"), 5, 0, va) == -1);
        CHECK(big.readTimeRows(QStringLiteral("nope"), 0, 10, timeSec) == -1);

        fprintf(stdout, "[ok] 行区间随机读取与全量一致（null → NaN 占位）\n");
    }

    fprintf(stdout, "[ok] ps_dataset 独立链接验收通过（未链接 ps_core）\n");
    return 0;
}
