#pragma once

#include "ToolboxHub.h"

#include <QString>

#include <memory>

namespace nextpilot::plotscope::ext
{

// ROS2 bag 扩展包：机器人域的 SQLite3 bag 文件。
class Rosbag2Pack
{
public:
    static QString packId();
    static std::unique_ptr<nextpilot::plotscope::registry::Pack> build();
};

} // namespace nextpilot::plotscope::ext
