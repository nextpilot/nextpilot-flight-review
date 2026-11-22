#pragma once

#include "ToolboxHub.h"

#include <QString>

#include <memory>

namespace nextpilot::plotscope::ext
{

// 汽车扩展包：预留领域，后续平移 ASC/BLF/MDF4/CAN 格式时落入本包。
// 包只是一个 build() 工厂：填一份纯数据清单（Pack），宿主 mount() 挂进工具箱。
class AutomotivePack
{
public:
    static QString packId();
    static std::unique_ptr<nextpilot::plotscope::registry::Pack> build();
};

} // namespace nextpilot::plotscope::ext
