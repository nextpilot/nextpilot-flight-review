#pragma once

#include <QWidget>

class QLineEdit;
class QPlainTextEdit;

namespace nextpilot::plotscope::context
{
class AppContext;
}

namespace nextpilot::plotscope::intent {
class IIntentExecutor;
}

namespace nextpilot::plotscope::gui
{

// 调试控制台（Region::Panel）：调试会话的输出 + 求值入口。
//
// 与「终端」的分工：终端是独立 REPL（新引擎、不参与当前调试会话）；调试控制台
// 走 script.workbench.repl —— 该意图在调试暂停时优先交给**当前调试引擎**求值
// （ScriptModule 里 debugPaused() 分支），所以这里输入的表达式能看到断点处的
// 局部变量。两块面板共用一条意图，不复制求值逻辑。
//
// 事件流只取 level=debug（暂停/单步/结束由 ScriptModule 写入消息流），
// 普通 print 不进这里——那属于终端与输出。
class DebugConsolePanel final : public QWidget
{
    Q_OBJECT
public:
    explicit DebugConsolePanel(context::AppContext &appCtx, QWidget *parent = nullptr);

    void appendMessage(const QString &level, const QString &source, const QString &text,
                       const QString &locator);
    void clearConsole();
    void evaluate(const QString &code);

private:
    void reloadFromLog();

    context::AppContext &m_appCtx;
    QPlainTextEdit *m_view = nullptr;
    QLineEdit *m_input = nullptr;
};

// 视图工厂（A2b 笔2b 第②步导出）：装配层（BuiltinWorkbenchViews）经它把本视图
// 注册进 ViewRegistry（registerViewProvider，owner core.workbench）——旧
// 旧工厂表注册已随之删除（A2d 连宏本体一并退场）。
QWidget *createDebugConsolePanel(nextpilot::plotscope::context::AppContext &appCtx,
                                 nextpilot::plotscope::intent::IIntentExecutor &exec, QWidget *parent);

} // namespace nextpilot::plotscope::gui
