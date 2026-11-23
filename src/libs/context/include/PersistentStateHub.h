#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace nextpilot::plotscope::context
{

// 一条可整体序列化/恢复的状态条目。
// Session 进快照的会话段，Document 进配置段；登记序即恢复序。
struct PersistentStateEntry
{
    QString id;
    enum Scope
    {
        Session,
        Document
    };
    Scope scope = Session;
    int version = 1;
    std::function<QJsonObject()> serialize;
    std::function<void(const QJsonObject &)> restore;
    bool bestEffort = true; // 失败静默，不阻断其余条目
};

// 结构化状态的登记与收集。hub 不持有文件路径：读写由外壳负责，这里只产出/消费 JSON。
// 简单标量（主题、上限、最近列表）走 SettingsStore，不进 hub，避免同一键两处写。
class PersistentStateHub
{
public:
    PersistentStateHub() = default;

    void registerEntry(PersistentStateEntry entry);
    QJsonObject collect() const;
    void restore(const QJsonObject &hubDoc);

    int count() const { return static_cast<int>(m_entries.size()); }
    QStringList ids() const;

private:
    QVector<PersistentStateEntry> m_entries;
};

} // namespace nextpilot::plotscope::context
