#include "Toolbox.h"

#include "BuiltinPackRegistry.h"

namespace nextpilot::plotscope::ext
{

void registerBuiltinPacks(nextpilot::plotscope::registry::ExtensionRegistry &registry)
{
    // 内置知识包由各自 TU 静态自注册（NG_REGISTER_PACK），这里只负责把它们
    // 装进工具箱。app 不认识 CSV/ULog 或任何具体贡献类型。
    //
    // 新增一个领域包 = 新增一个目录 + 一行 CMake，本文件与宿主代码都不用动。
    nextpilot::plotscope::registry::BuiltinPackRegistry::installAll(registry);
}

} // namespace nextpilot::plotscope::ext
