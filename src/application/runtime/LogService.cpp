#include "LogService.h"

#include <QFile>
#include <QTextStream>

namespace ps::runtime
{

LogService::LogService(const QString &path)
    : m_path(path)
{
}

void LogService::append(const QString &line) const
{
    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    QTextStream out(&file);
    out << line << '\n';
}

} // namespace ps::runtime
