#pragma once

#include <QWidget>

class QPlainTextEdit;

namespace nextpilot::plotscope::context
{
class AppContext;
}

namespace nextpilot::plotscope::gui
{

// 输出面板（Region::Panel）：**全量**消息流。
//
// 与终端的分工：终端是交互式 REPL（能输入、能求值），输出是只读的进程日志视图
// （脚本 print、外壳提示、调试事件全收）。与问题的分工：问题只留 warn/error，
// 输出保留全部级别——排查"为什么这条错误前面还有一堆 info"时用。
//
// 数据源是 AppContext::messages()：订阅 messageAppended 增量追加，不重读全表。
class OutputPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit OutputPanel(context::AppContext &appCtx, QWidget *parent = nullptr);

    // 追加一条（内部在信号上调用；公开是为了单测直接喂数据）。
    void appendMessage(const QString &level, const QString &source, const QString &text,
                       const QString &locator);
    void clearLog();

private:
    void reloadFromLog();

    context::AppContext &m_appCtx;
    QPlainTextEdit *m_view = nullptr;
};

} // namespace nextpilot::plotscope::gui
