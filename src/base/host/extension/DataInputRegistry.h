#pragma once

#include "host/extension/DataInputEntry.h"

#include <QStringList>
#include <QVector>

struct PsExtensionDescriptor;

// 数据输入宿主 catalog：文件与流同一个扫描入口。
// 对外保留 findFileBySuffix / findSourceById 两个查询，内部不再分两个 Registry。
class DataInputRegistry
{
public:
    static DataInputRegistry &instance();

    const DataInputEntry *findFileBySuffix(const QString &suffix) const;
    const DataInputEntry *findFileById(const QString &id) const;
    const DataInputEntry *findSourceById(const QString &id) const;

    const QVector<DataInputEntry> &fileEntries() const { return m_fileEntries; }
    const QVector<DataInputEntry> &streamEntries() const { return m_streamEntries; }
    const QVector<const PsExtensionDescriptor *> &descriptors() const { return m_descriptors; }

    bool registerDescriptor(const PsExtensionDescriptor *desc, const QString &packageDir = QString());

    // 外部扩展目录只扫描一次；loader/source 由 family 分派。
    QStringList scanExtensions(const QStringList &dirs);

private:
    DataInputRegistry();

    QVector<DataInputEntry> m_fileEntries;
    QVector<DataInputEntry> m_streamEntries;
    QVector<const PsExtensionDescriptor *> m_descriptors;
};
