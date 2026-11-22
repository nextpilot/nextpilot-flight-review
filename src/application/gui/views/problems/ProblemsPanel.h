#pragma once

#include <QWidget>

class QLabel;
class QTableWidget;

namespace nextpilot::plotscope::context
{
class AppContext;
}

namespace nextpilot::plotscope::gui
{

// 问题面板（dock.bottom）：消息流里**只留 warn/error** 的那一部分。
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

} // namespace nextpilot::plotscope::gui
