#pragma once

#include <QTabWidget>
#include <QVector>

#include <functional>

class CanvasWidget;

namespace ps::plot
{

// 绘图工作区：页签=绘图页，页内是画布树。
// 画布创建由外部注入，本类负责默认页、画布联动和页内交互。
class PlotWorkspace : public QTabWidget
{
    Q_OBJECT

public:
    using CanvasFactory = std::function<CanvasWidget *(QWidget *parent)>;

    explicit PlotWorkspace(CanvasFactory factory, QWidget *parent = nullptr);

    void createDefaultPages();
    void appendPlusTab();
    void attachCanvas(CanvasWidget *canvas);
    void setTimeMarkers(const QVector<QPair<double, QString>> &markers);

    CanvasWidget *currentCanvas() const;

signals:
    void canvasActivated(CanvasWidget *canvas);
    void canvasAddRequested(CanvasWidget *canvas);
    void canvasSplitRequested(CanvasWidget *canvas, int orientation);
    void addChannelRequested(CanvasWidget *canvas);
    void canvasMaximizeRequested(CanvasWidget *canvas);
    void canvasRemoveRequested(CanvasWidget *canvas);
    void markerRequested(double t);
    void markerRemoveRequested(double t);

private:
    CanvasWidget *makeCanvas(const QString &title, QWidget *parent);
    QVector<CanvasWidget *> pageCanvases() const;
    QVector<CanvasWidget *> allCanvases() const;

    CanvasFactory m_factory;
    QVector<QPair<double, QString>> m_markers;
};

} // namespace ps::plot
