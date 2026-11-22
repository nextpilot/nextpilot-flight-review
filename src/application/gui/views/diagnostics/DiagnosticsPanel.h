#pragma once

#include <QWidget>

class QTableWidget;

namespace nextpilot::plotscope::context
{
class AppContext;
}
namespace nextpilot::plotscope::intent
{
class IIntentExecutor;
}

namespace nextpilot::plotscope::gui
{

// M2.8 诊断面板（杀手场景收口）：一张表格列出全系统注册树——
// 列 kind / name / count / keys，数据走 diag.registries view action
// （GUI/脚本/CLI 同一条路径）。整改前不存在任何途径可以获得这张清单。
class DiagnosticsPanel : public QWidget
{
    Q_OBJECT
public:
    DiagnosticsPanel(nextpilot::plotscope::context::AppContext &appCtx,
                     nextpilot::plotscope::intent::IIntentExecutor &executor,
                     QWidget *parent = nullptr);

    // 重新拉取 diag.registries 并填充表格（返回行数，测试断言用）。
    int refresh();

private:
    nextpilot::plotscope::context::AppContext &m_appCtx;
    QTableWidget *m_table = nullptr;
};

} // namespace nextpilot::plotscope::gui
