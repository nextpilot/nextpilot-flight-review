#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include <utility>

namespace nextpilot::plotscope::registry
{

// 泛型注册表核：id → 条目 的登记 / 查询 / 移除 / 遍历。
//
// 这是所有具体注册表（ParserRegistry / PanelFactoryRegistry / BuiltinPackRegistry…）
// 的公共最小实现。各具体注册表退化为「定义条目类型 T + 薄壳委托」，
// 不再各写一份 QHash + 增删查遍历。
//
// 架构约束（master-plan §7 / AGENTS.md §5）：
//   - 本类是**类型定义**，不是活实例。谁要用谁在上层持有实例，
//     生命周期归组合根；库里不出现全局活实例。
//   - 需要「静态初始化期自注册」的具体注册表，用 StaticAutoRegistry.h 的
//     StaticRegistry<T>（C++17 inline 变量，属铁律允许的「静态对象」例外）。
//   - 只依赖 Qt Core + STL，无 QWidget、无 Rust、无 core 业务类型，headless 可用。
template <typename T, typename Key = QString>
class Registry
{
public:
    using value_type = T;
    using key_type   = Key;

    // 登记条目。重复 id 返回 false（先到者为准，与旧注册表语义一致）。
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

    // 查询条目；不存在返回 nullptr。调用方读到的指针在下次 insert/remove 前有效。
    T *find(const Key &id) const
    {
        auto it = m_map.constFind(id);
        if (it == m_map.constEnd())
            return nullptr;
        return const_cast<T *>(&it.value());
    }

    bool contains(const Key &id) const { return m_map.contains(id); }

    // 已登记 id 列表（顺序不保证，需要稳定顺序的调用方自行排序）。
    QList<Key> keys() const { return m_map.keys(); }

    int count() const { return m_map.size(); }
    bool isEmpty() const { return m_map.isEmpty(); }
    void clear() { m_map.clear(); }

private:
    QHash<Key, T> m_map;
};

} // namespace nextpilot::plotscope::registry
