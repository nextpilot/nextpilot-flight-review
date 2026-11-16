#include "MainWindow.h"
#include "DataSourceSession.h"
#include "DataSourceModule.h"

#include <QApplication>
#include <QFile>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);

    MainWindow window;
    const QString path = QStringLiteral(PS_FIXTURE_DIR "/layout_one_page.psl");
    const bool invoked = window.commands().invoke(
        QStringLiteral("layout.import"), {&window, path});
    if (!invoked)
        return 1;
    if (!window.pages() || window.pages()->count() != 2)
        return 1;
    if (window.pages()->tabText(0) != QStringLiteral("恢复页"))
        return 1;
    if (window.session().layoutPath() != path)
        return 1;

    QTemporaryDir dir;
    const QString customPath = dir.filePath(QStringLiteral("custom.psl"));
    window.session().setLayoutPath(customPath);
    auto *ds = ps::components::dataSourceSession(&window);
    ds->addTimeMarker(12.5, QStringLiteral("持久化"), false);
    if (!window.commands().invoke(QStringLiteral("layout.save"), {&window, {}}))
        return 1;
    if (!QFile::exists(customPath))
        return 1;

    QFile saved(customPath);
    if (!saved.open(QIODevice::ReadOnly))
        return 1;
    const QJsonObject root = QJsonDocument::fromJson(saved.readAll()).object();
    const QJsonArray markers = root.value(QStringLiteral("markers")).toArray();
    if (markers.size() != 1)
        return 1;
    const QString id = markers.first().toObject().value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        return 1;

    if (!window.commands().invoke(QStringLiteral("layout.import"), {&window, customPath}))
        return 1;
    const auto &loaded = ds->timeMarkers();
    if (loaded.size() != 1 || loaded.first().id != id || qAbs(loaded.first().t - 12.5) > 1e-6)
        return 1;

    ds->moveTimeMarker(id, 20.0);
    if (ds->timeMarkers().size() != 1 || ds->timeMarkers().first().id != id
        || qAbs(ds->timeMarkers().first().t - 20.0) > 1e-6)
        return 1;

    ds->renameTimeMarker(id, QStringLiteral("改名"));
    if (ds->timeMarkers().first().label != QStringLiteral("改名"))
        return 1;

    QJsonObject legacy = root;
    QJsonArray legacyMarkers = markers;
    QJsonObject legacyMarker = legacyMarkers.first().toObject();
    legacyMarker.remove(QStringLiteral("id"));
    legacyMarkers.replace(0, legacyMarker);
    legacy[QStringLiteral("markers")] = legacyMarkers;
    const QString legacyPath = dir.filePath(QStringLiteral("legacy.psl"));
    QFile legacyFile(legacyPath);
    if (!legacyFile.open(QIODevice::WriteOnly))
        return 1;
    legacyFile.write(QJsonDocument(legacy).toJson());
    legacyFile.close();
    if (!window.commands().invoke(QStringLiteral("layout.import"), {&window, legacyPath}))
        return 1;
    const auto &legacyLoaded = ds->timeMarkers();
    if (legacyLoaded.size() != 1 || legacyLoaded.first().id.isEmpty()
        || qAbs(legacyLoaded.first().t - 12.5) > 1e-6)
        return 1;

    std::printf("layout restore ok\n");
    return 0;
}
