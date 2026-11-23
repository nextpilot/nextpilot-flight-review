// ps_command 验收：when 语法白名单 / 编译缓存 / 三层作用域 / 键位冲突。
//
// 对应契约 IKXGYH 的验收基线 13：
//   · 白名单语法（! / in / not in / =~ / <）各一条用例
//   · 同一 when 串启动期只解析 1 次（执行 100 次计数不变）
//   · 三层作用域：View 层键覆盖 Shell 层同名键

#include "command/CommandRegistry.h"
#include "command/ContextStack.h"
#include "command/KeybindingRegistry.h"
#include "command/WhenExpression.h"

#include <QString>
#include <QVariant>

#include <cstdio>

using namespace nextpilot::plotscope::command;

static int g_failed = 0;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                            \
            ++g_failed;                                                                            \
        }                                                                                          \
    } while (false)

static ContextStack makeContext()
{
    ContextStack ctx;
    ctx.set(ContextScope::Shell, QStringLiteral("selection.kind"), QStringLiteral("series"));
    ctx.set(ContextScope::Shell, QStringLiteral("document.kind"), QStringLiteral("figure"));
    ctx.set(ContextScope::Shell, QStringLiteral("dataset.count"), 3);
    ctx.set(ContextScope::Shell, QStringLiteral("region.active"), QStringLiteral("editor"));
    ctx.set(ContextScope::Shell, QStringLiteral("busy"), false);
    ctx.set(ContextScope::Panel, QStringLiteral("view.active"), QStringLiteral("plot.canvas"));
    return ctx;
}

static bool evalWith(const QString &source, const ContextStack &ctx)
{
    QString error;
    return WhenExpression::compile(source, &error).evaluate(ctx);
}

static void testGrammar()
{
    const ContextStack ctx = makeContext();

    // 原有能力
    CHECK(evalWith(QStringLiteral("selection.kind == \"series\""), ctx));
    CHECK(!evalWith(QStringLiteral("selection.kind != \"series\""), ctx));
    CHECK(evalWith(QStringLiteral("selection.kind == \"series\" && document.kind == \"figure\""), ctx));
    CHECK(evalWith(QStringLiteral("selection.kind == \"axes\" || region.active == \"editor\""), ctx));
    CHECK(evalWith(QStringLiteral("(selection.kind == \"axes\" || region.active == \"editor\")"), ctx));

    // ! 绑定最紧
    CHECK(evalWith(QStringLiteral("!busy"), ctx));
    CHECK(!evalWith(QStringLiteral("!selection.kind == \"series\""), ctx));
    CHECK(evalWith(QStringLiteral("!(selection.kind == \"series\") || true"), ctx));

    // in / not in
    CHECK(evalWith(QStringLiteral("selection.kind in (\"series\", \"axes\")"), ctx));
    CHECK(!evalWith(QStringLiteral("selection.kind in (\"axes\", \"table\")"), ctx));
    CHECK(evalWith(QStringLiteral("selection.kind not in (\"axes\", \"table\")"), ctx));

    // 数值比较
    CHECK(evalWith(QStringLiteral("dataset.count > 1"), ctx));
    CHECK(evalWith(QStringLiteral("dataset.count >= 3"), ctx));
    CHECK(!evalWith(QStringLiteral("dataset.count < 2"), ctx));

    // 正则
    CHECK(evalWith(QStringLiteral("selection.kind =~ \"ser.*\""), ctx));
    CHECK(!evalWith(QStringLiteral("selection.kind =~ \"axes.*\""), ctx));
    CHECK(evalWith(QStringLiteral("selection.kind not =~ \"axes.*\""), ctx));

    // 未定义键求值为假（不报错）
    CHECK(!evalWith(QStringLiteral("no.such.key"), ctx));
    CHECK(!evalWith(QStringLiteral("no.such.key == \"x\""), ctx));

    // 解析失败折成恒假 + 带 error
    QString error;
    const WhenExpression broken = WhenExpression::compile(QStringLiteral("selection.kind =="), &error);
    CHECK(!broken.isValid());
    CHECK(!error.isEmpty());
    CHECK(!broken.evaluate(ctx));

    // 空表达式恒真
    const WhenExpression empty = WhenExpression::compile(QString());
    CHECK(empty.isEmpty());
    CHECK(empty.evaluate(ctx));
}

static void testScopeOverride()
{
    ContextStack ctx = makeContext();
    ctx.set(ContextScope::View, QStringLiteral("region.active"), QStringLiteral("panel"));
    CHECK(ctx.value(QStringLiteral("region.active")).toString() == QStringLiteral("panel"));
    ctx.clear(ContextScope::View);
    CHECK(ctx.value(QStringLiteral("region.active")).toString() == QStringLiteral("editor"));

    // 内层覆盖后 when 结论随之改变
    CHECK(evalWith(QStringLiteral("region.active == \"editor\""), ctx));
    ctx.set(ContextScope::View, QStringLiteral("region.active"), QStringLiteral("panel"));
    CHECK(!evalWith(QStringLiteral("region.active == \"editor\""), ctx));
}

static void testCache()
{
    CommandRegistry registry;
    int hits = 0;

    const QString when = QStringLiteral("selection.kind == \"series\" && dataset.count > 0");
    for (int i = 0; i < 5; ++i)
    {
        CommandDescriptor command;
        command.id = QStringLiteral("plot.cursor.%1").arg(i);
        command.title = QStringLiteral("游标动作 %1").arg(i);
        command.when = when;                       // 同一串：只应解析一次
        command.handler = [&hits]() { ++hits; };
        CHECK(registry.add(command));
    }
    CHECK(registry.whenParseCount() == 1);
    CHECK(registry.whenCacheSize() == 1);

    registry.setContextValue(QStringLiteral("selection.kind"), QStringLiteral("series"));
    registry.setContextValue(QStringLiteral("dataset.count"), 3);

    const quint64 before = registry.whenParseCount();
    for (int i = 0; i < 100; ++i)
        CHECK(registry.execute(QStringLiteral("plot.cursor.0")));
    CHECK(hits == 100);
    CHECK(registry.whenParseCount() == before);   // 执行不再解析

    // 上下文变化即生效，不需要重新解析
    registry.setContextValue(ContextScope::View, QStringLiteral("selection.kind"), QStringLiteral("axes"));
    QString reason;
    CHECK(!registry.isAvailable(QStringLiteral("plot.cursor.0"), &reason));
    CHECK(!reason.isEmpty());
    CHECK(registry.whenParseCount() == before);

    // 不同串各解析一次
    CommandDescriptor other;
    other.id = QStringLiteral("plot.other");
    other.title = QStringLiteral("别的动作");
    other.when = QStringLiteral("document.kind == \"script\"");
    other.handler = []() {};
    CHECK(registry.add(other));
    CHECK(registry.whenParseCount() == before + 1);
}

static void testKeybindings()
{
    KeybindingRegistry bindings;
    bindings.setDefault(QStringLiteral("file.save"), QStringLiteral("Ctrl+S"));
    bindings.setDefault(QStringLiteral("file.export"), QStringLiteral("Ctrl+S"));   // 故意冲突
    bindings.setDefault(QStringLiteral("view.palette"), QStringLiteral("Ctrl+K, Ctrl+P"));

    CHECK(bindings.sequenceFor(QStringLiteral("file.save")) == QStringLiteral("Ctrl+S"));

    // 冲突检测：改键之前两条命令抢同一个序列。
    {
        const QList<KeybindingConflict> conflicts = bindings.conflicts();
        CHECK(conflicts.size() == 1);
        if (!conflicts.isEmpty())
        {
            CHECK(conflicts.front().sequence == QStringLiteral("Ctrl+S"));
            CHECK(conflicts.front().commandIds.size() == 2);
        }
    }

    // 覆盖后冲突消失，来源是用户覆盖而非默认。
    CHECK(bindings.setOverride(QStringLiteral("file.save"), QStringLiteral("Ctrl+Shift+S")));
    CHECK(bindings.sequenceFor(QStringLiteral("file.save")) == QStringLiteral("Ctrl+Shift+S"));
    CHECK(bindings.conflicts().isEmpty());

    CHECK(KeybindingRegistry::isChord(bindings.sequenceFor(QStringLiteral("view.palette"))));
    CHECK(KeybindingRegistry::chordSteps(QStringLiteral("Ctrl+K, Ctrl+P")).size() == 2);
    CHECK(KeybindingRegistry::normalize(QStringLiteral("ctrl+s")) == QStringLiteral("Ctrl+S"));
}

int main()
{
    testGrammar();
    testScopeOverride();
    testCache();
    testKeybindings();

    if (g_failed == 0)
        std::printf("ps_command: all checks passed\n");
    return g_failed == 0 ? 0 : 1;
}
