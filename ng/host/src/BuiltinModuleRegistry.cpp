#include "BuiltinModuleRegistry.h"

#include "Registry.h"

namespace nextpilot::plotscope::host
{

using nextpilot::plotscope::registry::Registry;

namespace
{

// 函数内 static：避免静态初始化顺序问题（模块 TU 的静态对象可能先于本表构造）。
// 容器本体委托泛型注册表（机制唯一，保序：登记顺序即装载顺序）——
// 与工具箱的包自注册同构，不再各写一份 ordered+index。
struct ModuleEntry
{
    ModuleInfo info;
    ModuleFactory factory;
};

Registry<ModuleEntry> &registry()
{
    static Registry<ModuleEntry> r;
    return r;
}

} // namespace

bool BuiltinModuleRegistry::registerModule(const ModuleInfo &info, ModuleFactory factory)
{
    if (info.id.isEmpty() || !factory)
        return false;
    // insert 自带查重：重复 id 先到者为准，后到者忽略（避免重复装载）。
    return registry().insert(info.id, ModuleEntry{info, std::move(factory)});
}

QList<QPair<ModuleInfo, ModuleFactory>> BuiltinModuleRegistry::modules()
{
    QList<QPair<ModuleInfo, ModuleFactory>> out;
    for (const ModuleEntry &e : registry().values())
        out.append({e.info, e.factory});
    return out;
}

void BuiltinModuleRegistry::reset()
{
    registry().clear();
}

} // namespace nextpilot::plotscope::host
