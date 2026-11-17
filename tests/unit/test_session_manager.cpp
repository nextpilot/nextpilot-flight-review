#include "SessionManager.h"
#include "SessionCommands.h"
#include "SettingsManager.h"
#include "CommandManager.h"

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
    settings.remove(ps::settings::kLastLayout);
    settings.remove(ps::settings::kRecentFiles);
    settings.set(ps::settings::kLastLayout, QStringLiteral("/tmp/last.psl"));

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
    if (settings.get(ps::settings::kWindowGeometry).isEmpty())
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
    if (!settings.exportTo(dir.filePath(QStringLiteral("settings.yaml"))))
        return 1;
    QFile yaml(dir.filePath(QStringLiteral("settings.yaml")));
    if (!yaml.open(QIODevice::ReadOnly))
        return 1;
    const QString y = QString::fromUtf8(yaml.readAll());
    if (!y.contains(QStringLiteral("test.key: \"value\"")))
        return 1;
    if (settings.exportTo(dir.path()))
        return 1;

    ps::commands::CommandManager commands;
    shell::bindSessionCommandState(commands, session, [] {});
    session.markClean();
    ps::commands::CommandSpec dirty;
    dirty.id = QStringLiteral("test.dirty");
    dirty.sessionDirty = true;
    commands.registerCommand(dirty, [](const ps::commands::CommandContext &) {});
    commands.invoke(QStringLiteral("test.dirty"), {});
    if (!session.isDirty())
        return 1;

    session.markClean();
    commands.add(QStringLiteral("layout.save"), [](const ps::commands::CommandContext &) {});
    commands.invoke(QStringLiteral("layout.save"), {});
    if (session.isDirty())
        return 1;

    settings.remove(ps::settings::kLastLayout);
    settings.remove(ps::settings::kWindowGeometry);
    settings.remove(ps::settings::kWindowState);
    settings.remove(QStringLiteral("test.key"));
    std::printf("session manager ok\n");
    return 0;
}
