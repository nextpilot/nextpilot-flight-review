#pragma once

#include "Component.h"
#include <QJsonObject>
#include <QVector>

namespace ps::runtime { struct ModuleContext; }
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
void createPages(ps::runtime::ModuleContext *ctx);
void refreshPropTree(ps::runtime::ModuleContext *ctx, PlottingCanvas *active = nullptr);
PlottingCanvas *currentCanvas(ps::runtime::ModuleContext *ctx);
QVector<PlottingCanvas *> pageCanvases(ps::runtime::ModuleContext *ctx);
void newPage(ps::runtime::ModuleContext *ctx);
void setCanvasViewFlag(ps::runtime::ModuleContext *ctx, int flag, bool on);
void canvasZoom(ps::runtime::ModuleContext *ctx, double factor);
void canvasAutoY(ps::runtime::ModuleContext *ctx);
void canvasTool(ps::runtime::ModuleContext *ctx, int tool);
void reloadCanvasSettings(ps::runtime::ModuleContext *ctx);
void applyPlayhead(ps::runtime::ModuleContext *ctx, double t);
QJsonObject pageState(ps::runtime::ModuleContext *ctx, QWidget *page);
QWidget *buildPage(ps::runtime::ModuleContext *ctx, const QJsonObject &o);
void applyTimeMarkers(ps::runtime::ModuleContext *ctx);
void removeSourceFromCanvases(ps::runtime::ModuleContext *ctx, const QString &sourcePath);
void addChannels(ps::runtime::ModuleContext *ctx, const QVector<const PsChannelInfo *> &channels);
}
