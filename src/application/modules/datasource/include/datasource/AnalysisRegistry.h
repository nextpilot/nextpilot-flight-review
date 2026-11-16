#pragma once

#include <functional>
#include <QString>
#include <QStringList>
#include <QVector>

class QWidget;

namespace ps::data_source
{

// 分析入口只声明“哪个数据格式适用”，命令实现仍由领域工具箱注册。
// Action 执行命令；App 通过 create() 工厂打开独立面板。
enum class AnalysisKind
{
    Action,
    App,
};

struct AnalysisEntry
{
    QString id;
    QString title;
    QString icon;
    QString description;
    QStringList tags;
    QStringList formats; // 匹配 SourceData::fmt；为空表示不限制
    AnalysisKind kind = AnalysisKind::Action;
    // App 工厂：传入父窗口和当前源路径，返回要放进 Dock 的面板；Action 留空。
    std::function<QWidget *(QWidget *, const QString &)> create;
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
