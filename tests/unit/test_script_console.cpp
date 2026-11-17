// 控制台语义：表达式回显、ans、多行续行、元命令、错误定位、print 捕获。
#include "scripting/ScriptConsole.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <cassert>
#include <cstdio>

using ps::scripting::ScriptConsole;

static int failures = 0;

static void check(bool cond, const char *what)
{
    if (!cond)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main()
{
    ScriptConsole con;

    // 表达式回显
    auto r = con.eval(QStringLiteral("1+2"));
    check(r.kind == ScriptConsole::Result::Output, "1+2 应有输出");
    check(r.text == QLatin1String("3"), "1+2 应为 3");

    // 小数不被截成整数
    r = con.eval(QStringLiteral("1/3"));
    check(r.text.startsWith(QLatin1String("0.333")), "1/3 应保留小数");

    // 赋值静默，取值回显
    r = con.eval(QStringLiteral("x = 5"));
    check(r.kind == ScriptConsole::Result::Empty, "赋值不应有输出");
    r = con.eval(QStringLiteral("x"));
    check(r.text == QLatin1String("5"), "x 应为 5");

    // ans 承接上一个表达式
    r = con.eval(QStringLiteral("x * 2"));
    check(r.text == QLatin1String("10"), "x*2 应为 10");
    r = con.eval(QStringLiteral("ans + 1"));
    check(r.text == QLatin1String("11"), "ans+1 应为 11");

    // 字符串带引号
    r = con.eval(QStringLiteral("'hi'"));
    check(r.text == QLatin1String("\"hi\""), "字符串应带引号");

    // 数组描述
    r = con.eval(QStringLiteral("{1,2,3}"));
    check(r.text.contains(QLatin1String("[3]")), "数组应显示长度");

    // 布尔与 nil
    r = con.eval(QStringLiteral("1 > 0"));
    check(r.text == QLatin1String("true"), "比较应为 true");
    r = con.eval(QStringLiteral("nosuch"));
    check(r.text == QLatin1String("nil"), "未定义全局应为 nil");

    // print 捕获
    r = con.eval(QStringLiteral("print('a', 1)"));
    check(r.kind == ScriptConsole::Result::Output, "print 应有输出");
    check(r.text == QLatin1String("a\t1"), "print 用 tab 连接");
    check(!r.text.contains(QLatin1String("console")), "print 不应带 chunk 前缀");

    // 多行续行
    check(con.incomplete(QStringLiteral("function f(a)")), "未闭合的 function 应判为续行");
    check(con.incomplete(QStringLiteral("if true then")), "未闭合的 if 应判为续行");
    check(!con.incomplete(QStringLiteral("x = 1")), "完整语句不应续行");
    r = con.eval(QStringLiteral("function f(a)\n  return a * 2\nend"));
    check(r.kind == ScriptConsole::Result::Empty, "函数定义不应有输出");
    r = con.eval(QStringLiteral("f(3)"));
    check(r.text == QLatin1String("6"), "f(3) 应为 6");

    // 错误定位：带行号，且不执行后续
    r = con.eval(QStringLiteral("local a = 1\nboom()"));
    check(r.kind == ScriptConsole::Result::Error, "运行期错误应报错");
    check(r.text.contains(QStringLiteral("行 2")) || r.text.contains(QLatin1String("console:2")),
          "错误应指向第 2 行");
    r = con.eval(QStringLiteral("x = "));
    check(r.kind == ScriptConsole::Result::Error, "语法错应报错");

    // :who / :whos
    r = con.eval(QStringLiteral(":who"));
    check(r.text.contains(QLatin1String("x")), ":who 应包含 x");
    check(r.text.contains(QLatin1String("f")), ":who 应包含 f");
    r = con.eval(QStringLiteral(":whos"));
    check(r.text.contains(QLatin1String("5")), ":whos 应显示值");

    // :help 有内容
    r = con.eval(QStringLiteral(":help"));
    check(r.text.contains(QLatin1String("ps.app")), ":help 应提到数据面");

    // :clc 是清屏标记
    r = con.eval(QStringLiteral(":clc"));
    check(r.kind == ScriptConsole::Result::Clear, ":clc 应返回 Clear");

    // :clear 重建 workspace
    r = con.eval(QStringLiteral(":clear"));
    check(r.kind == ScriptConsole::Result::Output, ":clear 应有提示");
    r = con.eval(QStringLiteral("x"));
    check(r.text == QLatin1String("nil"), ":clear 后 x 应为 nil");

    // :run 保留变量
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("mod.lua"));
        QFile f(path);
        assert(f.open(QIODevice::WriteOnly));
        QTextStream out(&f);
        out << "y = 42\nprint('from file')\n";
        f.close();

        r = con.eval(QStringLiteral(":run ") + path);
        check(r.kind == ScriptConsole::Result::Output, ":run 应有输出");
        check(r.text.contains(QLatin1String("from file")), ":run 应显示 print");
        r = con.eval(QStringLiteral("y"));
        check(r.text == QLatin1String("42"), ":run 后变量应留在 workspace");

        // 文件里的错误要带文件路径与行号
        QFile bad(dir.filePath(QStringLiteral("bad.lua")));
        assert(bad.open(QIODevice::WriteOnly));
        QTextStream(&bad) << "local q = 1\nq()\n";
        bad.close();
        r = con.eval(QStringLiteral(":run ") + dir.filePath(QStringLiteral("bad.lua")));
        check(r.kind == ScriptConsole::Result::Error, "坏文件应报错");
        check(r.text.contains(QLatin1String("bad.lua")), "错误应带文件路径");
        check(r.text.contains(QLatin1String("2")), "错误应带行号");
    }

    // 未知元命令
    r = con.eval(QStringLiteral(":nope"));
    check(r.kind == ScriptConsole::Result::Error, "未知元命令应报错");

    // ps.gc 可用
    r = con.eval(QStringLiteral("type(ps.gc)"));
    check(r.text == QLatin1String("\"function\"") || r.text == QLatin1String("function"),
          "ps.gc 应为函数");

    if (failures == 0)
        std::printf("script console ok\n");
    return failures == 0 ? 0 : 1;
}
