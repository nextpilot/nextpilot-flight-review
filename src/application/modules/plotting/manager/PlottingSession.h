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

class MainWindow;
namespace ps::plotting { class PlottingTabs; }
class PlottingCanvas;
class QDockWidget;
class QLabel;
class QSplitter;
class QTableWidget;
class QToolButton;
class QTreeWidget;
struct PsChannelInfo;

class PlottingSession : public QWidget
{
public:
    explicit PlottingSession(MainWindow *mw);
    ~PlottingSession() override;
    void createPages();
    void refreshPropTree(PlottingCanvas *active = nullptr);
    PlottingCanvas *currentCanvas() const;
    QWidget *pageForCanvas(PlottingCanvas *c) const;
    QVector<PlottingCanvas *> pageCanvases() const;
    void setCanvasViewFlag(int flag, bool on);
    void canvasZoom(double factor);
    void canvasAutoY();
    QTableWidget *propTable() const { return m_propTable; }
    QTreeWidget *propTree() const { return m_propTree; }
    void setPropRows(const QString &object, const QVector<QPair<QString, QString>> &rows);
    void bindPlotPanels(QLabel *sel, QToolButton *edit, QTableWidget *table, QTreeWidget *tree);
    QJsonObject pageState(QWidget *page) const;
    QWidget *buildPage(const QJsonObject &o);
    QWidget *createPropPanel(bool tree);
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
    void editAxesProps(PlottingCanvas *c);
    void log(const QString &text);

private:
    MainWindow *m_mw = nullptr;
    ps::plotting::PlottingTabs *m_plotWorkspace = nullptr;
    QTableWidget *m_propTable = nullptr;
    QToolButton *m_propEditBtn = nullptr;
    PlottingCanvas *m_propCanvas = nullptr;
    int m_propSeriesIndex = -1;
    int m_propKind = 0;
    QTreeWidget *m_propTree = nullptr;
    bool m_propTreeBuilding = false;
    QLabel *m_selLabel = nullptr;
    QLabel *m_runLabel = nullptr;
    PlottingCanvas *m_maxedCanvas = nullptr;
    QVector<QWidget *> m_maxedHidden;
    QDockWidget *m_maxDock = nullptr;
    QWidget *m_maxDockCentral = nullptr;
    QList<QDockWidget *> m_maxDockHidden;
    QVector<QPair<QSplitter *, QList<int>>> m_maxedSizes;
    QSize m_sizeBeforeMax;
};

PlottingSession *plottingSession(MainWindow *mw);
