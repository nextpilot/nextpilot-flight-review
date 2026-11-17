#include "SettingsManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QVariant>
#include <cstdio>

static int failures = 0;

static void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

static bool waitFor(int &value, int expected, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (value < expected && timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(20);
    }
    return value >= expected;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid())
        return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());

    auto &settings = SettingsManager::instance();
    settings.remove(ps::settings::kTheme);
    settings.remove(ps::settings::kRestoreLayout);
    settings.remove(ps::settings::kDerivedMaxPoints);

    check(settings.get(ps::settings::kTheme) == QStringLiteral("dark"), "theme default");
    check(settings.get(ps::settings::kRestoreLayout) == false, "restoreLayout default");
    check(settings.get(ps::settings::kDerivedMaxPoints) == 2000000, "derivedMaxPoints default");

    settings.set(ps::settings::kDerivedMaxPoints, 42);
    check(settings.get(ps::settings::kDerivedMaxPoints) == 42, "typed int round-trip");

    settings.setValue(QStringLiteral("restoreLayout"), QStringLiteral("bad-bool"));
    check(settings.get(ps::settings::kRestoreLayout) == false, "type mismatch falls back");
    settings.setValue(QStringLiteral("restoreLayout"), QStringLiteral("true"));
    check(settings.get(ps::settings::kRestoreLayout) == true, "legacy bool string");
    settings.remove(ps::settings::kRestoreLayout);

    int themeChanges = 0;
    QVariant lastOld, lastNew;
    settings.onChanged(QStringLiteral("theme"), &app, [&](const QVariant &oldValue, const QVariant &newValue) {
        ++themeChanges;
        lastOld = oldValue;
        lastNew = newValue;
    });

    settings.set(ps::settings::kTheme, QStringLiteral("light"));
    check(themeChanges == 1, "first change emits");
    settings.set(ps::settings::kTheme, QStringLiteral("light"));
    check(themeChanges == 1, "same value does not emit");
    settings.set(ps::settings::kTheme, QStringLiteral("dark"));
    check(themeChanges == 2, "different value emits");
    check(lastOld.toString() == QStringLiteral("light") && lastNew.toString() == QStringLiteral("dark"),
          "old/new values");

    settings.remove(ps::settings::kTheme);
    check(themeChanges == 3, "remove emits");
    check(!lastNew.isValid(), "remove emits invalid new value");
    check(settings.get(ps::settings::kTheme) == QStringLiteral("dark"), "remove restores default");

    settings.set(ps::settings::kTheme, QStringLiteral("light"));
    // 显式 IniFormat：QSettings(org, app) 在 Windows 上会忽略 defaultFormat 落注册表，写不到临时文件。
    QSettings external(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("NextPilot"),
                       QStringLiteral("PlotScope"));
    external.setValue(QStringLiteral("theme"), QStringLiteral("dark"));
    external.sync();

    const int beforeExternal = themeChanges;
    settings.sync();
    check(themeChanges == beforeExternal + 1, "manual sync picks up external change");

    external.setValue(QStringLiteral("theme"), QStringLiteral("light"));
    external.sync();
    check(waitFor(themeChanges, beforeExternal + 2, 1800), "file watcher picks up external change");

    int typedTheme = 0;
    const auto typedConn = settings.onChanged(ps::settings::kTheme, &app, [&](const QString &) { ++typedTheme; });
    settings.set(ps::settings::kTheme, QStringLiteral("dark"));
    check(typedTheme == 1, "typed onChanged");
    QObject::disconnect(typedConn);

    check(settings.exportTo(QString()) == false, "empty export path fails");
    QFile blocker(dir.filePath(QStringLiteral("blocker")));
    check(blocker.open(QIODevice::WriteOnly), "blocker file");
    const QString badPath = blocker.fileName() + QStringLiteral("/out.json");
    check(settings.exportTo(badPath) == false, "unwritable export path fails");
    check(!settings.toJson().isEmpty(), "toJson has keys");

    settings.set(ps::settings::kAnalysisRecent, QStringList{QStringLiteral("a"), QStringLiteral("b")});
    check(settings.get(ps::settings::kAnalysisRecent) == QStringList{QStringLiteral("a"), QStringLiteral("b")},
          "analysis recent list round-trip");
    settings.set(ps::settings::kAnalysisFavorites, QStringList{QStringLiteral("autopilot.review.app")});
    check(settings.get(ps::settings::kAnalysisFavorites).size() == 1,
          "analysis favorites list round-trip");
    settings.remove(ps::settings::kAnalysisRecent);
    settings.remove(ps::settings::kAnalysisFavorites);
    check(settings.get(ps::settings::kAnalysisRecent).isEmpty(), "analysis recent default empty");

    return failures == 0 ? 0 : 1;
}
