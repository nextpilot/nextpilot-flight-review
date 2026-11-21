// M2.1-2.1.1：LuaEngine REPL 求值 + 输出 sink 验收（headless）。
// evaluate 表达式/语句/共享全局状态/错误行号；print 与 app.log 进 sink。
#include "LuaEngine.h"
#include "ScriptFacade.h"
#include "IIntentExecutor.h"
#include "Result.h"

#include <QCoreApplication>
#include <QStringList>
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

    if (g_fail == 0)
        std::printf("test_lua_engine: ALL PASS\n");
    else
        std::fprintf(stderr, "test_lua_engine: %d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
