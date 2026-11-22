#include "BlfFormatParser.h"

using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;

QString BlfFormatParser::id() const
{
    return QStringLiteral("blf");
}

QString BlfFormatParser::displayName() const
{
    return QStringLiteral("BLF 格式解析器");
}

nextpilot::plotscope::registry::Domain BlfFormatParser::domain() const
{
    return Domain::Automotive;
}

QStringList BlfFormatParser::suffixes() const
{
    return {QStringLiteral("blf")};
}

bool BlfFormatParser::acceptsFormat(const QString &format) const
{
    return format == QStringLiteral("blf") || format == QStringLiteral(".blf");
}
