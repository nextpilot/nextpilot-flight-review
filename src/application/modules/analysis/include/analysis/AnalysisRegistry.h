#pragma once

#include <functional>
#include <QString>
#include <QStringList>
#include <QVector>

class QWidget;

namespace ps::analysis
{

// 分析入口只描述应用本身；是否可用、需要什么数据由领域工具箱自己判断。
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
    AnalysisKind kind = AnalysisKind::Action;
    // 可选：由领域 App 判断当前窗口是否可用；analysis 只负责调用，不关心依据。
    std::function<bool(QWidget *)> canOpen;
    // App 工厂：返回要放进 Dock 的面板；Action 留空。
    std::function<QWidget *(QWidget *)> create;
};

class AnalysisRegistry
{
public:
    static AnalysisRegistry &instance();

    void add(const AnalysisEntry &entry);
    const QVector<AnalysisEntry> &all() const { return m_entries; }

private:
    QVector<AnalysisEntry> m_entries;
};

} // namespace ps::analysis
