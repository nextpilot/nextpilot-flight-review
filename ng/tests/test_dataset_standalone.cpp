// ng_dataset 独立链接验收：证明数据地基可脱离 ng_core 单独复用。
//
// 本 target 在 CMake 里**只链 ng_dataset + Qt6::Core**，不链 ng_core、
// 不链任何 module/extension/host。若哪天 Dataset 又偷偷依赖了 core 业务类型，
// 这里会直接链接失败——这就是抽库的硬承诺，不是口头声明。
#include "Dataset.h"          // 来自 ng_dataset（不是 ng_core）
#include "DataRepository.h"
#include "Result.h"           // 来自 ng_foundation

#include <QCoreApplication>
#include <QString>
#include <QVector>

#include <cstdio>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            return 1;                                                          \
        }                                                                      \
    } while (0)

using namespace nextpilot::plotscope::core;

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

    // 2. DataRepository 仓库：存/取/删，Result 来自 foundation，不依赖 core。
    DataRepository repo;
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
