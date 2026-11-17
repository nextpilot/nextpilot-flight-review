#include "DatasetCache.h"

#include "ps_kernel.h"

#include <QFile>
#include <QTemporaryDir>

#include <cassert>
#include <cstring>
#include <iostream>

int main()
{
    QTemporaryDir tmp;
    assert(tmp.isValid());
    qputenv("PLOTSCOPE_CACHE_DIR", tmp.path().toUtf8());

    const QString source = tmp.filePath(QStringLiteral("demo.bin"));
    QFile f(source);
    assert(f.open(QIODevice::WriteOnly));
    f.write("source-v1");
    f.close();

    PsDataset *ds = ps_dataset_new();
    assert(ds);
    int64_t t[3] = {0, 1000000, 2000000};
    double v[3] = {10.0, 20.0, 30.0};
    char err[128] = {0};
    assert(ps_dataset_add_table_typed(ds, "imu", t, 3, err, sizeof(err)) == 0);
    assert(ps_dataset_add_table_column_typed(ds, "imu", "roll", "deg", PS_VALUE_F64, v, nullptr, 3,
                                             err, sizeof(err)) == 0);
    assert(ps_dataset_channel_count(ds) == 1);

    assert(DatasetCache::save(source, "data_loader_csv", "", ds));
    ps_dataset_free(ds);

    PsDataset *hit = DatasetCache::load(source, "data_loader_csv", "");
    assert(hit);
    assert(ps_dataset_channel_count(hit) == 1);
    const PsChannelInfo *ch = ps_dataset_channel(hit, 0);
    assert(ch && std::strcmp(ch->name, "imu.roll") == 0 && ch->point_count == 3);
    assert(ps_channel_value_at(ch, 1) == 20.0);
    assert(ps_dataset_seq_at(hit, "imu", 1) == 1);
    ps_dataset_free(hit);

    const QString params = QStringLiteral("[{\"name\":\"TEST\",\"value\":\"42\"}]");
    assert(DatasetCache::saveParams(source, "data_loader_csv", "", params));
    assert(DatasetCache::loadParams(source, "data_loader_csv", "") == params);
    assert(DatasetCache::loadParams(source, "data_loader_csv", "{\"x\":1}").isEmpty());

    // 源文件变化 => 数据集和参数缓存同时失效
    assert(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("source-v2-longer");
    f.close();
    assert(DatasetCache::load(source, "data_loader_csv", "") == nullptr);
    assert(DatasetCache::loadParams(source, "data_loader_csv", "").isEmpty());

    DatasetCache::clear();
    std::cout << "dataset cache ok\n";
    return 0;
}
