// 控制台面板：ps.plot / ps.table 的表结构解析与渲染不崩。

#include "scripting/ConsolePanel.h"
#include "scripting/ScratchPlot.h"
#include "scripting/ScriptConsole.h"

#include <QApplication>
#include <QString>
#include <QTableWidget>

#include <cmath>
#include <cstdio>

using ps::scripting::ConsolePanel;
using ps::scripting::ScriptConsole;

namespace
{
int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

ScriptConsole::Result eval(ConsolePanel &p, const QString &code)
{
    return p.console()->eval(code);
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    ConsolePanel panel;

    // 图表入口存在
    check(eval(panel, QStringLiteral("type(ps.plot)")).text.contains(QLatin1String("function")),
          "ps.plot 已注册");
    check(eval(panel, QStringLiteral("type(ps.table)")).text.contains(QLatin1String("function")),
          "ps.table 已注册");

    // 1024 点 10 Hz 正弦 → FFT → 临时图
    eval(panel, QStringLiteral("w = {} for i=0,1023 do w[i+1] = math.sin(2*math.pi*10*i/1024) end"));
    eval(panel, QStringLiteral("spec = ps.fft(w, {fs=1024})"));
    const auto plotted = eval(panel, QStringLiteral("ps.plot(spec, {title='fft'})"));
    check(plotted.kind != ScriptConsole::Result::Error, "ps.plot 不报错");

    // 强制走一遍 paintEvent，确认自绘路径不崩
    const QPixmap shot = panel.grab();
    check(!shot.isNull(), "面板可渲染");

    // ps.table：列式 {t,v} → 两列
    eval(panel, QStringLiteral("d = ps.decimate(w, 8)"));
    eval(panel, QStringLiteral("ps.table(d, {title='dec'})"));
    auto *table = panel.findChild<QTableWidget *>();
    check(table != nullptr, "结果表存在");
    if (table)
    {
        check(table->columnCount() == 2, "列式表 2 列");
        check(table->rowCount() == 128, "decimate 后 128 行");
    }

    // 清空
    check(eval(panel, QStringLiteral("ps.clearResults()")).kind != ScriptConsole::Result::Error,
          "ps.clearResults 可用");

    if (failures == 0)
        std::printf("console panel ok\n");
    return failures == 0 ? 0 : 1;
}
