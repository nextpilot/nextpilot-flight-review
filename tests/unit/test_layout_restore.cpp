#include "MainWindow.h"

#include <QApplication>
#include <QStandardPaths>
#include <QTabWidget>
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

    std::printf("layout restore ok\n");
    return 0;
}
