#pragma once

#include "AppContext.h"
#include "IIntentExecutor.h"
#include <QWidget>

class QTableWidget;

namespace nextpilot::plotscope::ext::automotive
{

// CAN 报文浏览器：汽车领域专属面板（形态随 DBC 定义动态变化）。
//
// 详见 CanBrowserPanel.cpp 的说明：形态属领域知识，
// 故实现在扩展包的呈现层，而非通用视图层 gui/views/。
class CanBrowserPanel : public QWidget
{
    Q_OBJECT
public:
    explicit CanBrowserPanel(nextpilot::plotscope::context::AppContext &appCtx, QWidget *parent = nullptr);

    // 重建表格（数据源变化时调用）。
    void refresh();

private:
    nextpilot::plotscope::context::AppContext &m_appCtx;
    QTableWidget *m_table = nullptr;
};

// 工厂函数：与通用视图同款签名，注册进共用的工厂表。
QWidget *createCanBrowserPanel(nextpilot::plotscope::context::AppContext &appCtx, nextpilot::plotscope::intent::IIntentExecutor &exec,
                               QWidget *parent);

} // namespace nextpilot::plotscope::ext::automotive
