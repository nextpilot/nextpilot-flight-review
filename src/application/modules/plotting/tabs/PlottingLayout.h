#pragma once

#include <QScrollArea>
#include <QSplitter>
#include <QVector>

class PlottingCanvas;

namespace ps::plotting
{

// 页内布局：统一承载单画布、m×n 网格和任意 splitter 树。
// setGrid(rows, cols)：cols 固定，行数是下限，画布装不下就向下长；空格由调用方补空白画布。
class PlottingLayout : public QSplitter
{
public:
    explicit PlottingLayout(Qt::Orientation orientation = Qt::Vertical,
                            QWidget *parent = nullptr);

    static PlottingLayout *fromGrid(const QVector<PlottingCanvas *> &canvases,
                                    int rows, int cols, QWidget *parent = nullptr);
    static QVector<PlottingCanvas *> collectCanvases(QWidget *root);
    // 页面可能是裸 PlottingLayout，也可能是带竖直滚动的 PlottingPage
    static PlottingLayout *layoutOf(QWidget *page);

    QVector<PlottingCanvas *> canvases() const;
    void addCanvas(PlottingCanvas *canvas);
    void removeCanvas(PlottingCanvas *canvas);
    void setGrid(int rows, int cols);
    // 按给定顺序整体重置画布（顺序即行优先填充顺序）
    void setCanvases(const QVector<PlottingCanvas *> &ordered);
    void reflow();
    bool isGridMode() const { return m_gridMode; }
    // 手动拆分后就不再是整格网格，避免下一次重排把合并/拆分结果抹平
    void clearGridMode() { m_gridMode = false; }
    int minRows() const { return m_minRows; }
    int gridTotalRows() const { return m_rows; }
    int gridRows() const { return m_rows; }
    int gridCols() const { return m_cols; }
    void split(PlottingCanvas *target, Qt::Orientation orientation,
               PlottingCanvas *canvas);

private:
    void clearChildren();
    void rebuildGrid();

    int m_minRows = 1;
    int m_rows = 1;
    int m_cols = 1;
    bool m_gridMode = false;
};

// 页容器：竖向滚动，画布多到一屏放不下时可往下滚
class PlottingPage : public QScrollArea
{
public:
    explicit PlottingPage(QWidget *parent = nullptr);
    PlottingLayout *layout() const { return m_layout; }

private:
    PlottingLayout *m_layout = nullptr;
};

} // namespace ps::plotting
