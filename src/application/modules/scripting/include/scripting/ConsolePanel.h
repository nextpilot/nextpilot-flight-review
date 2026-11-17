#pragma once

#include "Component.h"

#include <QWidget>

#include <functional>

struct lua_State;

class QLineEdit;
class QTableWidget;
class QTextEdit;

namespace ps::scripting
{

class ScratchPlot;
class ScriptConsole;

// 脚本控制台面板：输出区 + 输入行 + 结果表。
// 只是 ScriptConsole 的壳，执行语义全在 ScriptConsole 里。
class ConsolePanel final : public QWidget
{
    Q_OBJECT

public:
    explicit ConsolePanel(QWidget *parent = nullptr);
    ~ConsolePanel() override;

    static ps::components::DockContribution dockContribution();

    // 宿主/测试要往里敲命令时用；执行语义仍归 ScriptConsole
    ScriptConsole *console() const { return m_console; }

    void showResultTable(lua_State *L, int tableIdx, const QString &title);
    void showScratchPlot(lua_State *L, int tableIdx);

private:
    void submit();
    void append(const QString &text, bool error = false);
    void installTableApi(lua_State *L);
    void installPlotApi(lua_State *L);

    ScriptConsole *m_console = nullptr;
    QTextEdit *m_out = nullptr;
    QLineEdit *m_in = nullptr;
    QTableWidget *m_table = nullptr;
    ScratchPlot *m_plot = nullptr;
    QStringList m_history;
    int m_historyPos = 0;
};

} // namespace ps::scripting
