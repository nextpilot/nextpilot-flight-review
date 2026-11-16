#include "ShellPaths.h"

#include <QDir>
#include <QStandardPaths>

QString ShellPaths::configDir()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.plotscope");
    return dir;
}

QString ShellPaths::logDir()
{
    return configDir() + QStringLiteral("/logs");
}

QString ShellPaths::logPath()
{
    return logDir() + QStringLiteral("/plotscope.log");
}

QString ShellPaths::defaultLayoutPath()
{
    return configDir() + QStringLiteral("/last.psl");
}
