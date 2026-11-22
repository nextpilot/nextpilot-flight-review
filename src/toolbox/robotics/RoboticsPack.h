#pragma once

#include "ToolboxHub.h"

#include <QString>

#include <memory>

namespace nextpilot::plotscope::ext
{

// 机器人扩展包：预留领域，机器人消息中间件（MCAP / ROSbag2）格式后续平移时落入本包。
// 包只是一个 build() 工厂：填一份纯数据清单（Pack），宿主 mount() 挂进工具箱。
class RoboticsPack
{
public:
    static QString packId();
    static std::unique_ptr<nextpilot::plotscope::registry::Pack> build();
};

} // namespace nextpilot::plotscope::ext
