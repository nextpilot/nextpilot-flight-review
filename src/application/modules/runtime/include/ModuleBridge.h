#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <functional>

class MainWindow;
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

using SourceViewsProvider = std::function<QVector<SourceView>(MainWindow *)>;
void setSourceViewsProvider(MainWindow *mw, SourceViewsProvider provider);
QVector<SourceView> sourceViews(MainWindow *mw);
QVector<const PsChannelInfo *> sourceChannels(MainWindow *mw);
QVector<QPair<double, QString>> timeMarkers(MainWindow *mw);
void addTimeMarker(MainWindow *mw, double t, const QString &label, bool prompt);
void removeTimeMarker(MainWindow *mw, double t);

// 绘图模块向数据源模块暴露的操作，避免 data_source 反向 include plotting。
void newPage(MainWindow *mw);
QVector<PlottingCanvas *> pageCanvases(MainWindow *mw);
PlottingCanvas *currentCanvas(MainWindow *mw);
QJsonObject pageState(MainWindow *mw, QWidget *page);
QWidget *buildPage(MainWindow *mw, const QJsonObject &o);
void refreshPropTree(MainWindow *mw, PlottingCanvas *active = nullptr);
void applyTimeMarkers(MainWindow *mw);
void setPropRows(MainWindow *mw, const QString &object,
                 const QVector<QPair<QString, QString>> &rows);
QTableWidget *propTable(MainWindow *mw);
QTreeWidget *propTree(MainWindow *mw);
QVector<PlottingCanvas *> allCanvases(MainWindow *mw);
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

} // namespace ps::bridge
