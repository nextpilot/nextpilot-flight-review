#include "MdfFormatParser.h"

using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;

QString MdfFormatParser::id() const
{
    return QStringLiteral("mdf4");
}

QString MdfFormatParser::displayName() const
{
    return QStringLiteral("MDF 4.x 格式解析器");
}

nextpilot::plotscope::registry::Domain MdfFormatParser::domain() const
{
    return Domain::Automotive;
}

QStringList MdfFormatParser::suffixes() const
{
    return {QStringLiteral("mf4"), QStringLiteral("mdf4"), QStringLiteral("mdf")};
}

bool MdfFormatParser::acceptsFormat(const QString &format) const
{
    return format == QStringLiteral("mdf4") || format == QStringLiteral(".mf4") ||
           format == QStringLiteral("mdf") || format == QStringLiteral(".mdf");
}
