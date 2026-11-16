#include "PlottingTabs.h"

#include "PlottingCanvas.h"
#include "PlottingLayout.h"

#include <QInputDialog>
#include <QLineEdit>
#include <QTabBar>

#include <utility>


namespace ps::plotting
{

PlottingTabs::PlottingTabs(CanvasFactory factory, QWidget *parent)
    : QTabWidget(parent), m_factory(std::move(factory))
{
    setDocumentMode(true);
    setTabsClosable(true);
    tabBar()->setExpanding(false);
    tabBar()->setObjectName(QStringLiteral("pageTabs"));
    tabBar()->setUsesScrollButtons(true);
    tabBar()->setElideMode(Qt::ElideRight);
    // 双击页签改名：最后一个「＋」是新建按钮，不参与
    connect(tabBar(), &QTabBar::tabBarDoubleClicked, this, [this](int idx) {
        if (idx < 0 || idx >= count() - 1)
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("重命名页签"),
                                                   QStringLiteral("页名"), QLineEdit::Normal,
                                                   tabText(idx), &ok);
        if (!ok)
            return;
        const QString trimmed = name.trimmed();
        if (trimmed.isEmpty() || trimmed == tabText(idx))
            return;
        setTabText(idx, trimmed);
        emit pageRenamed(idx, trimmed);
    });
}

PlottingCanvas *PlottingTabs::makeCanvas(const QString &title, QWidget *parent)
{
    auto *c = m_factory(parent);
    c->setTitle(title);
    attachCanvas(c);
    return c;
}

void PlottingTabs::attachCanvas(PlottingCanvas *c)
{
    connect(c, &PlottingCanvas::activated, this, [this, c](const QString &) {
        emit canvasActivated(c);
    });
    connect(c, &PlottingCanvas::timeRangeChanged, this, [this, c](double t0, double t1) {
        if (c->plotMode() != PlottingCanvas::TimeSeries)
            return;
        for (PlottingCanvas *o : allCanvases())
            if (o != c && o->plotMode() == PlottingCanvas::TimeSeries)
                o->setTimeRange(t0, t1);
    });
    connect(c, &PlottingCanvas::addRequested, this, [this, c] { emit canvasAddRequested(c); });
    connect(c, &PlottingCanvas::splitRequested, this,
            [this, c](int orientation) { emit canvasSplitRequested(c, orientation); });
    connect(c, &PlottingCanvas::addChannelRequested, this,
            [this, c] { emit addChannelRequested(c); });
    connect(c, &PlottingCanvas::maximizeRequested, this,
            [this, c] { emit canvasMaximizeRequested(c); });
    connect(c, &PlottingCanvas::markerRequested, this, [this](double t) { emit markerRequested(t); });
    connect(c, &PlottingCanvas::markerRemoveRequested, this,
            [this](double t) { emit markerRemoveRequested(t); });
    connect(c, &PlottingCanvas::removeRequested, this, [this, c] { emit canvasRemoveRequested(c); });
    c->setTimeMarkers(m_markers);
}

void PlottingTabs::createDefaultPages()
{
    // 页 1：三 plot 竖排（PlottingLayout，D15）
    auto *page1 = new PlottingPage(this);
    auto *layout1 = page1->layout();
    layout1->addCanvas(makeCanvas(QStringLiteral("姿态角 · plot"), layout1));
    auto *vib = makeCanvas(QStringLiteral("振动 · plot"), layout1);
    vib->clearChannels();
    layout1->addCanvas(vib);
    auto *bus = makeCanvas(QStringLiteral("总线 · plot"), layout1);
    bus->clearChannels();
    layout1->addCanvas(bus);
    layout1->setSizes({300, 220, 220});
    addTab(page1, QStringLiteral("姿态与振动"));

    // 页 2：单 plot
    auto *busPage = new PlottingPage(this);
    auto *busLayout = busPage->layout();
    auto *busCanvas = makeCanvas(QStringLiteral("车辆总线 · plot"), busLayout);
    busCanvas->clearChannels();
    busLayout->addCanvas(busCanvas);
    addTab(busPage, QStringLiteral("车辆总线"));

    // 页 3：仪表大屏，三块独立画布分别走地平仪 / 罗盘 / 表盘模式
    auto *dash = new PlottingLayout(Qt::Horizontal, this);
    auto *horizon = makeCanvas(QStringLiteral("地平仪 · attitude"), dash);
    horizon->setPlotMode(PlottingCanvas::Horizon, QStringLiteral("horizon"));
    auto *compass = makeCanvas(QStringLiteral("罗盘 · heading"), dash);
    compass->setPlotMode(PlottingCanvas::Compass, QStringLiteral("compass"));
    auto *gauge = makeCanvas(QStringLiteral("表盘 · value"), dash);
    gauge->setPlotMode(PlottingCanvas::Gauge, QStringLiteral("gauge"));
    dash->addCanvas(horizon);
    dash->addCanvas(compass);
    dash->addCanvas(gauge);
    dash->setSizes({420, 320, 320});
    addTab(dash, QStringLiteral("仪表大屏"));

    appendPlusTab();
}

void PlottingTabs::appendPlusTab()
{
    const int idx = count();
    addTab(new QWidget(this), QStringLiteral("＋"));
    tabBar()->setTabToolTip(idx, QStringLiteral("新建页"));
    tabBar()->setTabButton(idx, QTabBar::RightSide, nullptr);
}

void PlottingTabs::setTimeMarkers(const QVector<QPair<double, QString>> &markers)
{
    m_markers = markers;
    for (int i = 0; i < count(); ++i)
    {
        QVector<PlottingCanvas *> canvases = PlottingLayout::collectCanvases(widget(i));
        for (PlottingCanvas *c : canvases)
            c->setTimeMarkers(m_markers);
    }
}

QVector<PlottingCanvas *> PlottingTabs::pageCanvases() const
{
    return PlottingLayout::collectCanvases(currentWidget());
}

QVector<PlottingCanvas *> PlottingTabs::allCanvases() const
{
    QVector<PlottingCanvas *> out;
    for (int i = 0; i < count(); ++i)
        out += PlottingLayout::collectCanvases(widget(i));
    return out;
}

PlottingCanvas *PlottingTabs::currentCanvas() const
{
    return pageCanvases().value(0, nullptr);
}

} // namespace ps::plotting
