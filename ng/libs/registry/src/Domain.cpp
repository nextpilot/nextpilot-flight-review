#include "Domain.h"

namespace nextpilot::plotscope::registry
{

const char *domainKey(Domain domain)
{
    switch (domain)
    {
    case Domain::General:    return "general";
    case Domain::Autopilot:  return "autopilot";
    case Domain::Automotive: return "automotive";
    case Domain::Robotics:   return "robotics";
    case Domain::Gnss:       return "gnss";
    case Domain::Video:      return "video";
    }
    return "general";
}

QString domainName(Domain domain)
{
    switch (domain)
    {
    case Domain::General:    return QStringLiteral("通用");
    case Domain::Autopilot:  return QStringLiteral("飞控");
    case Domain::Automotive: return QStringLiteral("汽车");
    case Domain::Robotics:   return QStringLiteral("机器人");
    case Domain::Gnss:       return QStringLiteral("卫导");
    case Domain::Video:      return QStringLiteral("视频");
    }
    return QStringLiteral("通用");
}

} // namespace nextpilot::plotscope::registry
