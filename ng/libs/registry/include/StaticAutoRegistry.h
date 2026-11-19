#pragma once

#include "Registry.h"

namespace nextpilot::plotscope::registry
{

// 启动期自注册表：inline 变量保证跨 TU 单一实例，进程生命周期，静态初始化期填好。
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

} // namespace nextpilot::plotscope::registry

// TU 末尾一行自注册：NG_AUTO_REGISTER(条目类型, 变量名, "id", 构造表达式)
// 变量名在 TU 内唯一。展开为匿名命名空间的静态对象。
#define NG_AUTO_REGISTER(EntryType, VarName, Id, Expr)                                    \
    namespace                                                                            \
    {                                                                                    \
    const ::nextpilot::plotscope::registry::AutoRegistrar<EntryType> g_ng_auto_##VarName{ \
        (Id), (Expr)};                                                                   \
    }
