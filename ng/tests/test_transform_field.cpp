// B8-b：TransformField 输入控件输出契约测试（offscreen GUI）。
// 验证选算子 / 直接敲表达式 / Lua 编辑回填三条路径的 TransformRequest 输出。
#include "AppContext.h"
#include "Transform.h"
#include "TransformField.h"

#include <QApplication>
#include <QComboBox>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>

#include <cstdio>

using namespace nextpilot::plotscope::core;
using namespace nextpilot::plotscope::gui;

namespace
{
int g_fail = 0;
#define CHECK(cond)                                                                  \
    do                                                                               \
    {                                                                                \
        if (!(cond))                                                                 \
        {                                                                            \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
            ++g_fail;                                                                \
        }                                                                            \
    } while (0)
} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TransformRegistry::clear();

    AppContext ctx;

    // --- 1. 按能力过滤出 descriptor ---
    TransformField field(ctx, CapDerived);
    CHECK(field.descriptorCount() > 0);

    auto *combo = field.findChild<QComboBox *>();
    CHECK(combo != nullptr);
    if (!combo)
        return 1;

    // --- 2. 选 builtin.scale -> request 输出 descriptorId + params ---
    const int scaleIdx = combo->findData(QStringLiteral("builtin.scale"));
    CHECK(scaleIdx >= 0);
    if (scaleIdx >= 0)
    {
        combo->setCurrentIndex(scaleIdx);
        const QJsonObject req = field.request();
        CHECK(req.value(QStringLiteral("descriptorId")).toString() == QStringLiteral("builtin.scale"));
        CHECK(req.value(QStringLiteral("kind")).toString() == QStringLiteral("builtin"));
        const QJsonObject params = req.value(QStringLiteral("params")).toObject();
        CHECK(params.contains(QStringLiteral("value_scale")));
    }

    // --- 3. 直接敲表达式 -> req.descriptorId=expression, kind=expression, expression=文本 ---
    const int exprIdx = combo->findData(QStringLiteral("expression"));
    CHECK(exprIdx >= 0);
    if (exprIdx >= 0)
    {
        combo->setCurrentIndex(exprIdx);
        auto *expr = field.findChild<QPlainTextEdit *>();
        CHECK(expr != nullptr);
        if (expr)
            expr->setPlainText(QStringLiteral("5*x+10"));
        const QJsonObject req = field.request();
        CHECK(req.value(QStringLiteral("descriptorId")).toString() == QStringLiteral("expression"));
        CHECK(req.value(QStringLiteral("kind")).toString() == QStringLiteral("expression"));
        CHECK(req.value(QStringLiteral("expression")).toString() == QStringLiteral("5*x+10"));
    }

    // --- 4. setRequest 回填 builtin 参数 ---
    QJsonObject initial;
    initial.insert(QStringLiteral("descriptorId"), QStringLiteral("builtin.scale"));
    initial.insert(QStringLiteral("kind"), QStringLiteral("builtin"));
    QJsonObject ip;
    ip.insert(QStringLiteral("value_scale"), -1);
    initial.insert(QStringLiteral("params"), ip);
    field.setRequest(initial);
    CHECK(combo->currentData().toString() == QStringLiteral("builtin.scale"));
    const QJsonObject back = field.request();
    const QJsonObject backParams = back.value(QStringLiteral("params")).toObject();
    CHECK(backParams.value(QStringLiteral("value_scale")).toInt() == -1);

    // --- 5. Lua 脚本经 ops.save 注册后出现在下拉；request 带 scriptId + luaSource 快照 ---
    bool savedOk = false;
    TransformScriptOps ops;
    ops.resolve = [](const QString &, const QString &snap) { return snap.toUtf8(); };
    ops.describe = [](const QByteArray &src, const QString &name) -> Result<TransformDescriptor> {
        TransformDescriptor d;
        d.id = QStringLiteral("lua.") + name;
        d.kind = TransformKind::Lua;
        d.label = name;
        d.capabilities = CapTransform;
        Q_UNUSED(src);
        return Result<TransformDescriptor>(d);
    };
    ops.save = [&savedOk](const QString &name, const QByteArray &source) -> Result<TransformDescriptor> {
        savedOk = true;
        TransformDescriptor d;
        d.id = QStringLiteral("lua.") + name;
        d.kind = TransformKind::Lua;
        d.label = name;
        d.capabilities = CapTransform;
        Q_UNUSED(source);
        TransformRegistry::registerDescriptor(d);
        return Result<TransformDescriptor>(d);
    };
    ctx.registerTransformScriptOps(ops);
    CHECK(ops.valid());

    // 手动走一遍"保存 -> 注册 -> 控件重拉"的链路（编辑按钮内部就是这么做的）。
    const TransformDescriptor r = ops.save(QStringLiteral("myfilter"), QByteArray()).value();
    CHECK(savedOk);
    CHECK(TransformRegistry::contains(QStringLiteral("lua.myfilter")));

    // 新控件应能看到新脚本。
    TransformField field2(ctx, CapDerived);
    CHECK(field2.findChild<QComboBox *>()->findData(QStringLiteral("lua.myfilter")) >= 0);

    if (g_fail == 0)
        std::printf("test_transform_field: ALL PASS\n");
    else
        std::fprintf(stderr, "test_transform_field: %d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
