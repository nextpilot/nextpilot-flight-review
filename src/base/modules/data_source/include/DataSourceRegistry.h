#pragma once

#include "DataSourceEntry.h"

#include <QStringList>

#include <functional>
#include <QVector>

struct PsExtensionDescriptor;
class QDialog;
class QWidget;

// 数据输入宿主 catalog：文件与流同一个扫描入口。
// 对外保留 findFileBySuffix / findSourceById 两个查询，内部不再分两个 Registry。
class DataSourceRegistry
{
public:
    static DataSourceRegistry &instance();

    const DataSourceEntry *findFileBySuffix(const QString &suffix) const;
    const DataSourceEntry *findFileById(const QString &id) const;
    const DataSourceEntry *findSourceById(const QString &id) const;

    const QVector<DataSourceEntry> &fileEntries() const { return m_fileEntries; }
    const QVector<DataSourceEntry> &streamEntries() const { return m_streamEntries; }
    const QVector<const PsExtensionDescriptor *> &descriptors() const { return m_descriptors; }

    bool registerDescriptor(const PsExtensionDescriptor *desc, const QString &packageDir = QString(),
                            const std::function<QDialog *(const QString &, QWidget *)> &editor = {});

    // 外部扩展目录只扫描一次；loader/source 由 family 分派。
    QStringList scanExtensions(const QStringList &dirs);

    // 只注册 ExtensionRuntime 已装载的数据输入扩展；组件注册由 ExtensionHost 负责。
    QStringList registerLoadedExtensions();

private:
    DataSourceRegistry();

    QVector<DataSourceEntry> m_fileEntries;
    QVector<DataSourceEntry> m_streamEntries;
    QVector<const PsExtensionDescriptor *> m_descriptors;
};
