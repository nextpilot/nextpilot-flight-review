#include "BuiltinPackRegistry.h"

#include "ExtensionRegistry.h"

#include <QHash>

namespace ng::core
{

namespace
{

// 函数内 static：避免静态初始化顺序问题。
struct Registry
{
    QList<QPair<QString, BuiltinPackRegistry::PackFactory>> ordered;
    QHash<QString, int> indexById;
};

Registry &registry()
{
    static Registry r;
    return r;
}

} // namespace

bool BuiltinPackRegistry::registerPack(const QString &id, PackFactory factory)
{
    if (id.isEmpty() || !factory)
        return false;
    Registry &r = registry();
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
    Registry &r = registry();
    r.ordered.clear();
    r.indexById.clear();
}

} // namespace ng::core
