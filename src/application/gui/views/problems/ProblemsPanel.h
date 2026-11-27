#pragma once

#include <QWidget>

class QLabel;
class QTableWidget;

namespace nextpilot::plotscope::context
{
class AppContext;
}

namespace nextpilot::plotscope::intent {
class IIntentExecutor;
}

namespace nextpilot::plotscope::gui
{

// 问题面板（Region::Panel）：消息流里**只留 warn/error** 的那一部分。
//
// 为什么和输出分开：输出是流（按时间看全部），问题是清单（按严重度收敛，
// 可逐条定位）。脚本错误的定位串是 "name:line"，双击直接跳到编辑器对应行，
// 与终端里点错误行是同一个落点（不新增第二条跳转通道）。
class ProblemsPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit ProblemsPanel(context::AppContext &appCtx, QWidget *parent = nullptr);

    // 追加一条（非 warn/error 直接忽略；公开供单测）。
    void appendMessage(const QString &level, const QString &source, const QString &text,
                       const QString &locator);
    void clearProblems();
    int rowCount() const;

private:
    void reloadFromLog();

    context::AppContext &m_appCtx;
    QTableWidget *m_table = nullptr;
    QLabel *m_summary = nullptr;
};

// 视图工厂（A2b 笔2b 第②步导出）：装配层（BuiltinWorkbenchViews）经它把本视图
// 注册进 ViewRegistry（registerViewProvider，owner core.workbench）——旧
// 旧工厂表注册已随之删除（A2d 连宏本体一并退场）。
QWidget *createProblemsPanel(nextpilot::plotscope::context::AppContext &appCtx,
                             nextpilot::plotscope::intent::IIntentExecutor &exec, QWidget *parent);

} // namespace nextpilot::plotscope::gui
