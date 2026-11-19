#include "BuiltinPackRegistry.h"

#include "ExtensionRegistry.h"

#include <QHash>

namespace nextpilot::plotscope::registry
{

namespace
{

// 函数内 static：避免静态初始化顺序问题。
struct PackStore
{
    QList<QPair<QString, BuiltinPackRegistry::PackFactory>> ordered;
    QHash<QString, int> indexById;
};

PackStore &registry()
{
    static PackStore r;
    return r;
}

} // namespace

bool BuiltinPackRegistry::registerPack(const QString &id, PackFactory factory)
{
    if (id.isEmpty() || !factory)
        return false;
    PackStore &r = registry();
    if (r.indexById.contains(id))
        return false; // 重复 id：先到者为准
    r.indexById.insert(id, r.ordered.size());
    r.ordered.append({id, std::move(factory)});
    return true;
}

QList<QPair<QString, BuiltinPackRegistry::PackFactory>> BuiltinPackRegistry::packs()
{
    return registry().ordered;
}

QStringList BuiltinPackRegistry::installAll(ExtensionRegistry &target)
{
    QStringList installed;
    for (const auto &entry : registry().ordered)
    {
        auto pack = entry.second();
        if (!pack)
            continue;
        if (target.registerPack(std::move(pack)))
            installed.append(entry.first);
    }
    return installed;
}

void BuiltinPackRegistry::reset()
{
    PackStore &r = registry();
    r.ordered.clear();
    r.indexById.clear();
}

} // namespace nextpilot::plotscope::registry
