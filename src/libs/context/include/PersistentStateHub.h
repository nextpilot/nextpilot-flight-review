#pragma once

#include <QHash>
#include <QJsonObject>
#include <QPair>
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
    // 可选：内容版本号。提供后，collect() 在版本未变时复用上次的序列化结果，
    // 避免重复执行昂贵的 serialize（如导出全部曲线）。留空 = 每次都重算（默认，兼容现有登记点）。
    // 约束：serialize 的输出必须只依赖 revision 覆盖的内容，否则会写出过期快照。
    std::function<quint64()> revision;
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

    // 诊断：本次 collect 中命中 revision 缓存（跳过 serialize）的条目数。
    int lastCacheHits() const { return m_lastCacheHits; }

private:
    QVector<PersistentStateEntry> m_entries;
    // revision 未变时复用上次序列化结果。mutable 让 collect() 保持 const（外部零影响）。
    // key=entry.id，value={上次 revision, 上次 serialize 结果}。
    mutable QHash<QString, QPair<quint64, QJsonObject>> m_serializeCache;
    mutable int m_lastCacheHits = 0;
};

} // namespace nextpilot::plotscope::context
