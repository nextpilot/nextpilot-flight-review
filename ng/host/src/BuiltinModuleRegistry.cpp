#include "BuiltinModuleRegistry.h"

#include <QHash>

namespace nextpilot::plotscope::host
{

namespace
{

// 函数内 static：避免静态初始化顺序问题（模块 TU 的静态对象可能先于本表构造）。
struct Registry
{
    QList<QPair<ModuleInfo, ModuleFactory>> ordered;
    QHash<QString, int> indexById; // id -> ordered 下标，用于查重
};

Registry &registry()
{
    static Registry r;
    return r;
}

} // namespace

bool BuiltinModuleRegistry::registerModule(const ModuleInfo &info, ModuleFactory factory)
{
    if (info.id.isEmpty() || !factory)
        return false;
    Registry &r = registry();
    if (r.indexById.contains(info.id))
        return false; // 重复 id：先到者为准，后到者忽略（避免重复装载）
    r.indexById.insert(info.id, r.ordered.size());
    r.ordered.append({info, std::move(factory)});
    return true;
}

QList<QPair<ModuleInfo, ModuleFactory>> BuiltinModuleRegistry::modules()
{
    return registry().ordered;
}

void BuiltinModuleRegistry::reset()
{
    Registry &r = registry();
    r.ordered.clear();
    r.indexById.clear();
}

} // namespace nextpilot::plotscope::host
