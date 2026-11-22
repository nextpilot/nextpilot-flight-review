#pragma once

#include "ToolboxHub.h"

#include <QString>

#include <memory>

namespace nextpilot::plotscope::ext
{

// 卫导扩展包：预留领域，后续平移 NMEA / UBX / OEM7 格式。
// 包只是一个 build() 工厂：填一份纯数据清单（Pack），宿主 mount() 挂进工具箱。
class GnssPack
{
public:
    static QString packId();
    static std::unique_ptr<nextpilot::plotscope::registry::Pack> build();
};

} // namespace nextpilot::plotscope::ext
