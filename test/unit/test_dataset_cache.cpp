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
    double t[3] = {0.0, 1.0, 2.0};
    double v[3] = {10.0, 20.0, 30.0};
    char err[128] = {0};
    assert(ps_dataset_add_channel(ds, "roll", "deg", t, v, 3, err, sizeof(err)) == 0);
    assert(ps_dataset_channel_count(ds) == 1);

    assert(DatasetCache::save(source, "data_loader_csv", "", ds));
    ps_dataset_free(ds);

    PsDataset *hit = DatasetCache::load(source, "data_loader_csv", "");
    assert(hit);
    assert(ps_dataset_channel_count(hit) == 1);
    const PsChannelInfo *ch = ps_dataset_channel(hit, 0);
    assert(ch && std::strcmp(ch->name, "roll") == 0 && ch->point_count == 3);
    assert(ch->value[1] == 20.0);
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
