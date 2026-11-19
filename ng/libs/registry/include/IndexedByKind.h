#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <utility>

namespace nextpilot::plotscope::registry
{

// 二维索引：kind → 条目列表。只负责按 kind 分组增删查，不持有所有权、不保全局顺序。
// 是可组合的能力片，实例由使用方（具体注册表薄壳）持有，本身无全局状态。
template <typename T>
class IndexedByKind
{
public:
    using value_type = T;

    // 把一个条目挂到 kind 下；同 kind 内保插入顺序。
    void insert(const QString &kind, T entry)
    {
        m_byKind[kind].append(std::move(entry));
    }

    // 取某 kind 的全部条目；不存在返回空表。
    QList<T> values(const QString &kind) const
    {
        return m_byKind.value(kind, {});
    }

    // 当前所有非空 kind，顺序不保证。
    QStringList kinds() const
    {
        QStringList out;
        for (auto it = m_byKind.constBegin(); it != m_byKind.constEnd(); ++it)
            if (!it.value().isEmpty())
                out.append(it.key());
        return out;
    }

    void clear() { m_byKind.clear(); }
    int count() const
    {
        int total = 0;
        for (auto it = m_byKind.constBegin(); it != m_byKind.constEnd(); ++it)
            total += it.value().size();
        return total;
    }
    bool isEmpty() const { return m_byKind.isEmpty(); }

private:
    QHash<QString, QList<T>> m_byKind;
};

} // namespace nextpilot::plotscope::registry
