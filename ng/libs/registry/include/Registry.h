#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include <utility>

namespace nextpilot::plotscope::registry
{

// 泛型注册表：id → 条目的增删查遍历。具体表定义条目类型后委托本模板，不再各写一份容器。
// 本类只是类型，实例由使用方持有；启动期自注册用 StaticAutoRegistry.h。
template <typename T, typename Key = QString>
class Registry
{
public:
    using value_type = T;
    using key_type   = Key;

    // 登记条目；重复 id 返回 false（先到者为准）。
    bool insert(const Key &id, T entry)
    {
        if (m_map.contains(id))
            return false;
        m_map.insert(id, std::move(entry));
        return true;
    }

    // 移除条目，返回是否存在并被移除。
    bool remove(const Key &id)
    {
        return m_map.remove(id) > 0;
    }

    // 查询条目；不存在返回 nullptr。指针在下次增删前有效。
    T *find(const Key &id) const
    {
        auto it = m_map.constFind(id);
        if (it == m_map.constEnd())
            return nullptr;
        return const_cast<T *>(&it.value());
    }

    // 取条目副本；不存在返回 defaultValue。指针型条目用它比 find() 顺手。
    T value(const Key &id, const T &defaultValue = T{}) const
    {
        auto it = m_map.constFind(id);
        return it == m_map.constEnd() ? defaultValue : it.value();
    }

    bool contains(const Key &id) const { return m_map.contains(id); }

    // 已登记 id 列表，顺序不保证。
    QList<Key> keys() const { return m_map.keys(); }

    int count() const { return m_map.size(); }
    bool isEmpty() const { return m_map.isEmpty(); }
    void clear() { m_map.clear(); }

private:
    QHash<Key, T> m_map;
};

} // namespace nextpilot::plotscope::registry
