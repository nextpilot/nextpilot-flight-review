#include "plotting/PlottingModule.h"
#include "MainWindow.h"
#include "PlottingSession.h"
#include "PlottingCanvas.h"
#include "PlottingRibbon.h"
#include "PlottingSettings.h"

#include <QAction>
#include <QIcon>
#include <QTableWidget>
#include <QWidget>
#include "SARibbonPanel.h"

namespace ps::components
{
namespace
{
struct PlotRegistrar
{
    PlotRegistrar()
    {
        ExtensionRegistry::instance().addExtension(
            ExtensionDescriptor{QStringLiteral("plot.core"), QStringLiteral("绘图"),
                                QStringLiteral("1.0"), ExtensionType::Builtin},
            [] { return std::make_unique<PlottingComponent>(); });
    }
};
static PlotRegistrar g_plottingRegistrar;
} // namespace

ComponentManifest PlottingComponent::manifest() const
{
    return {QStringLiteral("plot.core"), QStringLiteral("绘图"), QStringLiteral("1.0"),
            {QStringLiteral("plot"), QStringLiteral("dock")}};
}

void PlottingComponent::contribute(Registry &registry, HostContext &)
{
    registerPlottingContributions(registry);
    registry.addStatusChip(StatusChipContribution{QStringLiteral("lod"), QStringLiteral("LOD: Rust"), 1});
    registry.addWindowHook([](QWidget *w) {
        auto *mw = qobject_cast<MainWindow *>(w);
        if (!mw)
            return;
        ps::plotting::createPages(mw);
    });
}

void registerPlottingContributions(Registry &registry)
{
    registry.addSettings(SettingsContribution{
        QStringLiteral("settings.plotting"), QStringLiteral("绘图"), QStringLiteral(":/icons/prop.svg"), 10,
        [](QWidget *host) -> QWidget * {
            auto *mw = qobject_cast<MainWindow *>(host);
            return mw ? new PlottingSettingsPage(mw) : nullptr;
        },
        [](QWidget *page) { static_cast<PlottingSettingsPage *>(page)->apply(); }});
    registry.addDock(DockContribution{QStringLiteral("dock.prop"), QStringLiteral("属性面板"),
                                      Qt::RightDockWidgetArea, true, 1, nullptr,
                                      [](QWidget *p) {
                                          auto *mw = qobject_cast<MainWindow *>(p);
                                          return mw ? plottingSession(mw)->createPropPanel(false) : nullptr;
                                      }});
    registry.addDock(DockContribution{QStringLiteral("dock.tree"), QStringLiteral("对象树"),
                                      Qt::RightDockWidgetArea, true, 2,
                                      QStringLiteral(":/icons/prop.svg"),
                                      [](QWidget *p) {
                                          auto *mw = qobject_cast<MainWindow *>(p);
                                          return mw ? plottingSession(mw)->createPropPanel(true) : nullptr;
                                      }});
    registry.addRibbon(RibbonContribution{QStringLiteral("绘图"), {}, {},
        {{QStringLiteral("页面"),
             {{QStringLiteral("page.new"), QStringLiteral("新建页"), {}, true, QStringLiteral("newpage")},
              {QStringLiteral("page.addCanvas"), QStringLiteral("添加画布"), {}, true, QStringLiteral("plus")},
              {QStringLiteral("page.splitHorizontal"), QStringLiteral("向右拆分"), {}, true, QStringLiteral("split_h")},
              {QStringLiteral("page.splitVertical"), QStringLiteral("向下拆分"), {}, true, QStringLiteral("split_v")}}},
         {QStringLiteral("画布布局"), {}, ps::plotting::buildLayoutPicker},
         {QStringLiteral("显示"), {},
             [](QWidget *p) {
                 auto *panel = static_cast<SARibbonPanel *>(p);
                 auto *mw = qobject_cast<MainWindow *>(panel->window());
                 auto icon = [](const QString &name) {
                     return QIcon(QStringLiteral(":/icons/%1.svg").arg(name));
                 };
                 auto add = [&](const QString &text, const QString &iconName, int flag) {
                     auto *a = new QAction(icon(iconName), text, panel);
                     a->setCheckable(true);
                     QObject::connect(a, &QAction::toggled, mw, [mw, flag](bool on) { ps::plotting::setCanvasViewFlag(mw, flag, on); });
                     panel->addSmallAction(a);
                 };
                 add(QStringLiteral("网格"), QStringLiteral("grid"), 0);
                 add(QStringLiteral("边框"), QStringLiteral("box"), 1);
                 add(QStringLiteral("图例"), QStringLiteral("legend"), 2);
                 add(QStringLiteral("框选"), QStringLiteral("cursor"), 3);
                 panel->addSeparator();
                 auto *zin = new QAction(icon(QStringLiteral("zoom_in")), QStringLiteral("放大"), panel);
                 QObject::connect(zin, &QAction::triggered, mw, [mw] { ps::plotting::canvasZoom(mw, 0.8); });
                 panel->addSmallAction(zin);
                 auto *zout = new QAction(icon(QStringLiteral("zoom_out")), QStringLiteral("缩小"), panel);
                 QObject::connect(zout, &QAction::triggered, mw, [mw] { ps::plotting::canvasZoom(mw, 1.25); });
                 panel->addSmallAction(zout);
                 auto *autoY = new QAction(icon(QStringLiteral("autoy")), QStringLiteral("自适应"), panel);
                 QObject::connect(autoY, &QAction::triggered, mw, [mw] { ps::plotting::canvasAutoY(mw); });
                 panel->addSmallAction(autoY);
             }},
         {QStringLiteral("图表类型"), {}, ps::plotting::buildPlotGallery}},
        2});
}

} // namespace ps::components

namespace ps::plotting
{
void createPages(MainWindow *mw) { plottingSession(mw)->createPages(); }
void refreshPropTree(MainWindow *mw, PlottingCanvas *c) { plottingSession(mw)->refreshPropTree(c); }
PlottingCanvas *currentCanvas(MainWindow *mw) { return plottingSession(mw)->currentCanvas(); }
QVector<PlottingCanvas *> pageCanvases(MainWindow *mw) { return plottingSession(mw)->pageCanvases(); }
void newPage(MainWindow *mw) { plottingSession(mw)->newPage(); }
void setCanvasViewFlag(MainWindow *mw, int f, bool on) { plottingSession(mw)->setCanvasViewFlag(f, on); }
void canvasZoom(MainWindow *mw, double f) { plottingSession(mw)->canvasZoom(f); }
void canvasAutoY(MainWindow *mw) { plottingSession(mw)->canvasAutoY(); }
void reloadCanvasSettings(MainWindow *mw) { plottingSession(mw)->reloadCanvasSettings(); }
void applyPlayhead(MainWindow *mw, double t) { plottingSession(mw)->applyPlayhead(t); }
QJsonObject pageState(MainWindow *mw, QWidget *p) { return plottingSession(mw)->pageState(p); }
QWidget *buildPage(MainWindow *mw, const QJsonObject &o) { return plottingSession(mw)->buildPage(o); }
void applyTimeMarkers(MainWindow *mw) { plottingSession(mw)->applyTimeMarkers(); }
void removeSourceFromCanvases(MainWindow *mw, const QString &p) { plottingSession(mw)->removeSourceFromCanvases(p); }
void addChannels(MainWindow *mw, const QVector<const PsChannelInfo *> &c) { plottingSession(mw)->addChannels(c); }
void setPropRows(MainWindow *mw, const QString &o, const QVector<QPair<QString, QString>> &r) { plottingSession(mw)->setPropRows(o, r); }
QTableWidget *propTable(MainWindow *mw) { return plottingSession(mw)->propTable(); }
QTreeWidget *propTree(MainWindow *mw) { return plottingSession(mw)->propTree(); }
void bindPlotPanels(MainWindow *mw, QLabel *s, QToolButton *e, QTableWidget *t, QTreeWidget *tr) { plottingSession(mw)->bindPlotPanels(s, e, t, tr); }
}

namespace ps::bridge
{
void newPage(MainWindow *mw) { ps::plotting::newPage(mw); }
QVector<PlottingCanvas *> pageCanvases(MainWindow *mw) { return ps::plotting::pageCanvases(mw); }
PlottingCanvas *currentCanvas(MainWindow *mw) { return ps::plotting::currentCanvas(mw); }
QJsonObject pageState(MainWindow *mw, QWidget *page) { return ps::plotting::pageState(mw, page); }
QWidget *buildPage(MainWindow *mw, const QJsonObject &o) { return ps::plotting::buildPage(mw, o); }
void refreshPropTree(MainWindow *mw, PlottingCanvas *active) { ps::plotting::refreshPropTree(mw, active); }
void applyTimeMarkers(MainWindow *mw) { ps::plotting::applyTimeMarkers(mw); }
void setPropRows(MainWindow *mw, const QString &object,
                 const QVector<QPair<QString, QString>> &rows) { ps::plotting::setPropRows(mw, object, rows); }
QTableWidget *propTable(MainWindow *mw) { return ps::plotting::propTable(mw); }
QTreeWidget *propTree(MainWindow *mw) { return ps::plotting::propTree(mw); }
QVector<PlottingCanvas *> allCanvases(MainWindow *mw)
{
    QVector<PlottingCanvas *> out;
    if (!mw || !mw->pages())
        return out;
    for (int i = 0; i < mw->pages()->count(); ++i)
        if (QWidget *page = mw->pages()->widget(i))
            for (PlottingCanvas *c : page->findChildren<PlottingCanvas *>())
                out.append(c);
    return out;
}
void canvasAddChannel(PlottingCanvas *c, const PsChannelInfo *ch) { if (c) c->addChannel(ch); }
void canvasAddChannelEx(PlottingCanvas *c, const PsChannelInfo *ch, int srcId,
                        const QString &path, const QString &alias, double offsetSec)
{ if (c) c->addChannelEx(ch, srcId, path, alias, offsetSec); }
bool canvasReplaceChannel(PlottingCanvas *c, const QString &oldName, const PsChannelInfo *ch)
{ return c && c->replaceChannel(oldName, ch); }
void canvasSetChannels(PlottingCanvas *c, const QVector<const PsChannelInfo *> &channels)
{ if (c) c->setChannels(channels); }
void canvasSetTitle(PlottingCanvas *c, const QString &title) { if (c) c->setTitle(title); }
void canvasSetInputSource(PlottingCanvas *c, int id, const QString &path,
                          const QString &alias, double offsetSec)
{ if (c) c->setInputSource(id, path, alias, offsetSec); }
void canvasSetSourceOffset(PlottingCanvas *c, const QString &path, double offsetSec, bool refit)
{ if (c) c->setSourceOffset(path, offsetSec, refit); }
void canvasRemoveChannel(PlottingCanvas *c, const QString &name) { if (c) c->removeChannel(name); }
void canvasRemoveChannelsFromSource(PlottingCanvas *c, const QString &path)
{ if (c) c->removeChannelsFromSource(path); }
void canvasRenameSource(PlottingCanvas *c, const QString &path, const QString &alias)
{ if (c) c->renameSource(path, alias); }
void canvasFollowLatest(PlottingCanvas *c) { if (c) c->followLatest(); }
void canvasNotifyActivated(PlottingCanvas *c) { if (c) c->notifyActivated(); }
void canvasUpdate(PlottingCanvas *c) { if (c) c->update(); }
bool canvasHasData(PlottingCanvas *c) { return c && c->hasData(); }
void canvasSetFollowLive(PlottingCanvas *c, bool on, double windowSec)
{ if (c) c->setFollowLive(on, windowSec); }
} // namespace ps::bridge
