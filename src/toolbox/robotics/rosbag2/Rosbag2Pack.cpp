#include "Rosbag2Pack.h"

#include "Contribution.h"
#include "Rosbag2FormatParser.h"

using namespace nextpilot::plotscope::registry;

namespace nextpilot::plotscope::ext
{

QString Rosbag2Pack::packId()
{
    return QStringLiteral("pack.rosbag2");
}

std::unique_ptr<Pack> Rosbag2Pack::build()
{
    auto pack = std::make_unique<Pack>();
    pack->id = packId();
    pack->name = QStringLiteral("ROS2 bag");
    pack->domain = Domain::Robotics;

    // 格式：ROS2 bag（.db3）→ Dataset（数值批经 SPI 解析器 rosbag2 执行）
    pack->parsers.push_back(std::make_unique<Rosbag2FormatParser>());

    return pack;
}

} // namespace nextpilot::plotscope::ext

PS_REGISTER_PACK(::nextpilot::plotscope::ext::Rosbag2Pack)
