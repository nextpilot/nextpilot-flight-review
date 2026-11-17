#include "scripting/ConsolePanel.h"

#include "scripting/ScratchPlot.h"
#include "scripting/ScriptConsole.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVector>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>

namespace ps::scripting
{
namespace
{
const int kMaxRows = 500;

// ps.table(t[, {title=}])：把表推到结果面板，同时往输出区打一行摘要。
// 无面板时（--run）不装这个 API，脚本里调会报 nil。
ConsolePanel *panelOf(lua_State *L)
{
    return static_cast<ConsolePanel *>(lua_touserdata(L, lua_upvalueindex(1)));
}

int l_ps_plot(lua_State *L)
{
    ConsolePanel *panel = panelOf(L);
    if (!panel || !lua_istable(L, 1))
    {
        lua_pushnil(L);
        lua_pushstring(L, "eformat: ps.plot 需要 table");
        return 2;
    }
    panel->showScratchPlot(L, 1);
    return 0;
}

int l_ps_clear(lua_State *L)
{
    ConsolePanel *panel = panelOf(L);
    if (panel)
        panel->showScratchPlot(L, 0);
    return 0;
}

int l_ps_table(lua_State *L)
{
    ConsolePanel *panel = panelOf(L);
    if (!panel || !lua_istable(L, 1))
    {
        lua_pushnil(L);
        lua_pushstring(L, "eformat: ps.table 需要 table");
        return 2;
    }
    QString title;
    if (lua_istable(L, 2))
    {
        lua_getfield(L, 2, "title");
        title = QString::fromUtf8(lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    panel->showResultTable(L, 1, title);
    return 0;
}
} // namespace

// 结果表转二维字符串：优先认「列式」{t={},v={}}，否则按「行式」逐行取。
void ConsolePanel::showResultTable(lua_State *L, int idx, const QString &title)
{
    idx = lua_absindex(L, idx);
    QVector<QStringList> rows;
    QStringList headers;

    lua_getfield(L, idx, "t");
    const bool columnar = lua_istable(L, -1);
    lua_pop(L, 1);

    if (columnar)
    {
        lua_getfield(L, idx, "v");
        const bool hasV = lua_istable(L, -1);
        lua_pop(L, 1);
        headers << QStringLiteral("t");
        if (hasV)
            headers << QStringLiteral("v");
        // 逐列填
        const int cols = headers.size();
        lua_getfield(L, idx, "t");
        const lua_Integer n = luaL_len(L, -1);
        lua_pop(L, 1);
        rows.resize(static_cast<int>(std::min<lua_Integer>(n, kMaxRows)));
        for (int c = 0; c < cols; ++c)
        {
            lua_getfield(L, idx, c == 0 ? "t" : "v");
            for (int r = 0; r < rows.size(); ++r)
            {
                lua_rawgeti(L, -1, r + 1);
                double v = lua_tonumber(L, -1);
                lua_pop(L, 1);
                rows[r] << QString::number(v, 'g', 6);
            }
            lua_pop(L, 1);
        }
    }
    else
    {
        const lua_Integer n = luaL_len(L, idx);
        const int cnt = static_cast<int>(std::min<lua_Integer>(n, kMaxRows));
        rows.reserve(cnt);
        for (int r = 0; r < cnt; ++r)
        {
            lua_rawgeti(L, idx, r + 1);
            QStringList row;
            if (lua_istable(L, -1))
            {
                const lua_Integer m = luaL_len(L, -1);
                for (lua_Integer c = 1; c <= m; ++c)
                {
                    lua_rawgeti(L, -1, c);
                    row << QString::number(lua_tonumber(L, -1), 'g', 6);
                    lua_pop(L, 1);
                }
                if (headers.isEmpty())
                    for (int c = 0; c < row.size(); ++c)
                        headers << QStringLiteral("c%1").arg(c + 1);
            }
            else
            {
                row << QString::number(lua_tonumber(L, -1), 'g', 6);
                if (headers.isEmpty())
                    headers << QStringLiteral("v");
            }
            lua_pop(L, 1);
            rows << row;
        }
    }

    m_table->clear();
    m_table->setColumnCount(headers.size());
    m_table->setHorizontalHeaderLabels(headers);
    m_table->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r)
        for (int c = 0; c < rows[r].size() && c < headers.size(); ++c)
            m_table->setItem(r, c, new QTableWidgetItem(rows[r][c]));
    m_table->resizeColumnsToContents();
    m_table->setVisible(true);

    append(QStringLiteral("%1  %2 行 × %3 列")
               .arg(title.isEmpty() ? QStringLiteral("结果") : title)
               .arg(rows.size())
               .arg(headers.size()));
}

namespace
{
bool readNumArray(lua_State *L, int idx, QVector<double> *out)
{
    if (!lua_istable(L, idx))
        return false;
    const int n = static_cast<int>(luaL_len(L, idx));
    out->resize(n);
    for (int i = 0; i < n; ++i)
    {
        lua_rawgeti(L, idx, i + 1);
        (*out)[i] = lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    return n > 0;
}
} // namespace

// 认 {t=,v=} / {f=,mag=} / {f=,psd=} / 纯数组；超过 4000 点按步长抽稀。
void ConsolePanel::showScratchPlot(lua_State *L, int idx)
{
    if (idx == 0)
    {
        m_plot->clearSeries();
        return;
    }
    idx = lua_absindex(L, idx);

    QVector<double> xs;
    QVector<double> ys;
    QString xUnit;
    QString title;

    lua_getfield(L, idx, "title");
    title = QString::fromUtf8(lua_tostring(L, -1));
    lua_pop(L, 1);

    lua_getfield(L, idx, "t");
    const bool hasT = lua_istable(L, -1);
    if (hasT)
        readNumArray(L, -1, &xs);
    lua_pop(L, 1);

    if (!hasT)
    {
        lua_getfield(L, idx, "f");
        const bool hasF = lua_istable(L, -1);
        if (hasF)
        {
            readNumArray(L, -1, &xs);
            xUnit = QStringLiteral("Hz");
        }
        lua_pop(L, 1);
        if (!hasF)
            xUnit.clear();
    }

    lua_getfield(L, idx, "v");
    if (lua_istable(L, -1))
        readNumArray(L, -1, &ys);
    lua_pop(L, 1);
    if (ys.isEmpty())
    {
        lua_getfield(L, idx, "mag");
        if (lua_istable(L, -1))
            readNumArray(L, -1, &ys);
        lua_pop(L, 1);
    }
    if (ys.isEmpty())
    {
        lua_getfield(L, idx, "psd");
        if (lua_istable(L, -1))
            readNumArray(L, -1, &ys);
        lua_pop(L, 1);
    }
    if (ys.isEmpty())
        readNumArray(L, idx, &ys);

    if (ys.size() < 2)
    {
        append(QStringLiteral("ps.plot: 没有可画的数据"), true);
        return;
    }
    if (xs.size() != ys.size())
    {
        xs.resize(ys.size());
        for (int i = 0; i < xs.size(); ++i)
            xs[i] = i;
    }

    QVector<QPointF> pts;
    pts.reserve(ys.size());
    const int stride = std::max(1, static_cast<int>(ys.size()) / 4000);
    for (int i = 0; i < ys.size(); i += stride)
        pts << QPointF(xs[i], ys[i]);
    if (pts.last().x() != xs.last())
        pts << QPointF(xs.last(), ys.last());

    m_plot->setSeries(pts, title.isEmpty() ? QStringLiteral("scratch") : title, xUnit);
    m_plot->setVisible(true);
    append(QStringLiteral("%1  %2 点").arg(title.isEmpty() ? QStringLiteral("图") : title).arg(pts.size()));
}

void ConsolePanel::installPlotApi(lua_State *L)
{
    lua_getglobal(L, "ps");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setglobal(L, "ps");
    }
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, l_ps_plot, 1);
    lua_setfield(L, -2, "plot");
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, l_ps_clear, 1);
    lua_setfield(L, -2, "clearResults");
    lua_pop(L, 1);
}

void ConsolePanel::installTableApi(lua_State *L)
{
    lua_getglobal(L, "ps");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setglobal(L, "ps");
    }
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, l_ps_table, 1);
    lua_setfield(L, -2, "table");
    lua_pop(L, 1);
}

ConsolePanel::ConsolePanel(QWidget *parent) : QWidget(parent)
{
    m_console = new ScriptConsole();
    m_console->addStateHook([this](lua_State *L) {
        installTableApi(L);
        installPlotApi(L);
    });

    m_out = new QTextEdit(this);
    m_out->setReadOnly(true);
    m_out->setFont(QFont(QStringLiteral("monospace"), 10));
    m_out->setLineWrapMode(QTextEdit::NoWrap);

    m_table = new QTableWidget(this);
    m_table->setFont(QFont(QStringLiteral("monospace"), 9));
    m_table->setVisible(false);
    m_table->verticalHeader()->setVisible(false);

    m_plot = new ScratchPlot(this);
    m_plot->setVisible(false);

    m_in = new QLineEdit(this);
    m_in->setFont(QFont(QStringLiteral("monospace"), 10));
    m_in->setPlaceholderText(QStringLiteral("Lua 表达式或 :help"));
    connect(m_in, &QLineEdit::returnPressed, this, &ConsolePanel::submit);

    auto *split = new QSplitter(Qt::Vertical, this);
    split->addWidget(m_out);
    split->addWidget(m_table);
    split->addWidget(m_plot);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 2);
    split->setStretchFactor(2, 3);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);
    lay->addWidget(split, 1);
    lay->addWidget(m_in);

    append(m_console->help());
    m_in->setFocus();
}

ConsolePanel::~ConsolePanel()
{
    delete m_console;
}

void ConsolePanel::append(const QString &text, bool error)
{
    const QString safe = text.toHtmlEscaped().replace(QLatin1String("\n"), QLatin1String("<br>"));
    m_out->append(QStringLiteral("<span style=\"color:%1\">%2</span>")
                      .arg(error ? QStringLiteral("#ff7b72") : QStringLiteral("#d4d4d4"), safe));
}

void ConsolePanel::submit()
{
    const QString code = m_in->text();
    if (code.isEmpty())
        return;
    m_in->clear();
    m_history << code;
    m_historyPos = m_history.size();

    append(QStringLiteral("> ") + code);
    const auto r = m_console->eval(code);
    switch (r.kind)
    {
    case ScriptConsole::Result::Empty:
        break;
    case ScriptConsole::Result::Clear:
        m_out->clear();
        break;
    case ScriptConsole::Result::Error:
        append(r.text, true);
        break;
    case ScriptConsole::Result::Output:
        append(r.text);
        break;
    }
}

ps::components::DockContribution ConsolePanel::dockContribution()
{
    return ps::components::DockContribution{
        QStringLiteral("dock.console"), QStringLiteral("脚本控制台"), Qt::BottomDockWidgetArea, true,
        20, QString(),
        [](QWidget *parent) { return new ConsolePanel(parent); }};
}

} // namespace ps::scripting
