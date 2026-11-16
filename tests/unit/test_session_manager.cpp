#include "SessionManager.h"
#include "SettingsManager.h"

#include <QApplication>
#include <QMainWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);

    auto &settings = SettingsManager::instance();
    settings.remove(QStringLiteral("lastLayout"));
    settings.setValue(QStringLiteral("lastLayout"), QStringLiteral("/tmp/last.psl"));

    SessionManager session;
    if (session.layoutPath() != QStringLiteral("/tmp/last.psl"))
        return 1;
    if (!SessionManager::defaultLayoutPath().endsWith(QStringLiteral("last.psl")))
        return 1;

    session.addRecentFile(QStringLiteral("/tmp/a.csv"));
    if (session.recentFiles().size() != 1)
        return 1;
    session.clearRecentFiles();
    if (!session.recentFiles().isEmpty())
        return 1;

    QMainWindow window;
    window.resize(800, 600);
    session.saveWindow(&window);
    if (settings.value(QStringLiteral("windowGeometry")).toByteArray().isEmpty())
        return 1;

    QTemporaryDir dir;
    settings.setValue(QStringLiteral("test.key"), QStringLiteral("value"));
    if (!settings.exportTo(dir.filePath(QStringLiteral("settings.json"))))
        return 1;
    QFile file(dir.filePath(QStringLiteral("settings.json")));
    if (!file.open(QIODevice::ReadOnly))
        return 1;
    const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
    if (json.value(QStringLiteral("test.key")).toString() != QLatin1String("value"))
        return 1;

    settings.remove(QStringLiteral("lastLayout"));
    settings.remove(QStringLiteral("windowGeometry"));
    settings.remove(QStringLiteral("windowState"));
    settings.remove(QStringLiteral("test.key"));
    std::printf("session manager ok\n");
    return 0;
}
