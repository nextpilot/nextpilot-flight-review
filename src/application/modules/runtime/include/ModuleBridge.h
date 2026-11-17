#pragma once

#include <QJsonObject>
#include <QString>
#include <QPair>
#include <QVector>
#include <functional>

#include "TimeBookmark.h"

namespace ps::runtime
{
struct ModuleContext;
}

class PlottingCanvas;
class QWidget;
class QPlainTextEdit;
class QComboBox;
class QTableWidget;
class QTreeWidget;
struct PsChannelInfo;

namespace ps::bridge
{

// 数据源模块向绘图模块提供的只读视图，避免 plot 反向 include data_source。
struct SourceView
{
    int id = -1;
    QString path;
    QString name;
    QString alias;
    QVector<const PsChannelInfo *> channels;
};

using SourceViewsProvider = std::function<QVector<SourceView>(ps::runtime::ModuleContext *)>;
void setSourceViewsProvider(ps::runtime::ModuleContext *ctx, SourceViewsProvider provider);
QVector<SourceView> sourceViews(ps::runtime::ModuleContext *ctx);
QVector<const PsChannelInfo *> sourceChannels(ps::runtime::ModuleContext *ctx);
QVector<ps::TimeBookmark> timeMarkers(ps::runtime::ModuleContext *ctx);
void addTimeMarker(ps::runtime::ModuleContext *ctx, double t, const QString &label, bool prompt);
void removeTimeMarker(ps::runtime::ModuleContext *ctx, const QString &id);
void moveTimeMarker(ps::runtime::ModuleContext *ctx, const QString &id, double to);
void renameTimeMarker(ps::runtime::ModuleContext *ctx, const QString &id, const QString &label);

// 绘图模块向数据源模块暴露的操作，避免 data_source 反向 include plotting。
void newPage(ps::runtime::ModuleContext *ctx);
QVector<PlottingCanvas *> pageCanvases(ps::runtime::ModuleContext *ctx);
PlottingCanvas *currentCanvas(ps::runtime::ModuleContext *ctx);
QJsonObject pageState(ps::runtime::ModuleContext *ctx, QWidget *page);
QWidget *buildPage(ps::runtime::ModuleContext *ctx, const QJsonObject &o);
void refreshPropTree(ps::runtime::ModuleContext *ctx, PlottingCanvas *active = nullptr);
void applyTimeMarkers(ps::runtime::ModuleContext *ctx);
QVector<PlottingCanvas *> allCanvases(ps::runtime::ModuleContext *ctx);
void canvasAddChannel(PlottingCanvas *c, const PsChannelInfo *ch);
void canvasAddChannelEx(PlottingCanvas *c, const PsChannelInfo *ch, int srcId,
                       const QString &path, const QString &alias, double offsetSec);
bool canvasReplaceChannel(PlottingCanvas *c, const QString &oldName, const PsChannelInfo *ch);
void canvasSetChannels(PlottingCanvas *c, const QVector<const PsChannelInfo *> &channels);
void canvasSetTitle(PlottingCanvas *c, const QString &title);
void canvasSetInputSource(PlottingCanvas *c, int id, const QString &path,
                          const QString &alias = QString(), double offsetSec = 0.0);
void canvasSetSourceOffset(PlottingCanvas *c, const QString &path, double offsetSec, bool refit);
void canvasRemoveChannel(PlottingCanvas *c, const QString &name);
void canvasRemoveChannelsFromSource(PlottingCanvas *c, const QString &path);
void canvasRenameSource(PlottingCanvas *c, const QString &path, const QString &alias);
void canvasFollowLatest(PlottingCanvas *c);
void canvasNotifyActivated(PlottingCanvas *c);
void canvasUpdate(PlottingCanvas *c);
bool canvasHasData(PlottingCanvas *c);
void canvasSetFollowLive(PlottingCanvas *c, bool on, double windowSec);

// 已绘制通道的当前配色（通道全名 → #rrggbb），供数据源树按曲线颜色显示通道名。
QVector<QPair<QString, QString>> plottedChannelColors(ps::runtime::ModuleContext *ctx);

} // namespace ps::bridge
