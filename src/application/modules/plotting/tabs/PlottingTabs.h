#pragma once

#include <QTabWidget>
#include <QVector>

#include <functional>

class PlottingCanvas;

namespace ps::plotting
{

// 绘图工作区：页签=绘图页，页内由 PlottingLayout 承载画布树。
// 画布创建由外部注入，本类负责默认页、画布联动和页内交互。
class PlottingTabs : public QTabWidget
{
    Q_OBJECT

public:
    using CanvasFactory = std::function<PlottingCanvas *(QWidget *parent)>;

    explicit PlottingTabs(CanvasFactory factory, QWidget *parent = nullptr);

    void createDefaultPages();
    void appendPlusTab();
    void attachCanvas(PlottingCanvas *canvas);
    void setTimeMarkers(const QVector<QPair<double, QString>> &markers);

    PlottingCanvas *currentCanvas() const;

signals:
    void canvasActivated(PlottingCanvas *canvas);
    void seriesSelected(PlottingCanvas *canvas, int index);
    void canvasAddRequested(PlottingCanvas *canvas);
    void canvasSplitRequested(PlottingCanvas *canvas, int orientation);
    void addChannelRequested(PlottingCanvas *canvas);
    void canvasMaximizeRequested(PlottingCanvas *canvas);
    void canvasRemoveRequested(PlottingCanvas *canvas);
    void pageRenamed(int index, const QString &title);
    void markerRequested(double t);
    void markerRemoveRequested(double t);
    void markerMoveRequested(double from, double to);

private:
    PlottingCanvas *makeCanvas(const QString &title, QWidget *parent);
    QVector<PlottingCanvas *> pageCanvases() const;
    QVector<PlottingCanvas *> allCanvases() const;

    CanvasFactory m_factory;
    QVector<QPair<double, QString>> m_markers;
};

} // namespace ps::plotting
