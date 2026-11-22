#pragma once

#include "Registry.h"

#include <QString>

namespace nextpilot::plotscope::registry
{

// 启动期自注册表：inline 变量保证跨 TU 单一实例，进程生命周期，静态初始化期填好。
// [静态纪律·合法形态] 仅用于"进程一份、无生命周期、自注册"的表（面板工厂等）；
// 有运行时生命周期的状态禁止放静态存储——实例归组合根（AppContext）持有。
// 只适合"装满后只读"的表；要运行期增删的自己持有 Registry 实例。
template <typename T, typename Key = QString>
struct StaticRegistry
{
    static inline Registry<T, Key> table{};
};

// 构造时登记一个条目，供宏在静态初始化期触发。
template <typename T, typename Key = QString>
struct AutoRegistrar
{
    AutoRegistrar(const Key &id, T entry)
    {
        StaticRegistry<T, Key>::table.insert(id, std::move(entry));
    }
};

// 通用静态表门面：命名静态表的**唯一**实现（禁止再手写"QHash+order+register/find"）。
// 存储 = Registry 原语（保序：keys() 即登记顺序）+ 惰性 Meyers 表（函数局部 static，
// 免疫跨 TU 静态初始化顺序问题）。策略固定：空 id 拒收、Entry::valid() 拒收、先到者为准。
//
// 用法：新增一张静态表 = Entry 结构（带 valid()）+ `using X = StaticHub<Entry>` 别名；
// 有额外领域方法时继承它补（参考 PanelFactoryHub::create）——只补领域方法，不碰存储。
// 运行期要增删的表**不用这个**（走 TypedRegistry/Registry 实例，归组合根持有）。
template <typename Entry, typename Key = QString>
class StaticHub
{
public:
    // 登记一条。空 id / 无效条目拒收；重复 id 先到者为准（Registry 保证）。
    static bool insert(const Key &id, Entry entry)
    {
        if (id.isEmpty() || !entry.valid())
            return false;
        return table().insert(id, std::move(entry));
    }

    static const Entry *find(const Key &id) { return table().find(id); }
    static bool remove(const Key &id) { return table().remove(id); }
    static QList<Key> keys() { return table().keys(); } // 登记顺序
    static int count() { return table().count(); }
    static void reset() { table().clear(); } // 仅测试用：同进程重复装配

private:
    // 函数局部 static：表保证先于一切登记者构造。命名避开守卫禁用的 instance()。
    static Registry<Entry, Key> &table()
    {
        static Registry<Entry, Key> t;
        return t;
    }
};

} // namespace nextpilot::plotscope::registry

// TU 末尾一行自注册：PS_AUTO_REGISTER(条目类型, 变量名, "id", 构造表达式)
// 变量名在 TU 内唯一。展开为匿名命名空间的静态对象。
#define PS_AUTO_REGISTER(EntryType, VarName, Id, Expr)                                    \
    namespace                                                                            \
    {                                                                                    \
    const ::nextpilot::plotscope::registry::AutoRegistrar<EntryType> g_ps_auto_##VarName{ \
        (Id), (Expr)};                                                                   \
    }
