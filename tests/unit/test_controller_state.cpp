#include "DataSourceController.h"
#include "PlottingController.h"

#include <QCoreApplication>
#include <cstdio>

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++g_fail; } } while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    ps::data_source::DataSourceController ds;
    ds.upsertTimeMarker(2.0, QStringLiteral("A"));
    ds.upsertTimeMarker(1.0, QStringLiteral("B"));
    CHECK(ds.timeMarkers().size() == 2, "two markers");
    CHECK(ds.timeMarkers().at(0).label == QStringLiteral("B"), "markers sorted by time");
    const QString id = ds.timeMarkers().at(0).id;
    CHECK(ds.renameTimeMarker(id, QStringLiteral("B2")), "rename marker");
    CHECK(ds.moveTimeMarker(id, 3.0), "move marker");
    CHECK(ds.removeTimeMarker(id), "remove marker");
    CHECK(ds.timeMarkers().size() == 1, "one marker left");

    ps::plotting::PlottingController pages;
    const QString p1 = pages.addPage(QStringLiteral("P1"));
    const QString p2 = pages.addPage(QStringLiteral("P2"));
    CHECK(pages.pageCount() == 2, "two pages");
    CHECK(pages.currentPageId() == p1, "first page current");
    pages.setCurrentPage(p2);
    CHECK(pages.currentPageId() == p2, "current page set");
    CHECK(pages.removePage(p1), "remove page");
    CHECK(pages.pageCount() == 1, "one page left");
    CHECK(!pages.removePage(p1), "remove unknown page fails");
    const QString p3 = pages.addPage(QStringLiteral("P3"));
    CHECK(pages.indexOf(p3) == 1, "append page");

    std::printf("%s\n", g_fail ? "controller_state FAILED" : "controller_state OK");
    return g_fail ? 1 : 0;
}
