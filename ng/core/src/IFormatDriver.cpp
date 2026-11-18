#include "IFormatDriver.h"

#include <QFileInfo>

namespace ng::core
{

bool IFormatDriver::acceptsSuffix(const QString &path) const
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix.isEmpty())
        return false;
    for (const QString &s : suffixes())
        if (suffix == s.toLower())
            return true;
    return false;
}

} // namespace ng::core
