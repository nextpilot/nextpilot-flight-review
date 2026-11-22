#include "Toolbox.h"

#include "ToolboxHub.h"

namespace nextpilot::plotscope::ext
{

void registerBuiltinPacks(nextpilot::plotscope::registry::ToolboxHub &registry)
{
    // 内置扩展包由各自 TU 静态自注册（PS_REGISTER_PACK），这里只负责把它们
    // 装进工具箱。app 不认识 CSV/ULog 或任何具体贡献类型。
    //
    // 新增一个领域包 = 新增一个目录 + 一行 CMake，本文件与宿主代码都不用动。
    // 装载动作在 ToolboxHub 本体（原 BuiltinPackRegistry 已折叠进容器）：
    registry.installBuiltinPacks();
}

} // namespace nextpilot::plotscope::ext
