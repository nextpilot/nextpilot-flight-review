#include "ApbinFormatParser.h"

using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;

QString ApbinFormatParser::id() const
{
    return QStringLiteral("apbin");
}

QString ApbinFormatParser::displayName() const
{
    return QStringLiteral("DataFlash 格式解析器");
}

nextpilot::plotscope::registry::Domain ApbinFormatParser::domain() const
{
    return Domain::Autopilot;
}

QStringList ApbinFormatParser::suffixes() const
{
    return {QStringLiteral("bin"), QStringLiteral("log")};
}

bool ApbinFormatParser::acceptsFormat(const QString &format) const
{
    return format == QStringLiteral("apbin") || format == QStringLiteral(".bin") ||
           format == QStringLiteral("bin") || format == QStringLiteral(".log");
}
