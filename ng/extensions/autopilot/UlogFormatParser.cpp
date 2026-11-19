#include "UlogFormatParser.h"
using namespace nextpilot::plotscope::core;

QString UlogFormatParser::id() const
{
    return QStringLiteral("ulog");
}

QString UlogFormatParser::displayName() const
{
    return QStringLiteral("ULog 格式解析器");
}

nextpilot::plotscope::registry::Domain UlogFormatParser::domain() const
{
    return nextpilot::plotscope::registry::Domain::Autopilot;
}

QStringList UlogFormatParser::suffixes() const
{
    return {QStringLiteral("ulg"), QStringLiteral("ulog")};
}

bool UlogFormatParser::acceptsFormat(const QString &format) const
{
    return format == QStringLiteral("ulog") || format == QStringLiteral(".ulg");
}
