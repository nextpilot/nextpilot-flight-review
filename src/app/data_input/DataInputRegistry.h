#pragma once

#include "data_input/DataInputPlugin.h"

#include <QStringList>
#include <QVector>

struct PsPluginDescriptor;

// 数据输入宿主 catalog：文件与流同一个扫描入口。
// 对外保留 findFileBySuffix / findSourceById 两个查询，内部不再分两个 Registry。
class DataInputRegistry
{
public:
    static DataInputRegistry &instance();

    const DataInputPlugin *findFileBySuffix(const QString &suffix) const;
    const DataInputPlugin *findFileById(const QString &id) const;
    const DataInputPlugin *findSourceById(const QString &id) const;

    const QVector<DataInputPlugin> &filePlugins() const { return m_filePlugins; }
    const QVector<DataInputPlugin> &streamPlugins() const { return m_streamPlugins; }

    bool registerDescriptor(const PsPluginDescriptor *desc, const QString &packageDir = QString());

    // 外部插件目录只扫描一次；loader/source 由 family 分派。
    QStringList scanPlugins(const QStringList &dirs);

private:
    DataInputRegistry();

    QVector<DataInputPlugin> m_filePlugins;
    QVector<DataInputPlugin> m_streamPlugins;
};
