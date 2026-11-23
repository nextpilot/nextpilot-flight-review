#pragma once

#include "DockManager.h"

#include <QVector>

namespace nextpilot::plotscope::gui
{

class AxesToolbar;
class DockedAxes;

// 一格面板的装配材料：面板由 PlotLayout 创建，外框与标题条归 PlotWorkspace 的 Cell 所有。
struct CellSlot
{
    QString title;
    QWidget *frame = nullptr;
    AxesToolbar *bar = nullptr;
};

// 一个绘图页的停靠容器：建格、按行列摆格、抹平等分，
// 并把 ADS 的面板增删信号转成"格面板"语义。
class PlotLayout : public ads::CDockManager
{
    Q_OBJECT
public:
    explicit PlotLayout(QWidget *host);

    // side: 0=中心格 1=右侧 2=下方；intoContainer 为真时相对整页容器插入（换行用）。
    DockedAxes *addCellDock(const CellSlot &slot, int side, DockedAxes *relative = nullptr,
                              bool intoContainer = false);
    // 按 rows×cols 依次摆满：首格占住整页、行内往右、换行插入容器下方。返回建成顺序的面板。
    QVector<DockedAxes *> layoutGrid(const QVector<CellSlot> &cellSlots, int rows, int cols);
    // 让所有分隔条恢复严格等分（拖过分隔条后点 m×n 重新抹平）。
    void equalizeSplitters();

    // 重建停靠树期间为真：这时的面板删除是"我们在拆旧树"，不是用户关格。
    void setRebuilding(bool rebuilding) { m_rebuilding = rebuilding; }
    bool isRebuilding() const { return m_rebuilding; }

Q_SIGNALS:
    void cellDockAdded(DockedAxes *dock);
    void cellDockRemoved(DockedAxes *dock);

private:
    bool m_rebuilding = false;
};

} // namespace nextpilot::plotscope::gui
