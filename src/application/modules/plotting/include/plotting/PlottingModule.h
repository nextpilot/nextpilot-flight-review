#pragma once

#include "Component.h"
#include <QJsonObject>
#include <QVector>

class MainWindow;
class PlottingCanvas;
class QWidget;
class QTableWidget;
class QLabel;
class QToolButton;
class QTreeWidget;
struct PsChannelInfo;

namespace ps::components
{

// 绘图模块：注册“绘图”Ribbon页；画布与页面实现留在本模块。
void registerPlottingContributions(Registry &registry);

class PlottingComponent : public IComponent
{
public:
    ComponentManifest manifest() const override;
    void contribute(Registry &registry, HostContext &host) override;
};

} // namespace ps::components

namespace ps::plotting
{
void createPages(MainWindow *mw);
void refreshPropTree(MainWindow *mw, PlottingCanvas *active = nullptr);
PlottingCanvas *currentCanvas(MainWindow *mw);
QVector<PlottingCanvas *> pageCanvases(MainWindow *mw);
void newPage(MainWindow *mw);
void setCanvasViewFlag(MainWindow *mw, int flag, bool on);
void canvasZoom(MainWindow *mw, double factor);
void canvasAutoY(MainWindow *mw);
void canvasTool(MainWindow *mw, int tool);
void reloadCanvasSettings(MainWindow *mw);
void applyPlayhead(MainWindow *mw, double t);
QJsonObject pageState(MainWindow *mw, QWidget *page);
QWidget *buildPage(MainWindow *mw, const QJsonObject &o);
void applyTimeMarkers(MainWindow *mw);
void removeSourceFromCanvases(MainWindow *mw, const QString &sourcePath);
void addChannels(MainWindow *mw, const QVector<const PsChannelInfo *> &channels);
}
