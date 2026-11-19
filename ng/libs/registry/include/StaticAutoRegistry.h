#pragma once

#include "Registry.h"

namespace nextpilot::plotscope::registry
{

// 静态共享表：供「程序启动期自注册」的具体注册表使用。
//
// C++17 inline 变量保证跨 TU 单一实例，本质是**静态对象**——libs 铁律允许的
// 例外（master-plan §7.3 / AGENTS.md §5.1），不是 Meyers 单例，因此不会触发
// LibBoundaryGuard 的 static T& instance() 检查。
//
// 生命周期跟随进程；自注册发生在静态初始化期，与现有 NG_REGISTER_MODULE /
// NG_REGISTER_PACK 惯例一致。适合「只读、启动期填满、运行期只查」的注册表。
// 若注册表需要运行期增删改或可控生命周期，不应使用本表，改由上层持有 Registry<T> 实例。
template <typename T, typename Key = QString>
struct StaticRegistry
{
    static inline Registry<T, Key> table{};
};

// 自注册触发器：构造时即登记到 StaticRegistry<T>。
// 配合 NG_AUTO_REGISTER 宏在 TU 末尾一行完成注册。
template <typename T, typename Key = QString>
struct AutoRegistrar
{
    AutoRegistrar(const Key &id, T entry)
    {
        StaticRegistry<T, Key>::table.insert(id, std::move(entry));
    }
};

} // namespace nextpilot::plotscope::registry

// TU 末尾一行自注册（VarName 须为合法 C++ 标识符，全 TU 唯一）：
//   NG_AUTO_REGISTER(MyEntry, myThing, "my-id", makeMyEntry())
// 展开为匿名命名空间的静态触发器对象——铁律允许的自注册静态对象。
#define NG_AUTO_REGISTER(EntryType, VarName, Id, Expr)                                    \
    namespace                                                                            \
    {                                                                                    \
    const ::nextpilot::plotscope::registry::AutoRegistrar<EntryType> g_ng_auto_##VarName{ \
        (Id), (Expr)};                                                                   \
    }
