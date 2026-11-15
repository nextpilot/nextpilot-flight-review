#include "PlotWorkspace.h"

#include "CanvasWidget.h"

#include <QSplitter>
#include <QTabBar>

#include <utility>

namespace
{
void collectCanvases(QWidget *root, QVector<CanvasWidget *> &out)
{
    if (!root)
        return;
    if (auto *c = qobject_cast<CanvasWidget *>(root))
        out.append(c);
    if (auto *sp = qobject_cast<QSplitter *>(root))
    {
        for (int i = 0; i < sp->count(); ++i)
            collectCanvases(sp->widget(i), out);
        return;
    }
    for (QObject *child : root->children())
        if (auto *w = qobject_cast<QWidget *>(child))
            collectCanvases(w, out);
}
} // namespace

namespace ps::plot
{

PlotWorkspace::PlotWorkspace(CanvasFactory factory, QWidget *parent)
    : QTabWidget(parent), m_factory(std::move(factory))
{
    setDocumentMode(true);
    setTabsClosable(true);
    tabBar()->setExpanding(false);
    tabBar()->setObjectName(QStringLiteral("pageTabs"));
}

CanvasWidget *PlotWorkspace::makeCanvas(const QString &title, QWidget *parent)
{
    auto *c = m_factory(parent);
    c->setTitle(title);
    attachCanvas(c);
    return c;
}

void PlotWorkspace::attachCanvas(CanvasWidget *c)
{
    connect(c, &CanvasWidget::activated, this, [this, c](const QString &) {
        emit canvasActivated(c);
    });
    connect(c, &CanvasWidget::timeRangeChanged, this, [this, c](double t0, double t1) {
        if (c->plotMode() != CanvasWidget::TimeSeries)
            return;
        for (CanvasWidget *o : allCanvases())
            if (o != c && o->plotMode() == CanvasWidget::TimeSeries)
                o->setTimeRange(t0, t1);
    });
    connect(c, &CanvasWidget::addRequested, this, [this, c] { emit canvasAddRequested(c); });
    connect(c, &CanvasWidget::splitRequested, this,
            [this, c](int orientation) { emit canvasSplitRequested(c, orientation); });
    connect(c, &CanvasWidget::addChannelRequested, this,
            [this, c] { emit addChannelRequested(c); });
    connect(c, &CanvasWidget::maximizeRequested, this,
            [this, c] { emit canvasMaximizeRequested(c); });
    connect(c, &CanvasWidget::markerRequested, this, [this](double t) { emit markerRequested(t); });
    connect(c, &CanvasWidget::markerRemoveRequested, this,
            [this](double t) { emit markerRemoveRequested(t); });
    connect(c, &CanvasWidget::removeRequested, this, [this, c] { emit canvasRemoveRequested(c); });
    c->setTimeMarkers(m_markers);
}

void PlotWorkspace::createDefaultPages()
{
    // 页 1：三 plot 竖排（QSplitter 树，D15）
    auto *page1 = new QSplitter(Qt::Vertical, this);
    page1->setHandleWidth(6);
    page1->addWidget(makeCanvas(QStringLiteral("姿态角 · plot"), page1));
    auto *vib = makeCanvas(QStringLiteral("振动 · plot"), page1);
    vib->clearChannels();
    page1->addWidget(vib);
    auto *bus = makeCanvas(QStringLiteral("总线 · plot"), page1);
    bus->clearChannels();
    page1->addWidget(bus);
    page1->setSizes({300, 220, 220});
    addTab(page1, QStringLiteral("姿态与振动"));

    // 页 2：单 plot
    auto *busPage = makeCanvas(QStringLiteral("车辆总线 · plot"), this);
    busPage->clearChannels();
    addTab(busPage, QStringLiteral("车辆总线"));

    // 页 3：仪表大屏，三块独立画布分别走地平仪 / 罗盘 / 表盘模式
    auto *dash = new QSplitter(Qt::Horizontal, this);
    dash->setHandleWidth(6);
    auto *horizon = makeCanvas(QStringLiteral("地平仪 · attitude"), dash);
    horizon->setPlotMode(CanvasWidget::Horizon, QStringLiteral("horizon"));
    auto *compass = makeCanvas(QStringLiteral("罗盘 · heading"), dash);
    compass->setPlotMode(CanvasWidget::Compass, QStringLiteral("compass"));
    auto *gauge = makeCanvas(QStringLiteral("表盘 · value"), dash);
    gauge->setPlotMode(CanvasWidget::Gauge, QStringLiteral("gauge"));
    dash->addWidget(horizon);
    dash->addWidget(compass);
    dash->addWidget(gauge);
    dash->setSizes({420, 320, 320});
    addTab(dash, QStringLiteral("仪表大屏"));

    appendPlusTab();
}

void PlotWorkspace::appendPlusTab()
{
    const int idx = count();
    addTab(new QWidget(this), QStringLiteral("＋"));
    tabBar()->setTabToolTip(idx, QStringLiteral("新建页"));
    tabBar()->setTabButton(idx, QTabBar::RightSide, nullptr);
}

void PlotWorkspace::setTimeMarkers(const QVector<QPair<double, QString>> &markers)
{
    m_markers = markers;
    for (int i = 0; i < count(); ++i)
    {
        QVector<CanvasWidget *> canvases;
        collectCanvases(widget(i), canvases);
        for (CanvasWidget *c : canvases)
            c->setTimeMarkers(m_markers);
    }
}

QVector<CanvasWidget *> PlotWorkspace::pageCanvases() const
{
    QVector<CanvasWidget *> out;
    collectCanvases(currentWidget(), out);
    return out;
}

QVector<CanvasWidget *> PlotWorkspace::allCanvases() const
{
    QVector<CanvasWidget *> out;
    for (int i = 0; i < count(); ++i)
        collectCanvases(widget(i), out);
    return out;
}

CanvasWidget *PlotWorkspace::currentCanvas() const
{
    return pageCanvases().value(0, nullptr);
}

} // namespace ps::plot
