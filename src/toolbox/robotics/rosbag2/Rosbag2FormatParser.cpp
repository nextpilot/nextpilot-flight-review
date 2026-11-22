#include "Rosbag2FormatParser.h"

using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;

QString Rosbag2FormatParser::id() const
{
    return QStringLiteral("rosbag2");
}

QString Rosbag2FormatParser::displayName() const
{
    return QStringLiteral("ROS2 bag 格式解析器");
}

nextpilot::plotscope::registry::Domain Rosbag2FormatParser::domain() const
{
    return Domain::Robotics;
}

QStringList Rosbag2FormatParser::suffixes() const
{
    return {QStringLiteral("db3"), QStringLiteral("db2")};
}

bool Rosbag2FormatParser::acceptsFormat(const QString &format) const
{
    return format == QStringLiteral("rosbag2") || format == QStringLiteral(".db3") ||
           format == QStringLiteral("db3") || format == QStringLiteral(".db2");
}
