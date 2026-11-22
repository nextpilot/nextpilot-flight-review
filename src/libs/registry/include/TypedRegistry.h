#pragma once

// M2.8 Registry 机制整改（R2）：通用注册节点——RegistryNode 机制 + 泛型存储。
//
// 【拍板：一 template + 一 type 区分】一次实现全类型复用；owner 语义在这里
// （不在基类——只有 Extension/UiSlot 等少数表有提供者概念，不强迫全部节点实现）。
//
// 三层选型纪律：
//   活对象   RegistryNode / RegistryTree      —— 有生命周期的模块/扩展/面板宿主
//   登记表   TypedRegistry<T,Key>（本类）      —— 需要信号/owner/id 分配的值登记表
//   存储原语 Registry<T,Key>（不改，组合使用）  —— 保序容器底座，仅 registry 库内部、
//                                               纯数据声明式表、静态自注册通道
// 业务登记表禁止裸用 Registry<T>：有生命周期走 TypedRegistry，是活对象就继承 RegistryNode。
//
// 信号：Qt moc 不支持模板类声明新信号，故本类直接发射基类的 objectAdded/
// objectRemoved(QVariant)（带 key，拍板 3）；具体子类用自己的 Q_OBJECT 保留
// 旧具名信号（datasetAdded 等），与基类信号并存（测试零改动）。

#include "Registry.h"
#include "RegistryNode.h"
#include "Result.h"
#include "ErrorCodes.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <type_traits>

namespace nextpilot::plotscope::registry
{

using nextpilot::plotscope::foundation::Result;

namespace detail
{
template <typename> struct always_false : std::false_type
{
};
} // namespace detail

template <typename T, typename Key = QString>
class TypedRegistry : public RegistryNode
{
public:
    // kind/name 由使用方给定（diag.registries 展示用）。必须上树构造——
    // 不存在不上树形态。
    TypedRegistry(RegistryTree &tree, RegistryKind kind, const QString &name,
                  QObject *parent = nullptr)
        : RegistryNode(tree, kind, name, parent)
    {
    }

    // 登记条目（owner 可选）。Key 为整型时自动分配 id；其余 key 类型必须走
    // addWithStableId 显式给 id（编译期拦下）。
    Key add(T entry, const QString &owner = QString())
    {
        if constexpr (std::is_integral<Key>::value)
        {
            const Key id = m_nextId++;
            insertWithOwner(id, std::move(entry), owner);
            return id;
        }
        else
        {
            static_assert(detail::always_false<Key>::value,
                          "非整型 Key 请用 addWithStableId 显式给 id");
            return Key{};
        }
    }

    // 显式稳定 id 存入：录制/回放稳定 id。id 已被占用则失败（先到者为准）；
    // 成功后同步推进自增游标，防止后续自动分配撞车。
    Result<Key> addWithStableId(T entry, Key id, const QString &owner = QString())
    {
        if (m_table.contains(id))
            return nextpilot::plotscope::foundation::fail(
                nextpilot::plotscope::foundation::err::DupId,
                QStringLiteral("id 已存在: %1").arg(keyToString(id)));
        insertWithOwner(id, std::move(entry), owner);
        if constexpr (std::is_integral<Key>::value)
        {
            if (id >= m_nextId)
                m_nextId = id + Key(1);
        }
        return id;
    }

    // 移除条目（不存在返回 false）。发射基类 objectRemoved。
    bool remove(Key id)
    {
        if (!m_table.remove(id))
            return false;
        m_owners.remove(id);
        Q_EMIT objectRemoved(QVariant::fromValue(id));
        return true;
    }

    // 查询条目；不存在返回 nullptr。指针在下次增删前有效。
    T *get(Key id) const { return m_table.find(id); }

    // 已登记 id 列表（插入顺序，与存储原语 keys() 同序）。
    QVector<Key> ids() const
    {
        QVector<Key> out;
        const auto ks = m_table.keys();
        out.reserve(ks.size());
        for (const Key &k : ks)
            out.append(k);
        return out;
    }

    int count() const override { return m_table.count(); }

    // keys 的字符串视图（diag.registries / 树上可枚举的落点）。
    QStringList keysAsStrings() const override
    {
        QStringList out;
        for (const Key &k : m_table.keys())
            out.append(keyToString(k));
        return out;
    }

    // 按提供者原子撤销（挂载单位=owner 的表用，如工具箱按包卸载）。
    bool removeByOwner(const QString &owner)
    {
        QVector<Key> doomed;
        for (auto it = m_owners.constBegin(); it != m_owners.constEnd(); ++it)
            if (it.value() == owner)
                doomed.append(it.key());
        if (doomed.isEmpty())
            return false;
        for (const Key &id : doomed)
            remove(id);
        return true;
    }

    // 全清（按插入序逐条发射 objectRemoved 后清空）。
    void clear()
    {
        const QVector<Key> all = ids();
        m_table.clear();
        m_owners.clear();
        for (const Key &id : all)
            Q_EMIT objectRemoved(QVariant::fromValue(id));
    }

    // 提供者查询（owner 语义落点）。
    QString ownerOf(Key id) const { return m_owners.value(id); }

private:
    void insertWithOwner(const Key &id, T entry, const QString &owner)
    {
        m_table.insert(id, std::move(entry));
        if (!owner.isEmpty())
            m_owners.insert(id, owner);
        Q_EMIT objectAdded(QVariant::fromValue(id));
    }

    static QString keyToString(const Key &k)
    {
        if constexpr (std::is_integral<Key>::value)
            return QString::number(qint64(k));
        else if constexpr (std::is_same_v<Key, QString>)
            return k;
        else
            return QVariant::fromValue(k).toString();
    }

    Registry<T, Key> m_table; // 存储继续委托现有模板（红线：不改它）
    QHash<Key, QString> m_owners;
    Key m_nextId = Key{};     // Key 为整型时作自增游标
};

} // namespace nextpilot::plotscope::registry
