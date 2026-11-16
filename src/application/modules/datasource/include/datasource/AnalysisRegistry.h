#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace ps::data_source
{

// 分析入口只声明“哪个数据格式适用”，命令实现仍由领域工具箱注册。
// 将来 Analysis App 可以复用同一份条目，增加 create() 工厂即可。
struct AnalysisEntry
{
    QString id;
    QString title;
    QString icon;
    QString description;
    QStringList tags;
    QStringList formats; // 匹配 SourceData::fmt；为空表示不限制
};

class AnalysisRegistry
{
public:
    static AnalysisRegistry &instance();

    void add(const AnalysisEntry &entry);
    const QVector<AnalysisEntry> &all() const { return m_entries; }
    QVector<AnalysisEntry> forFormat(const QString &format) const;

private:
    QVector<AnalysisEntry> m_entries;
};

} // namespace ps::data_source
