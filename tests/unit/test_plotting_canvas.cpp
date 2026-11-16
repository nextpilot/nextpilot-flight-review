#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QWheelEvent>
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
    c.setTimeMarkers({{5.0, QStringLiteral("b")}});
    QPixmap pm(c.size());
    c.render(&pm);
    double movedFrom = -1.0;
    double movedTo = -1.0;
    QObject::connect(&c, &PlottingCanvas::markerMoveRequested, [&](double f, double t) {
        movedFrom = f;
        movedTo = t;
    });
    const QPointF marker(plot.left() + plot.width() * 0.5 + 8, plot.top() + 10);
    mouse(&c, QEvent::MouseButtonPress, marker, Qt::LeftButton, Qt::LeftButton);
    mouse(&c, QEvent::MouseMove, marker + QPointF(50, 0), Qt::NoButton, Qt::LeftButton);
    mouse(&c, QEvent::MouseButtonRelease, marker + QPointF(50, 0), Qt::LeftButton, Qt::NoButton);
    check(movedFrom == 5.0 && movedTo > 5.0, "bookmark drag emits move");

    // 书签选中后 Delete。
    reset(c);
    c.setTimeMarkers({{5.0, QStringLiteral("b")}});
    c.render(&pm);
    double removed = -1.0;
    QObject::connect(&c, &PlottingCanvas::markerRemoveRequested, [&](double t) { removed = t; });
    mouse(&c, QEvent::MouseButtonPress, marker, Qt::LeftButton, Qt::LeftButton);
    mouse(&c, QEvent::MouseButtonRelease, marker, Qt::LeftButton, Qt::NoButton);
    key(&c, Qt::Key_Delete);
    check(removed == 5.0, "delete selected bookmark");

    // Ctrl+左键直接生成 A/B，Esc 清除。
    reset(c);
    mouse(&c, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    mouse(&c, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    check(c.measurementEnabled(), "ctrl+click creates A/B");
    check(c.measurementA() < c.measurementB(), "A/B order");
    key(&c, Qt::Key_Escape);
    check(!c.measurementEnabled(), "esc clears A/B");

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
    c.setTimeMarkers({{2.0, QStringLiteral("a")}, {8.0, QStringLiteral("b")}});
    key(&c, Qt::Key_BracketRight);
    check(qAbs((c.t0() + c.t1()) * 0.5 - 8.0) < 1e-4, "bracket right jumps bookmark");
    key(&c, Qt::Key_BracketLeft);
    check(qAbs((c.t0() + c.t1()) * 0.5 - 2.0) < 1e-4, "bracket left jumps bookmark");

    if (failures)
        std::fprintf(stderr, "%d checks failed\n", failures);
    return failures ? 1 : 0;
}
