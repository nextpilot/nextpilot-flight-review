#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>

struct PsDataset;

// 一个加载器的全部能力：文件匹配、选项 schema、探测、实际加载。
// app 只认识这个结构，不再为每个格式写一个对话框。
struct LoaderPlugin
{
    QString id;
    QString label;
    QString filter;                 // QFileDialog 过滤器
    QString formatTag;              // 数据源列表显示名
    bool tree = false;              // 是否有 topic/ID 层级
    bool hasParams = false;         // 是否有 P/Q、PARM 参数表
    QJsonObject schema;             // 空对象 = 无选项，直接加载

    std::function<QJsonObject(const QString &path, const QJsonObject &opts)> probe;
    std::function<QString(const QString &path)> defaultOptions;
    std::function<int(const QString &path, const QString &opts, PsDataset **ds,
                      char *err, unsigned int errCap)> load;
};
