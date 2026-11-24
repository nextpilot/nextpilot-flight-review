#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include <utility>

namespace nextpilot::plotscope::registry
{

// 泛型注册表：id → 条目的增删查遍历。具体表定义条目类型后委托本模板，不再各写一份容器。
// 本类只是类型，实例由使用方持有；启动期自注册用 StaticAutoRegistry.h。
//
// 保序语义：keys() 按**插入顺序**返回（非 hash 序）——登记顺序即展示顺序
// （如派生目录、数据集树）。覆盖值用 find() 取指针改写，位置不动。
template <typename T, typename Key = QString>
class Registry
{
public:
    using value_type = T;
    using key_type   = Key;

    // D10 启动期冻结：贡献点收集完成后调用，此后所有写操作被拒（先到者为准的顺序不再变）。
    // 默认不冻结——纯数据容器行为不变，只有显式 freeze() 的表才锁定。
    void freeze() { m_frozen = true; }
    bool isFrozen() const { return m_frozen; }

    // 登记条目；重复 id 或已冻结返回 false（先到者为准）。登记顺序被记录。
    bool insert(const Key &id, T entry)
    {
        if (m_frozen || m_map.contains(id))
            return false;
        m_map.insert(id, std::move(entry));
        m_order.append(id);
        return true;
    }

    // 移除条目，返回是否存在并被移除；已冻结拒绝。
    bool remove(const Key &id)
    {
        if (m_frozen)
            return false;
        if (m_map.remove(id) == 0)
            return false;
        m_order.removeOne(id);
        return true;
    }

    // 查询条目；不存在返回 nullptr。指针在下次增删前有效（可变访问用于改值）。
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

    // 已登记 id 列表，**按插入顺序**。
    QList<Key> keys() const { return m_order; }

    // 已登记条目列表，按插入顺序。
    QList<T> values() const
    {
        QList<T> out;
        out.reserve(m_order.size());
        for (const Key &k : m_order)
            out.append(m_map.value(k));
        return out;
    }

    int count() const { return m_map.size(); }
    bool isEmpty() const { return m_map.isEmpty(); }
    void clear()
    {
        if (m_frozen)
            return;
        m_map.clear();
        m_order.clear();
    }

private:
    QHash<Key, T> m_map;
    QList<Key> m_order; // 插入顺序（保序视图）
    bool m_frozen = false;
};

} // namespace nextpilot::plotscope::registry
