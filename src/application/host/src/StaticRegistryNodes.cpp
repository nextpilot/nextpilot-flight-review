#include "StaticRegistryNodes.h"

#include "ModuleInfo.h"

namespace nextpilot::plotscope::host
{

int BuiltinModuleNode::count() const
{
    return BuiltinModuleHub::modules().size();
}

QStringList BuiltinModuleNode::keysAsStrings() const
{
    QStringList out;
    for (const auto &entry : BuiltinModuleHub::modules())
        out.append(entry.first.id);
    return out; // 登记顺序（登记顺序即装载顺序）
}

} // namespace nextpilot::plotscope::host
