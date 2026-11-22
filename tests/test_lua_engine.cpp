// M2.1-2.1.1：LuaEngine REPL 求值 + 输出 sink 验收（headless）。
// evaluate 表达式/语句/共享全局状态/错误行号；print 与 app.log 进 sink。
#include "LuaEngine.h"
#include "ScriptFacade.h"
#include "IIntentExecutor.h"
#include "Result.h"

#include <QCoreApplication>
#include <QStringList>
#include <QTimer>
#include <QVariant>

#include <cstdio>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::context;
using namespace nextpilot::plotscope::script;

namespace
{
int g_fail = 0;
#define CHECK(cond)                                                                     \
    do                                                                                  \
    {                                                                                   \
        if (!(cond))                                                                    \
        {                                                                               \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
            ++g_fail;                                                                   \
        }                                                                               \
    } while (0)

// 最小执行器：记下收到的 op，回显固定值。
class EchoExecutor : public IIntentExecutor
{
public:
    Result<IntentResult> execute(const Intent &intent) override
    {
        lastOp = intent.op;
        IntentResult out;
        out.payload = QStringLiteral("ok");
        return out;
    }
    QString lastOp;
};

QStringList g_sinkLines;
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    EchoExecutor executor;
    ScriptFacade facade(executor, nullptr, {});
    LuaEngine lua(facade);

    // --- 1. 输出 sink：print 与 app.log 都进 sink，不再走 stdout ---
    lua.setOutputSink([](const QString &msg) { g_sinkLines << msg; });
    CHECK(lua.runString("print(\"hello\", 42)").ok());
    CHECK(lua.runString("app.log(\"from log\")").ok());
    CHECK(g_sinkLines.size() == 2);
    CHECK(g_sinkLines.at(0) == QStringLiteral("hello\t42"));
    CHECK(g_sinkLines.at(1) == QStringLiteral("from log"));

    // --- 2. evaluate 表达式：返回值转 QVariant ---
    const Result<QVariant> two = lua.evaluate(QStringLiteral("2^10"));
    CHECK(two.ok());
    CHECK(two.value().toDouble() == 1024.0);

    const Result<QVariant> str = lua.evaluate(QStringLiteral("'abc' .. 'd'"));
    CHECK(str.ok());
    CHECK(str.value().toString() == QStringLiteral("abcd"));

    // --- 3. 连续求值共享全局状态（持久 VM） ---
    CHECK(lua.evaluate("acc = 0").ok());
    CHECK(lua.evaluate("acc = acc + 7").ok());
    const Result<QVariant> acc = lua.evaluate("acc * 2");
    CHECK(acc.ok());
    CHECK(acc.value().toDouble() == 14.0);

    // --- 4. 语句降级：`return (...)` 编译不过时按整段语句执行 ---
    CHECK(lua.evaluate("x = 3\ny = x * 2").ok());
    const Result<QVariant> y = lua.evaluate("y");
    CHECK(y.ok());
    CHECK(y.value().toDouble() == 6.0);

    // --- 5. 语法错误带 repl 行号前缀 ---
    const Result<QVariant> bad = lua.evaluate("if then end");
    CHECK(!bad.ok());
    CHECK(bad.error().code == QStringLiteral("esyntax"));
    CHECK(bad.error().message.contains(QLatin1String("repl:")));

    // --- 6. 运行时错误带行号 ---
    const Result<QVariant> rt = lua.evaluate("error('boom')");
    CHECK(!rt.ok());
    CHECK(rt.error().code == QStringLiteral("eruntime"));
    CHECK(rt.error().message.contains(QLatin1String("boom")));

    // --- 7. evaluate 与 runString 共用同一 VM ---
    CHECK(lua.runString("shared = 99").ok());
    const Result<QVariant> shared = lua.evaluate("shared + 1");
    CHECK(shared.ok());
    CHECK(shared.value().toDouble() == 100.0);

    // --- 8. sink 可 unset 回退：无 sink 时 print 不崩（stdout 路径） ---
    LuaEngine lua2(facade); // 不设 sink
    CHECK(lua2.runString("print(\"legacy path\")").ok());

    // ===== 调试器 v1（2.1.7-2.1.8 验收）=====
    // 注：Lua 5.4 行钩子停的是"下一条指令所在行"；`local function` 的 CLOSURE
    // 指令挂在函数体结束行，所以该 chunk 的入口暂停行是 L3（标准调试器语义）。
    const char *dbgSrc =
        "local function f(x)\n" // L1
        "    return x * 2\n"    // L2
        "end\n"                 // L3
        "local a = f(3)\n"      // L4
        "local b = a + 1\n"     // L5
        "g = b * 10\n"          // L6
        "print(b)\n";           // L7

    // --- 9. 断点命中：暂停在 L6 → 收到行号/局部变量/调用栈快照 → 继续后执行完成 ---
    {
        LuaEngine dbg(facade);
        dbg.setOutputSink([](const QString &) {});
        QVector<LuaEngine::DebugPause> pauses;
        const Result<void> r = dbg.runStringDebug(
            dbgSrc, "=dbg", {6},
            [&](const LuaEngine::DebugPause &p) {
                pauses << p;
                QTimer::singleShot(0, [&dbg]() { dbg.debugContinue(); });
            },
            false); // 纯断点模式，不做入口暂停
        CHECK(r.ok());
        CHECK(pauses.size() == 1);
        CHECK(pauses.at(0).line == 6);
        CHECK(pauses.at(0).reason == QStringLiteral("breakpoint"));
        CHECK(pauses.at(0).locals.contains(QStringLiteral("b")));
        CHECK(pauses.at(0).locals.value(QStringLiteral("b")).toDouble() == 7.0);
        CHECK(!pauses.at(0).stack.isEmpty());
        CHECK(pauses.at(0).stack.at(0).contains(QLatin1String("dbg")));
        const Result<QVariant> g = dbg.evaluate("g");
        CHECK(g.ok());
        CHECK(g.value().toDouble() == 70.0);
    }

    // --- 10. 跳过（stepOver）：无函数的纯顺序 chunk，入口 L1 → 下一行 L2 ---
    {
        const char *seq = "local a = 0\n" // L1 入口停这（尚未执行，a 未定义）
                          "local b = a + 1\n" // L2
                          "local c = b + 1\n" // L3
                          "print(c)\n";       // L4
        LuaEngine dbg(facade);
        dbg.setOutputSink([](const QString &) {});
        QVector<LuaEngine::DebugPause> pauses;
        dbg.runStringDebug(seq, "=seq", {},
                           [&](const LuaEngine::DebugPause &p) {
                               pauses << p;
                               if (pauses.size() == 1)
                                   QTimer::singleShot(0, [&dbg]() { dbg.debugStepOver(); });
                               else
                                   QTimer::singleShot(0, [&dbg]() { dbg.debugContinue(); });
                           });
        CHECK(pauses.size() == 2);
        CHECK(pauses.at(0).reason == QStringLiteral("entry"));
        CHECK(pauses.at(0).line == 1);
        CHECK(pauses.at(1).line == 2);
        CHECK(pauses.at(1).locals.contains(QStringLiteral("a")));
        CHECK(pauses.at(1).locals.value(QStringLiteral("a")).toDouble() == 0.0);
    }

    // --- 11. 进入（stepIn）：入口 L3 → L4（CALL 之前的行事件）→ 跟进函数体 L2 ---
    {
        LuaEngine dbg(facade);
        dbg.setOutputSink([](const QString &) {});
        QVector<LuaEngine::DebugPause> pauses;
        dbg.runStringDebug(dbgSrc, "=dbg", {},
                           [&](const LuaEngine::DebugPause &p) {
                               pauses << p;
                               if (pauses.size() <= 2)
                                   QTimer::singleShot(0, [&dbg]() { dbg.debugStepIn(); });
                               else
                                   QTimer::singleShot(0, [&dbg]() { dbg.debugContinue(); });
                           });
        CHECK(pauses.size() == 3);
        CHECK(pauses.at(0).reason == QStringLiteral("entry"));
        CHECK(pauses.at(0).line == 3); // 首条指令（CLOSURE）所在行
        CHECK(pauses.at(1).line == 4); // CALL f 执行前的行事件（进入=任意行都停）
        CHECK(pauses.at(2).line == 2); // 跟进到 f 内部
        CHECK(pauses.at(2).locals.contains(QStringLiteral("x")));
        CHECK(pauses.at(2).locals.value(QStringLiteral("x")).toDouble() == 3.0);
        CHECK(pauses.at(2).stack.size() >= 2); // f 帧 + 主 chunk 帧
    }

    // --- 12. 跳出（stepOut）：进入函数体后跳出，深度回退停在调用方后续行 ---
    {
        LuaEngine dbg(facade);
        dbg.setOutputSink([](const QString &) {});
        QVector<LuaEngine::DebugPause> pauses;
        dbg.runStringDebug(dbgSrc, "=dbg", {},
                           [&](const LuaEngine::DebugPause &p) {
                               pauses << p;
                               if (pauses.size() <= 2)
                                   QTimer::singleShot(0, [&dbg]() { dbg.debugStepIn(); });
                               else if (pauses.size() == 3)
                                   QTimer::singleShot(0, [&dbg]() { dbg.debugStepOut(); });
                               else
                                   QTimer::singleShot(0, [&dbg]() { dbg.debugContinue(); });
                           });
        CHECK(pauses.size() == 4);
        CHECK(pauses.at(2).line == 2); // f 内部（深度 2）
        CHECK(pauses.at(3).line >= 4); // 深度回退到主 chunk（f 返回后的下一行事件）
        CHECK(pauses.at(3).locals.contains(QStringLiteral("a"))); // 已拿到 f 的返回值
        CHECK(pauses.at(3).locals.value(QStringLiteral("a")).toDouble() == 6.0);
    }

    // --- 13. 停止：暂停期请求停止 → runStringDebug 返回失败，VM 可复用 ---
    {
        LuaEngine dbg(facade);
        dbg.setOutputSink([](const QString &) {});
        bool paused = false;
        const Result<void> r = dbg.runStringDebug(
            dbgSrc, "=dbg", {}, [&](const LuaEngine::DebugPause &) {
                paused = true;
                QTimer::singleShot(0, [&dbg]() { dbg.debugStop(); });
            });
        CHECK(paused);
        CHECK(!r.ok());
        CHECK(r.error().message.contains(QStringLiteral("停止")));
        // 中断后的 VM 仍然可用（evaluate 共享全局状态不受影响）
        const Result<QVariant> ok = dbg.evaluate("1+1");
        CHECK(ok.ok());
        CHECK(ok.value().toDouble() == 2.0);
    }

    // --- 14. 暂停期 REPL：debugEvaluate 在新协程求值（共享全局表） ---
    {
        LuaEngine dbg(facade);
        dbg.setOutputSink([](const QString &) {});
        // 非暂停期调用必须明确失败
        const Result<QVariant> off = dbg.debugEvaluate("1");
        CHECK(!off.ok());
        QVector<LuaEngine::DebugPause> pauses;
        dbg.runStringDebug(dbgSrc, "=dbg", {},
                           [&](const LuaEngine::DebugPause &p) {
                               pauses << p;
                               if (pauses.size() == 1)
                               {
                                   const Result<QVariant> v = dbg.debugEvaluate("1+1");
                                   CHECK(v.ok());
                                   CHECK(v.value().toDouble() == 2.0);
                                   const Result<QVariant> w = dbg.debugEvaluate("gwx = 41 + 1");
                                   CHECK(w.ok());
                                   QTimer::singleShot(0, [&dbg]() { dbg.debugContinue(); });
                               }
                               else
                                   QTimer::singleShot(0, [&dbg]() { dbg.debugContinue(); });
                           });
        CHECK(!pauses.isEmpty());
        // 暂停期写入的全局变量在恢复后仍然存在（同一 VM）
        const Result<QVariant> g = dbg.evaluate("gwx");
        CHECK(g.ok());
        CHECK(g.value().toDouble() == 42.0);
    }

    if (g_fail == 0)
        std::printf("test_lua_engine: ALL PASS\n");
    else
        std::fprintf(stderr, "test_lua_engine: %d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
