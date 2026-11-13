#pragma once

#include "data_input/LoaderPlugin.h"
#include "data_input/SourcePlugin.h"

#include <QStringList>
#include <QVector>

struct PsPluginDescriptor;

// 数据输入宿主 catalog：文件与流同一个扫描入口。
// 对外保留 findFileBySuffix / findSourceById 两个查询，内部不再分两个 Registry。
class DataInputRegistry
{
public:
    static DataInputRegistry &instance();

    const LoaderPlugin *findFileBySuffix(const QString &suffix) const;
    const LoaderPlugin *findFileById(const QString &id) const;
    const SourcePlugin *findSourceById(const QString &id) const;

    const QVector<LoaderPlugin> &filePlugins() const { return m_filePlugins; }
    const QVector<SourcePlugin> &streamPlugins() const { return m_streamPlugins; }

    bool registerDescriptor(const PsPluginDescriptor *desc, const QString &packageDir = QString());

    // 外部插件目录只扫描一次；loader/source 由 family 分派。
    QStringList scanPlugins(const QStringList &dirs);

private:
    DataInputRegistry();

    QVector<LoaderPlugin> m_filePlugins;
    QVector<SourcePlugin> m_streamPlugins;
};
