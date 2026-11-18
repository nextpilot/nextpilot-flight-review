#include "FormatDomain.h"

namespace ng::core
{

const char *domainKey(FormatDomain domain)
{
    switch (domain)
    {
    case FormatDomain::General:    return "general";
    case FormatDomain::Autopilot:  return "autopilot";
    case FormatDomain::Automotive: return "automotive";
    case FormatDomain::Robotics:   return "robotics";
    case FormatDomain::Gnss:       return "gnss";
    case FormatDomain::Video:      return "video";
    }
    return "general";
}

QString domainName(FormatDomain domain)
{
    switch (domain)
    {
    case FormatDomain::General:    return QStringLiteral("通用");
    case FormatDomain::Autopilot:  return QStringLiteral("飞控");
    case FormatDomain::Automotive: return QStringLiteral("汽车");
    case FormatDomain::Robotics:   return QStringLiteral("机器人");
    case FormatDomain::Gnss:       return QStringLiteral("卫导");
    case FormatDomain::Video:      return QStringLiteral("视频");
    }
    return QStringLiteral("通用");
}

} // namespace ng::core
