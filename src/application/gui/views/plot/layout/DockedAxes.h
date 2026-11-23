#pragma once

#include "DockWidget.h"

namespace nextpilot::plotscope::gui
{

class AxesToolbar;
class PlotLayout;

// 一格画布在停靠树里的面板：持有外框（标题条 + 画布堆叠）。
// 关闭入口只留标题条上的按钮，所以面板自身不可关闭。
class DockedAxes : public ads::CDockWidget
{
    Q_OBJECT
public:
    DockedAxes(PlotLayout *pageDock, const QString &title, QWidget *frame, AxesToolbar *bar);

    AxesToolbar *titleBar() const { return m_bar; }
    QWidget *cellWidget() const { return m_frame; }
    // 重建前摘出外框：面板随后销毁，不再连带删它。
    QWidget *takeCellWidget();

private:
    QWidget *m_frame = nullptr;
    AxesToolbar *m_bar = nullptr;
};

} // namespace nextpilot::plotscope::gui
