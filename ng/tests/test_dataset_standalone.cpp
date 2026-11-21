// ng_dataset 独立链接验收：证明数据地基可脱离 ng_core 单独复用。
//
// 本 target 在 CMake 里**只链 ng_dataset + Qt6::Core**，不链 ng_core、
// 不链任何 module/extension/host。若哪天 Dataset 又偷偷依赖了 core 业务类型，
// 这里会直接链接失败——这就是抽库的硬承诺，不是口头声明。
#include "Dataset.h"          // 来自 ng_dataset（不是 ng_core）
#include "DatasetRegistry.h"
#include "Result.h"           // 来自 ng_foundation

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

    // 1b. 源级时间对齐（T3.1）：存原始、出口叠加、reset 逐点还原。
    CHECK(ds.timeOffsetUs() == 0);
    CHECK(std::fabs(ds.timeMin() - 0.0) < 1e-9);
    CHECK(std::fabs(ds.timeMax() - 3.0) < 1e-9);
    // LOD 基线（offset=0）：kernel 返回的是桶中心 x，不是桶起点，
    // 所以只能断言"整条 LOD 相对基线整体平移 offset"，不能断言首点等于 2.0。
    const auto lodRaw = ds.lodMinMax(QStringLiteral("ch"), QStringLiteral("y"), 0, 3'000'000, 8);
    CHECK(!lodRaw.isEmpty());
    ds.setTimeOffsetUs(2'000'000); // +2s
    CHECK(ds.timeOffsetUs() == 2'000'000);
    CHECK(std::fabs(ds.rawTimeMin() - 0.0) < 1e-9); // 原始范围不变（存储没被改写）
    CHECK(std::fabs(ds.timeMin() - 2.0) < 1e-9);
    CHECK(std::fabs(ds.timeMax() - 5.0) < 1e-9);
    {
        // 调用方用"对齐后"的窗口读，出口时间也应是"对齐后"的。
        auto shifted = ds.readWindow(QStringLiteral("ch"), QStringLiteral("y"),
                                     2'000'000, 5'000'000, 4096);
        CHECK(shifted.has_value());
        CHECK(shifted->size() == 4);
        CHECK(std::fabs(shifted->time.first() - 2.0) < 1e-9);
        CHECK(std::fabs(shifted->time.last() - 5.0) < 1e-9);
        CHECK(shifted->value.first() == 10.0); // 值不随对齐变化
    }
    {
        // LOD 路径同一口径：视口入参平移、输出 x 平移。
        const auto lod = ds.lodMinMax(QStringLiteral("ch"), QStringLiteral("y"),
                                      2'000'000, 5'000'000, 8);
        CHECK(lod.size() == lodRaw.size());
        for (int i = 0; i < lod.size(); ++i)
        {
            CHECK(std::fabs((lod.at(i).x - lodRaw.at(i).x) - 2.0) < 1e-6);
            CHECK(lod.at(i).min == lodRaw.at(i).min); // 值域不随对齐变化
        }
    }
    ds.setTimeOffsetUs(0); // reset：只清偏移，原始数据从未被写过，必然逐点还原
    {
        auto restored = ds.readWindow(QStringLiteral("ch"), QStringLiteral("y"),
                                      0, 3'000'000, 4096);
        CHECK(restored.has_value());
        CHECK(restored->size() == 4);
        CHECK(std::fabs(restored->time.first() - 0.0) < 1e-9);
        CHECK(std::fabs(restored->time.last() - 3.0) < 1e-9);
    }

    // 2. DatasetRegistry 仓库：存/取/删，Result 来自 foundation，不依赖 core。
    DatasetRegistry repo;
    Result<DatasetId> added = repo.addWithId(ds, QStringLiteral("standalone"));
    CHECK(added.ok());
    const DatasetId id = added.value();
    CHECK(repo.count() == 1);
    CHECK(repo.get(id) != nullptr);
    CHECK(repo.get(id)->name() == QStringLiteral("standalone"));

    CHECK(repo.remove(id));
    CHECK(repo.count() == 0);
    CHECK(repo.get(id) == nullptr);

    fprintf(stdout, "[ok] ng_dataset 独立链接验收通过（未链接 ng_core）\n");
    return 0;
}
