#include "DockedAxes.h"

#include "chrome/AxesToolbar.h"
#include "PlotLayout.h"

namespace nextpilot::plotscope::gui
{

DockedAxes::DockedAxes(PlotLayout *pageDock, const QString &title, QWidget *frame, AxesToolbar *bar)
    : ads::CDockWidget(pageDock, title), m_frame(frame), m_bar(bar)
{
    setObjectName(QStringLiteral("dockedAxes"));
    // 关闭入口在标题条上，面板自带的关闭按钮会和它重复，还可能被误点。
    setFeature(ads::CDockWidget::DockWidgetClosable, false);
    // 可拖、可浮动：拖分屏 / 拖中心合并 / 拖出浮动 / 拖回停靠全由 ADS 提供。
    setFeature(ads::CDockWidget::DockWidgetMovable, true);
    setFeature(ads::CDockWidget::DockWidgetFloatable, true);
    // 装的是外框，画布要填满面板，不再套一层滚动区。
    setWidget(frame, ads::CDockWidget::ForceNoScrollArea);
    // 标题条要知道自己属于哪个面板：拖动要转给面板所在停靠区的原生标题栏。
    if (m_bar)
        m_bar->setDockWidget(this);
}

QWidget *DockedAxes::takeCellWidget()
{
    return takeWidget();
}

} // namespace nextpilot::plotscope::gui
