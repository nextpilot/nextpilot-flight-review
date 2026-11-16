#pragma once

#include <QSplitter>
#include <QVector>

class PlottingCanvas;

namespace ps::plotting
{

// 页内布局：统一承载单画布、m×n 网格和任意 splitter 树。
// 画布都挂在同一套 splitter 树上，网格只是 setGrid() 一次重排的结果。
class PlottingLayout : public QSplitter
{
public:
    explicit PlottingLayout(Qt::Orientation orientation = Qt::Vertical,
                            QWidget *parent = nullptr);

    static PlottingLayout *fromGrid(const QVector<PlottingCanvas *> &canvases,
                                    int rows, int cols, QWidget *parent = nullptr);
    static QVector<PlottingCanvas *> collectCanvases(QWidget *root);

    QVector<PlottingCanvas *> canvases() const;
    void addCanvas(PlottingCanvas *canvas);
    void removeCanvas(PlottingCanvas *canvas);
    void setGrid(int rows, int cols);
    int gridRows() const { return m_rows; }
    int gridCols() const { return m_cols; }
    void split(PlottingCanvas *target, Qt::Orientation orientation,
               PlottingCanvas *canvas);

private:
    void clearChildren();

    int m_rows = 1;
    int m_cols = 1;
};

} // namespace ps::plotting
