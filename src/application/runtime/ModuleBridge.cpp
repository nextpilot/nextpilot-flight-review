#include "ModuleBridge.h"

namespace ps::bridge
{

Services &services()
{
    static Services instance;
    return instance;
}

void setSourceViewsProvider(ps::runtime::ModuleContext *, SourceViewsProvider provider)
{
    services().sourceViews = std::move(provider);
}

QVector<SourceView> sourceViews(ps::runtime::ModuleContext *ctx)
{
    return services().sourceViews ? services().sourceViews(ctx) : QVector<SourceView>{};
}

QVector<const PsChannelInfo *> sourceChannels(ps::runtime::ModuleContext *ctx)
{
    return services().sourceChannels ? services().sourceChannels(ctx) : QVector<const PsChannelInfo *>{};
}

QVector<ps::TimeBookmark> timeMarkers(ps::runtime::ModuleContext *ctx)
{
    return services().timeMarkers ? services().timeMarkers(ctx) : QVector<ps::TimeBookmark>{};
}

void addTimeMarker(ps::runtime::ModuleContext *ctx, double t, const QString &label, bool prompt)
{
    if (services().addTimeMarker)
        services().addTimeMarker(ctx, t, label, prompt);
}

void removeTimeMarker(ps::runtime::ModuleContext *ctx, const QString &id)
{
    if (services().removeTimeMarker)
        services().removeTimeMarker(ctx, id);
}

void moveTimeMarker(ps::runtime::ModuleContext *ctx, const QString &id, double to)
{
    if (services().moveTimeMarker)
        services().moveTimeMarker(ctx, id, to);
}

void renameTimeMarker(ps::runtime::ModuleContext *ctx, const QString &id, const QString &label)
{
    if (services().renameTimeMarker)
        services().renameTimeMarker(ctx, id, label);
}

void newPage(ps::runtime::ModuleContext *ctx)
{
    if (services().newPage)
        services().newPage(ctx);
}

QVector<PlottingCanvas *> pageCanvases(ps::runtime::ModuleContext *ctx)
{
    return services().pageCanvases ? services().pageCanvases(ctx) : QVector<PlottingCanvas *>{};
}

PlottingCanvas *currentCanvas(ps::runtime::ModuleContext *ctx)
{
    return services().currentCanvas ? services().currentCanvas(ctx) : nullptr;
}

QJsonObject pageState(ps::runtime::ModuleContext *ctx, QWidget *page)
{
    return services().pageState ? services().pageState(ctx, page) : QJsonObject{};
}

QWidget *buildPage(ps::runtime::ModuleContext *ctx, const QJsonObject &object)
{
    return services().buildPage ? services().buildPage(ctx, object) : nullptr;
}

void refreshPropTree(ps::runtime::ModuleContext *ctx, PlottingCanvas *active)
{
    if (services().refreshPropTree)
        services().refreshPropTree(ctx, active);
}

void applyTimeMarkers(ps::runtime::ModuleContext *ctx)
{
    if (services().applyTimeMarkers)
        services().applyTimeMarkers(ctx);
}

QVector<PlottingCanvas *> allCanvases(ps::runtime::ModuleContext *ctx)
{
    return services().allCanvases ? services().allCanvases(ctx) : QVector<PlottingCanvas *>{};
}

void canvasAddChannel(PlottingCanvas *canvas, const PsChannelInfo *channel)
{
    if (services().canvasAddChannel)
        services().canvasAddChannel(canvas, channel);
}

void canvasAddChannelEx(PlottingCanvas *canvas, const PsChannelInfo *channel, int sourceId,
                        const QString &path, const QString &alias, double offsetSec)
{
    if (services().canvasAddChannelEx)
        services().canvasAddChannelEx(canvas, channel, sourceId, path, alias, offsetSec);
}

bool canvasReplaceChannel(PlottingCanvas *canvas, const QString &oldName, const PsChannelInfo *channel)
{
    return services().canvasReplaceChannel ? services().canvasReplaceChannel(canvas, oldName, channel) : false;
}

void canvasSetChannels(PlottingCanvas *canvas, const QVector<const PsChannelInfo *> &channels)
{
    if (services().canvasSetChannels)
        services().canvasSetChannels(canvas, channels);
}

void canvasSetTitle(PlottingCanvas *canvas, const QString &title)
{
    if (services().canvasSetTitle)
        services().canvasSetTitle(canvas, title);
}

void canvasSetInputSource(PlottingCanvas *canvas, int id, const QString &path,
                          const QString &alias, double offsetSec)
{
    if (services().canvasSetInputSource)
        services().canvasSetInputSource(canvas, id, path, alias, offsetSec);
}

void canvasSetSourceOffset(PlottingCanvas *canvas, const QString &path, double offsetSec, bool refit)
{
    if (services().canvasSetSourceOffset)
        services().canvasSetSourceOffset(canvas, path, offsetSec, refit);
}

void canvasRemoveChannel(PlottingCanvas *canvas, const QString &name)
{
    if (services().canvasRemoveChannel)
        services().canvasRemoveChannel(canvas, name);
}

void canvasRemoveChannelsFromSource(PlottingCanvas *canvas, const QString &path)
{
    if (services().canvasRemoveChannelsFromSource)
        services().canvasRemoveChannelsFromSource(canvas, path);
}

void canvasRenameSource(PlottingCanvas *canvas, const QString &path, const QString &alias)
{
    if (services().canvasRenameSource)
        services().canvasRenameSource(canvas, path, alias);
}

void canvasFollowLatest(PlottingCanvas *canvas)
{
    if (services().canvasFollowLatest)
        services().canvasFollowLatest(canvas);
}

void canvasNotifyActivated(PlottingCanvas *canvas)
{
    if (services().canvasNotifyActivated)
        services().canvasNotifyActivated(canvas);
}

void canvasUpdate(PlottingCanvas *canvas)
{
    if (services().canvasUpdate)
        services().canvasUpdate(canvas);
}

bool canvasHasData(PlottingCanvas *canvas)
{
    return services().canvasHasData ? services().canvasHasData(canvas) : false;
}

void canvasSetFollowLive(PlottingCanvas *canvas, bool on, double windowSec)
{
    if (services().canvasSetFollowLive)
        services().canvasSetFollowLive(canvas, on, windowSec);
}

QVector<QPair<QString, QString>> plottedChannelColors(ps::runtime::ModuleContext *ctx)
{
    return services().plottedChannelColors ? services().plottedChannelColors(ctx)
                                           : QVector<QPair<QString, QString>>{};
}

} // namespace ps::bridge
