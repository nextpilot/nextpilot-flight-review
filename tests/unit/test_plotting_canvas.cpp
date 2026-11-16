#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QWheelEvent>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <cmath>
#include <cstdio>

#include "PlottingCanvas.h"

static int failures = 0;

static void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

static void mouse(QWidget *w, QEvent::Type type, const QPointF &pos, Qt::MouseButton button,
                  Qt::MouseButtons buttons, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent e(type, pos, w->mapToGlobal(pos), button, buttons, mods);
    QCoreApplication::sendEvent(w, &e);
}

static void wheel(QWidget *w, const QPointF &pos, int delta, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QWheelEvent e(pos, w->mapToGlobal(pos), QPoint(), QPoint(0, delta), Qt::NoButton, mods,
                  Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(w, &e);
}

static void key(QWidget *w, int k, Qt::KeyboardModifiers mods = Qt::NoModifier, QEvent::Type type = QEvent::KeyPress)
{
    QKeyEvent e(type, k, mods);
    QCoreApplication::sendEvent(w, &e);
}

static QString menuMiss;

static void chooseMenuAction(const QString &textPrefix, int delayMs = 20)
{
    menuMiss.clear();
    QTimer::singleShot(delayMs, [textPrefix]() {
        QMenu *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu)
        {
            for (QWidget *w : QApplication::topLevelWidgets())
                if (auto *m = qobject_cast<QMenu *>(w))
                    m->close();
            menuMiss = QStringLiteral("no active menu");
            return;
        }
        QAction *target = nullptr;
        for (QAction *a : menu->actions())
        {
            if (a->text().startsWith(textPrefix))
            {
                target = a;
                break;
            }
        }
        if (!target)
        {
            menuMiss = QStringLiteral("action not found: ") + textPrefix;
            menu->close();
            return;
        }
        menu->setActiveAction(target);
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(menu, &press);
        QCoreApplication::sendEvent(menu, &release);
    });
}

static void rightClick(QWidget *w, const QPointF &pos)
{
    mouse(w, QEvent::MouseButtonPress, pos, Qt::RightButton, Qt::RightButton);
    mouse(w, QEvent::MouseButtonRelease, pos, Qt::RightButton, Qt::NoButton);
}

static ps::TimeBookmark bm(const QString &id, double t, const QString &label)
{
    return ps::TimeBookmark{id, t, label};
}

static void makeCanvas(PlottingCanvas &c)
{
    c.resize(900, 600);
    c.clearChannels();
    QVector<double> t, v;
    for (int i = 0; i <= 100; ++i)
    {
        t.append(i * 0.1);
        v.append(std::sin(i * 0.1));
    }
    c.addGeneratedSeries(QStringLiteral("demo"), t, v);
    c.setTimeRange(0.0, 10.0);
    c.show();
    QApplication::processEvents();
}

static void reset(PlottingCanvas &c)
{
    c.setTimeRange(0.0, 10.0);
    c.requestAutoY();
    c.setTimeMarkers({});
    c.setRubberZoomEnabled(false);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    PlottingCanvas c;
    makeCanvas(c);
    const QRectF plot = c.plotArea();
    check(plot.width() > 100 && plot.height() > 100, "plot area");
    const QPointF center = plot.center();
    const QPointF xAxis(center.x(), c.height() - 3);
    const QPointF yAxis(3, center.y());

    // 滚轮：绘图区默认只缩 X；Ctrl 只缩 Y。
    reset(c);
    double span = c.t1() - c.t0();
    const double y0 = c.yMin();
    const double y1 = c.yMax();
    wheel(&c, center, 120);
    check(c.t1() - c.t0() < span, "wheel zooms X");
    check(qAbs(c.yMin() - y0) < 1e-9 && qAbs(c.yMax() - y1) < 1e-9, "wheel keeps Y");

    reset(c);
    const double t0 = c.t0();
    const double t1 = c.t1();
    wheel(&c, center, 120, Qt::ControlModifier);
    check(qAbs(c.t0() - t0) < 1e-9 && qAbs(c.t1() - t1) < 1e-9, "ctrl+wheel keeps X");
    check(qAbs(c.yMin() - y0) > 1e-9 || qAbs(c.yMax() - y1) > 1e-9, "ctrl+wheel zooms Y");

    // 轴区滚轮：单轴生效。
    reset(c);
    const double axY0 = c.yMin();
    wheel(&c, xAxis, 120);
    check(c.t1() - c.t0() < 10.0, "x-axis wheel zooms X");
    check(qAbs(c.yMin() - axY0) < 1e-9, "x-axis wheel keeps Y");

    reset(c);
    const double ayT0 = c.t0();
    const double ayT1 = c.t1();
    wheel(&c, yAxis, 120);
    check(qAbs(c.t0() - ayT0) < 1e-9 && qAbs(c.t1() - ayT1) < 1e-9, "y-axis wheel keeps X");
    check(qAbs(c.yMin() - y0) > 1e-9 || qAbs(c.yMax() - y1) > 1e-9, "y-axis wheel zooms Y");

    // 中键：绘图区双轴平移，X 轴区只平移 X。
    reset(c);
    double mt0 = c.t0();
    double my0 = c.yMin();
    mouse(&c, QEvent::MouseButtonPress, center, Qt::MiddleButton, Qt::MiddleButton);
    mouse(&c, QEvent::MouseMove, center + QPointF(40, 30), Qt::NoButton, Qt::MiddleButton);
    mouse(&c, QEvent::MouseButtonRelease, center + QPointF(40, 30), Qt::MiddleButton, Qt::NoButton);
    check(c.t0() < mt0 && c.yMin() < my0, "middle drag pans both axes");

    reset(c);
    mt0 = c.t0();
    my0 = c.yMin();
    mouse(&c, QEvent::MouseButtonPress, xAxis, Qt::MiddleButton, Qt::MiddleButton);
    mouse(&c, QEvent::MouseMove, xAxis + QPointF(40, 0), Qt::NoButton, Qt::MiddleButton);
    mouse(&c, QEvent::MouseButtonRelease, xAxis + QPointF(40, 0), Qt::MiddleButton, Qt::NoButton);
    check(c.t0() < mt0, "middle drag on X axis pans X");
    check(qAbs(c.yMin() - my0) < 1e-9, "middle drag on X axis keeps Y");

    // 右键拖动：绘图区框选双轴放大；按下不弹菜单。
    reset(c);
    const double rzT = c.t1() - c.t0();
    const double rzY = c.yMax() - c.yMin();
    const QPointF r0(plot.left() + plot.width() * 0.2, plot.top() + plot.height() * 0.2);
    const QPointF r1(plot.left() + plot.width() * 0.65, plot.top() + plot.height() * 0.65);
    mouse(&c, QEvent::MouseButtonPress, r0, Qt::RightButton, Qt::RightButton);
    mouse(&c, QEvent::MouseMove, r1, Qt::NoButton, Qt::RightButton);
    mouse(&c, QEvent::MouseButtonRelease, r1, Qt::RightButton, Qt::NoButton);
    check(c.t1() - c.t0() < rzT, "right box zooms X");
    check(c.yMax() - c.yMin() < rzY, "right box zooms Y");

    // 右键在 X 轴区框选：只改 X。
    reset(c);
    const double rxY = c.yMax() - c.yMin();
    const QPointF rx0(plot.left() + plot.width() * 0.2, c.height() - 3);
    const QPointF rx1(plot.left() + plot.width() * 0.6, c.height() - 3);
    mouse(&c, QEvent::MouseButtonPress, rx0, Qt::RightButton, Qt::RightButton);
    mouse(&c, QEvent::MouseMove, rx1, Qt::NoButton, Qt::RightButton);
    mouse(&c, QEvent::MouseButtonRelease, rx1, Qt::RightButton, Qt::NoButton);
    check(c.t1() - c.t0() < 10.0, "right box on X axis zooms X");
    check(qAbs((c.yMax() - c.yMin()) - rxY) < 1e-9, "right box on X axis keeps Y");

    // 书签拖动。
    reset(c);
    c.setTimeMarkers({bm(QStringLiteral("b1"), 5.0, QStringLiteral("b"))});
    QPixmap pm(c.size());
    c.render(&pm);
    QString movedId;
    double movedTo = -1.0;
    QObject::connect(&c, &PlottingCanvas::markerMoveRequested, [&](const QString &id, double t) {
        movedId = id;
        movedTo = t;
    });
    const QPointF marker(plot.left() + plot.width() * 0.5 + 8, plot.top() + 10);
    mouse(&c, QEvent::MouseButtonPress, marker, Qt::LeftButton, Qt::LeftButton);
    mouse(&c, QEvent::MouseMove, marker + QPointF(50, 0), Qt::NoButton, Qt::LeftButton);
    mouse(&c, QEvent::MouseButtonRelease, marker + QPointF(50, 0), Qt::LeftButton, Qt::NoButton);
    check(movedId == QStringLiteral("b1") && movedTo > 5.0, "bookmark drag emits move");

    // 书签选中后 Delete。
    reset(c);
    c.setTimeMarkers({bm(QStringLiteral("b1"), 5.0, QStringLiteral("b"))});
    c.render(&pm);
    QString removed;
    QObject::connect(&c, &PlottingCanvas::markerRemoveRequested, [&](const QString &id) { removed = id; });
    mouse(&c, QEvent::MouseButtonPress, marker, Qt::LeftButton, Qt::LeftButton);
    mouse(&c, QEvent::MouseButtonRelease, marker, Qt::LeftButton, Qt::NoButton);
    key(&c, Qt::Key_Delete);
    check(removed == QStringLiteral("b1"), "delete selected bookmark");

    // 同一时间两个书签：选中/删除仍按 id，不按时间碰撞。
    reset(c);
    c.setTimeMarkers({bm(QStringLiteral("a1"), 5.0, QStringLiteral("a")),
                      bm(QStringLiteral("b1"), 5.0, QStringLiteral("b"))});
    c.render(&pm);
    removed.clear();
    mouse(&c, QEvent::MouseButtonPress, marker, Qt::LeftButton, Qt::LeftButton);
    mouse(&c, QEvent::MouseButtonRelease, marker, Qt::LeftButton, Qt::NoButton);
    key(&c, Qt::Key_Delete);
    check(removed == QStringLiteral("a1"), "same-time bookmarks delete by id");

    // 外部刷新只改 t：选中仍跟随 id。
    reset(c);
    c.setTimeMarkers({bm(QStringLiteral("b1"), 5.0, QStringLiteral("b"))});
    c.render(&pm);
    mouse(&c, QEvent::MouseButtonPress, marker, Qt::LeftButton, Qt::LeftButton);
    mouse(&c, QEvent::MouseButtonRelease, marker, Qt::LeftButton, Qt::NoButton);
    c.setTimeMarkers({bm(QStringLiteral("b1"), 7.0, QStringLiteral("b"))});
    c.render(&pm);
    removed.clear();
    key(&c, Qt::Key_Delete);
    check(removed == QStringLiteral("b1"), "selection survives time change");

    // Ctrl+左键直接生成 A/B，Esc 清除。
    reset(c);
    mouse(&c, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    mouse(&c, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    check(c.measurementEnabled(), "ctrl+click creates A/B");
    check(c.measurementA() < c.measurementB(), "A/B order");
    key(&c, Qt::Key_Escape);
    check(!c.measurementEnabled(), "esc clears A/B");

    // A/B 吸附采样点：稀疏样本也要落在真实时间戳。
    c.clearChannels();
    QVector<double> sparseT{0, 2, 4, 6, 8, 10};
    QVector<double> sparseV{0, 1, 0, 1, 0, 1};
    c.addGeneratedSeries(QStringLiteral("sparse"), sparseT, sparseV);
    c.setTimeRange(0.0, 10.0);
    c.render(&pm);
    mouse(&c, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    mouse(&c, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    check(qAbs(c.measurementA() - 4.0) < 1e-6, "A snaps to sample");
    check(qAbs(c.measurementB() - 6.0) < 1e-6, "B snaps to sample");
    key(&c, Qt::Key_Escape);
    makeCanvas(c);

    // 同页光标联动：悬停发出时间，被动光标可设置。
    reset(c);
    double hoverT = -1.0;
    bool hoverVisible = false;
    QObject::connect(&c, &PlottingCanvas::hoverTimeChanged, [&](double t, bool visible) {
        hoverT = t;
        hoverVisible = visible;
    });
    mouse(&c, QEvent::MouseMove, center, Qt::NoButton, Qt::NoButton);
    check(hoverVisible && qAbs(hoverT - 5.0) < 0.2, "hover emits linked time");
    c.setLinkedCursor(3.0, true);
    check(c.linkedCursorVisible() && qAbs(c.linkedCursorTime() - 3.0) < 1e-9, "linked cursor set");
    c.setLinkedCursor(3.0, false);
    check(!c.linkedCursorVisible(), "linked cursor cleared");

    PlottingCanvas other;
    makeCanvas(other);
    QObject::connect(&c, &PlottingCanvas::hoverTimeChanged,
                     &other, &PlottingCanvas::setLinkedCursor);
    mouse(&c, QEvent::MouseMove, center, Qt::NoButton, Qt::NoButton);
    check(other.linkedCursorVisible() && qAbs(other.linkedCursorTime() - 5.0) < 0.2,
          "sibling canvas follows hover");

    // P 固定读数；P/Esc 解除。
    reset(c);
    mouse(&c, QEvent::MouseMove, center, Qt::NoButton, Qt::NoButton);
    key(&c, Qt::Key_P);
    check(c.readoutPinned() && qAbs(c.pinnedReadoutTime() - 5.0) < 0.2, "P pins readout");
    key(&c, Qt::Key_P);
    check(!c.readoutPinned(), "P unpins readout");
    key(&c, Qt::Key_P);
    check(c.readoutPinned(), "P re-pins readout");
    key(&c, Qt::Key_Escape);
    check(!c.readoutPinned(), "esc unpins readout");

    // 右键菜单固定/取消固定读数。
    reset(c);
    chooseMenuAction(QStringLiteral("固定读数"));
    rightClick(&c, center);
    check(menuMiss.isEmpty() && c.readoutPinned(), "menu pins readout");
    chooseMenuAction(QStringLiteral("取消固定读数"));
    rightClick(&c, center);
    check(menuMiss.isEmpty() && !c.readoutPinned(), "menu unpins readout");

    // 空格+左键平移。
    reset(c);
    mt0 = c.t0();
    key(&c, Qt::Key_Space);
    mouse(&c, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    mouse(&c, QEvent::MouseMove, center + QPointF(60, 0), Qt::NoButton, Qt::LeftButton);
    mouse(&c, QEvent::MouseButtonRelease, center + QPointF(60, 0), Qt::LeftButton, Qt::NoButton);
    key(&c, Qt::Key_Space, Qt::NoModifier, QEvent::KeyRelease);
    check(c.t0() < mt0, "space+left pans");

    // Ctrl+0 重置；[ / ] 跳转书签。
    reset(c);
    wheel(&c, center, 240);
    key(&c, Qt::Key_0, Qt::ControlModifier);
    check(qAbs((c.t1() - c.t0()) - 10.0) < 1e-4, "ctrl+0 resets view");
    c.setTimeMarkers({bm(QStringLiteral("a1"), 2.0, QStringLiteral("a")),
                         bm(QStringLiteral("b1"), 8.0, QStringLiteral("b"))});
    key(&c, Qt::Key_BracketRight);
    check(qAbs((c.t0() + c.t1()) * 0.5 - 8.0) < 1e-4, "bracket right jumps bookmark");
    key(&c, Qt::Key_BracketLeft);
    check(qAbs((c.t0() + c.t1()) * 0.5 - 2.0) < 1e-4, "bracket left jumps bookmark");

    // 右键单击空白：菜单项“添加时间书签”真正发出请求。
    reset(c);
    double requested = -1.0;
    QObject::connect(&c, &PlottingCanvas::markerRequested, [&](double t) { requested = t; });
    chooseMenuAction(QStringLiteral("在 "));
    rightClick(&c, center);
    check(menuMiss.isEmpty(), "blank menu opened");
    check(qAbs(requested - 5.0) < 0.2, "blank menu adds bookmark");

    // 右键单击书签：菜单删除书签。
    reset(c);
    c.setTimeMarkers({bm(QStringLiteral("b1"), 5.0, QStringLiteral("b"))});
    c.render(&pm);
    removed.clear();
    chooseMenuAction(QStringLiteral("删除书签"));
    rightClick(&c, marker);
    check(menuMiss.isEmpty(), "bookmark menu opened");
    check(removed == QStringLiteral("b1"), "bookmark menu deletes");

    // 右键单击曲线：菜单隐藏曲线。
    reset(c);
    const double sy = plot.top() + plot.height()
                     * (c.yMax() - std::sin(5.0)) / (c.yMax() - c.yMin());
    const QPointF onCurve(plot.left() + plot.width() * 0.5, sy);
    chooseMenuAction(QStringLiteral("隐藏曲线"));
    rightClick(&c, onCurve);
    check(menuMiss.isEmpty(), "series menu opened");
    check(!c.seriesState(0).value(QStringLiteral("visible")).toBool(), "series menu hides");

    // 右键单击 X 轴区：菜单重置 X 轴。
    reset(c);
    wheel(&c, center, 240);
    chooseMenuAction(QStringLiteral("重置 X 轴"));
    rightClick(&c, xAxis);
    check(menuMiss.isEmpty(), "axis menu opened");
    check(qAbs((c.t1() - c.t0()) - 10.0) < 1e-4, "axis menu resets X");

    // 右键单击 A/B 线：菜单清除测量。
    reset(c);
    mouse(&c, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    mouse(&c, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    check(c.measurementEnabled(), "A/B before menu");
    const double abX = plot.left() + plot.width() * (c.measurementA() - c.t0()) / (c.t1() - c.t0());
    chooseMenuAction(QStringLiteral("清除 A/B 测量"));
    rightClick(&c, QPointF(abX, center.y()));
    check(menuMiss.isEmpty(), "A/B menu opened");
    check(!c.measurementEnabled(), "A/B menu clears");

    if (failures)
        std::fprintf(stderr, "%d checks failed\n", failures);
    return failures ? 1 : 0;
}
