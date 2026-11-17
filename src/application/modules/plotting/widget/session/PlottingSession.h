#pragma once

#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <Qt>

namespace ps::runtime { struct ModuleContext; }
namespace ps::shell { struct PropertyContent; }
namespace ps::plotting { class PlottingController; class PlottingService; class PlottingTabs; }
class PlottingCanvas;
class QDockWidget;
class QLabel;
class QSplitter;
struct PsChannelInfo;

class PlottingSession : public QWidget
{
public:
    explicit PlottingSession(ps::runtime::ModuleContext &ctx);
    ~PlottingSession() override;
    void createPages();
    void refreshPropTree(PlottingCanvas *active = nullptr);
    PlottingCanvas *currentCanvas() const;
    QWidget *pageForCanvas(PlottingCanvas *c) const;
    QVector<PlottingCanvas *> pageCanvases() const;
    void setCanvasViewFlag(int flag, bool on);
    void canvasZoom(double factor);
    void setCanvasTool(int tool);   // -1=无工具 0=平移 1=放大 2=缩小，作用于当前页全部画布
    void canvasAutoY();
    void setPropRows(const QString &object, const QVector<QPair<QString, QString>> &rows,
                     const QStringList &editable = QStringList(), int kind = 0);
    PlottingCanvas *makeBlankCanvas();
    static bool canvasHasData(PlottingCanvas *c);
    QJsonObject pageState(QWidget *page) const;
    QWidget *buildPage(const QJsonObject &o);
    QWidget *buildNode(const QJsonObject &o, bool top);
    void applyTimeMarkers();
    void removeSourceFromCanvases(const QString &sourcePath);
    void addChannels(const QVector<const PsChannelInfo *> &channels);
    void newPage();
    void reloadCanvasSettings();
    void createPlotPage(const QString &kind);
    void registerCommands();
    void applyPlayhead(double t);
    void addCanvasToPage();
    void removeCanvas(PlottingCanvas *c);
    void applyCanvasGrid(int rows, int cols);
    void splitCanvas(PlottingCanvas *target, Qt::Orientation orientation);
    void toggleCanvasMaximize(PlottingCanvas *c);
    void restoreCanvasMaximize();
    void showAxesProps(PlottingCanvas *c);
    void showSeriesProps(PlottingCanvas *c, int index);
    void editSeriesProps(PlottingCanvas *c, int index);
    bool applyPropEdit(const QString &key, const QString &text);
    ps::shell::PropertyContent axesContent(PlottingCanvas *c);
    ps::shell::PropertyContent seriesContent(PlottingCanvas *c, int index);
    QString canvasNodeId(PlottingCanvas *c) const;
    void log(const QString &text);
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    ps::runtime::ModuleContext *m_ctx = nullptr;
    ps::plotting::PlottingController *m_core = nullptr;
    ps::plotting::PlottingService *m_service = nullptr;
    ps::plotting::PlottingTabs *m_plotWorkspace = nullptr;
    PlottingCanvas *m_propCanvas = nullptr;
    int m_propSeriesIndex = -1;
    int m_propKind = 0;
    QLabel *m_runLabel = nullptr;
    PlottingCanvas *m_maxedCanvas = nullptr;
    QVector<QWidget *> m_maxedHidden;
    QDockWidget *m_maxDock = nullptr;
    QWidget *m_maxDockCentral = nullptr;
    QList<QDockWidget *> m_maxDockHidden;
    QVector<QPair<QSplitter *, QList<int>>> m_maxedSizes;
    QSize m_sizeBeforeMax;
};

PlottingSession *plottingSession(ps::runtime::ModuleContext &ctx);
