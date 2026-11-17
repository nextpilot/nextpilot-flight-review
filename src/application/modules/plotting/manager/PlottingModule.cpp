#include "plotting/PlottingModule.h"
#include "ModuleContext.h"
#include "PlottingSession.h"
#include "PlottingCanvas.h"
#include "PlottingRibbon.h"
#include "PlottingSettings.h"

#include <QAction>
#include <QIcon>
#include <QTableWidget>
#include <QJsonObject>
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
    registry.addModuleHook([](ps::runtime::ModuleContext &ctx) {
        ps::plotting::createPages(&ctx);
    });
}

void registerPlottingContributions(Registry &registry)
{
    registry.addSettings(SettingsContribution{
        QStringLiteral("settings.plotting"), QStringLiteral("绘图"), QStringLiteral(":/icons/prop.svg"), 10,
        [](QWidget *host) -> QWidget * {
            auto *ctx = ps::runtime::ModuleContext::fromWidget(host);
            return ctx ? new PlottingSettingsPage(*ctx) : nullptr;
        },
        [](QWidget *page) { static_cast<PlottingSettingsPage *>(page)->apply(); }});
    registry.addRibbon(RibbonContribution{QStringLiteral("绘图"), {}, {},
        {{QStringLiteral("页面"), {}, ps::plotting::buildPagePanel},
         {QStringLiteral("布局"), {}, ps::plotting::buildLayoutPicker},
         {QStringLiteral("显示"), {}, ps::plotting::buildDisplayPanel},
         {QStringLiteral("画布操作"), {}, ps::plotting::buildPanelsToolbar},
         {QStringLiteral("图表类型"), {}, ps::plotting::buildPlotGallery}},
        2});
}

} // namespace ps::components

namespace ps::plotting
{
void createPages(ps::runtime::ModuleContext *ctx) { plottingSession(*ctx)->createPages(); }
void refreshPropTree(ps::runtime::ModuleContext *ctx, PlottingCanvas *c) { plottingSession(*ctx)->refreshPropTree(c); }
PlottingCanvas *currentCanvas(ps::runtime::ModuleContext *ctx) { return plottingSession(*ctx)->currentCanvas(); }
QVector<PlottingCanvas *> pageCanvases(ps::runtime::ModuleContext *ctx) { return plottingSession(*ctx)->pageCanvases(); }
void newPage(ps::runtime::ModuleContext *ctx) { plottingSession(*ctx)->newPage(); }
void setCanvasViewFlag(ps::runtime::ModuleContext *ctx, int f, bool on) { plottingSession(*ctx)->setCanvasViewFlag(f, on); }
void canvasZoom(ps::runtime::ModuleContext *ctx, double f) { plottingSession(*ctx)->canvasZoom(f); }
void canvasTool(ps::runtime::ModuleContext *ctx, int tool) { plottingSession(*ctx)->setCanvasTool(tool); }
void canvasAutoY(ps::runtime::ModuleContext *ctx) { plottingSession(*ctx)->canvasAutoY(); }
void reloadCanvasSettings(ps::runtime::ModuleContext *ctx) { plottingSession(*ctx)->reloadCanvasSettings(); }
void applyPlayhead(ps::runtime::ModuleContext *ctx, double t) { plottingSession(*ctx)->applyPlayhead(t); }
QJsonObject pageState(ps::runtime::ModuleContext *ctx, QWidget *p) { return plottingSession(*ctx)->pageState(p); }
QWidget *buildPage(ps::runtime::ModuleContext *ctx, const QJsonObject &o) { return plottingSession(*ctx)->buildPage(o); }
void applyTimeMarkers(ps::runtime::ModuleContext *ctx) { plottingSession(*ctx)->applyTimeMarkers(); }
void removeSourceFromCanvases(ps::runtime::ModuleContext *ctx, const QString &p) { plottingSession(*ctx)->removeSourceFromCanvases(p); }
void addChannels(ps::runtime::ModuleContext *ctx, const QVector<const PsChannelInfo *> &c) { plottingSession(*ctx)->addChannels(c); }
}

namespace ps::bridge
{
void newPage(ps::runtime::ModuleContext *ctx) { ps::plotting::newPage(ctx); }
QVector<PlottingCanvas *> pageCanvases(ps::runtime::ModuleContext *ctx) { return ps::plotting::pageCanvases(ctx); }
PlottingCanvas *currentCanvas(ps::runtime::ModuleContext *ctx) { return ps::plotting::currentCanvas(ctx); }
QJsonObject pageState(ps::runtime::ModuleContext *ctx, QWidget *page) { return ps::plotting::pageState(ctx, page); }
QWidget *buildPage(ps::runtime::ModuleContext *ctx, const QJsonObject &o) { return ps::plotting::buildPage(ctx, o); }
void refreshPropTree(ps::runtime::ModuleContext *ctx, PlottingCanvas *active) { ps::plotting::refreshPropTree(ctx, active); }
void applyTimeMarkers(ps::runtime::ModuleContext *ctx) { ps::plotting::applyTimeMarkers(ctx); }
QVector<PlottingCanvas *> allCanvases(ps::runtime::ModuleContext *ctx)
{
    QVector<PlottingCanvas *> out;
    if (!ctx || !ctx->pages())
        return out;
    for (int i = 0; i < ctx->pages()->count(); ++i)
        if (QWidget *page = ctx->pages()->widget(i))
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
QVector<QPair<QString, QString>> plottedChannelColors(ps::runtime::ModuleContext *ctx)
{
    QVector<QPair<QString, QString>> out;
    if (!ctx)
        return out;
    for (PlottingCanvas *c : allCanvases(ctx))
        for (int i = 0;; ++i)
        {
            const QJsonObject st = c->seriesState(i);
            if (st.isEmpty())
                break;
            const QString name = st.value(QStringLiteral("name")).toString();
            if (name.isEmpty() || st.value(QStringLiteral("srcPath")).toString().isEmpty())
                continue;
            bool dup = false;
            for (const auto &p : out)
                if (p.first == name) { dup = true; break; }
            if (!dup)
                out.append({name, st.value(QStringLiteral("color")).toString()});
        }
    return out;
}

} // namespace ps::bridge
