#pragma once

#include <QString>

struct PsDataset;

// 解析结果磁盘缓存：同一文件 + 同一 loader + 同一选项，第二次直接还原数据集，
// 不再走 probe/parse。缓存失效条件：源文件 size/mtime 变化、格式版本变化、手工清空。
class DatasetCache
{
public:
    static QString cacheDir();
    static QString cachePath(const QString &sourcePath, const QString &loaderId, const QString &opts);

    // 命中返回数据集，未命中/损坏返回 nullptr。
    static PsDataset *load(const QString &sourcePath, const QString &loaderId, const QString &opts);

    // 原子写；失败返回 false。
    static bool save(const QString &sourcePath, const QString &loaderId, const QString &opts,
                     const PsDataset *ds);

    static void clear();
};
