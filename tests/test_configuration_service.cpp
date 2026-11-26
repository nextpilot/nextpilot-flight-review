// IConfigurationService headless 单测：
//   1) 分层覆盖合并（document ← workspace ← user ← default）+ scope 决定参与层
//   2) reset 单键复位（其余键不受影响）
//   3) owner unmount 后 effectiveValue 回落（Undefined）+ 残留诊断（不删用户数据）
//   4) null 是合法值；pattern 非法值不写入 + 诊断
// 只链 ps_assembly（其头文件内联 adapter）+ Qt6::Core；不碰壳、不碰 GUI。
// 持久化只走 SettingsStore（内存态）；QSettings 在自有代码必须为 0。

#include "WorkbenchAdapters.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonValue>
#include <QString>

#include <cstdio>

using namespace nextpilot::plotscope;
using namespace nextpilot::plotscope::registry;

namespace {
int g_failed = 0;
#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);                                                        \
            ++g_failed;                                                                                                \
        }                                                                                                              \
    } while (0)

ContributesDoc doc(const char *json, const QString &owner)
{
    auto obj = QJsonDocument::fromJson(QByteArray(json)).object();
    return registry::parseContributes(obj, owner).doc; // 测试输入均合法
}

bool hasDiagFor(const QList<workbench::IConfigurationService::Diagnostic> &diags, const QString &key,
                const QString &needle)
{
    for (const auto &d : diags)
        if (d.key == key && d.message.contains(needle))
            return true;
    return false;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // 内存态 SettingsStore（空路径）+ 唯一扩展注册表——AppContext 组合根同款。
    context::SettingsStore settings;
    RegistryTree tree;
    registry::ToolboxHub toolbox(tree);
    assembly::ConfigurationServiceAdapter svc(settings, toolbox);

    // 声明两个键：lineWidth（number, default=2）与 theme.accent（string, default="blue"）。
    CHECK(toolbox.mountContribution(doc(R"({
        "configuration": [{"title":"绘图","order":10,
            "properties":{
                "plot.lineWidth":{"type":"number","default":2,"scope":"resource","order":1},
                "plot.accent":{"type":"string","default":"blue","scope":"resource","order":2}
            }}]
    })",
                                        QStringLiteral("plot"))));
    CHECK(svc.configurations().size() == 1);
    CHECK(svc.configurations().at(0).properties.size() == 2);

    // ===== 判据 1：分层覆盖合并 =====
    // 默认值生效。
    CHECK(svc.effectiveValue(QStringLiteral("plot.lineWidth")).toDouble() == 2.0);
    // user 层覆盖默认值。
    svc.setUserValue(QStringLiteral("plot.lineWidth"), QJsonValue(5.0));
    CHECK(svc.effectiveValue(QStringLiteral("plot.lineWidth")).toDouble() == 5.0);
    // workspace 覆盖 user。
    svc.setWorkspaceValue(QStringLiteral("plot.lineWidth"), QJsonValue(7.0));
    CHECK(svc.effectiveValue(QStringLiteral("plot.lineWidth")).toDouble() == 7.0);
    // document 覆盖 workspace。
    svc.setDocumentValue(QStringLiteral("plot.lineWidth"), QJsonValue(9.0));
    CHECK(svc.effectiveValue(QStringLiteral("plot.lineWidth")).toDouble() == 9.0);
    // inspect：每层来源 + 默认值 + 声明者。
    {
        const auto ins = svc.inspect(QStringLiteral("plot.lineWidth"));
        CHECK(ins.declared);
        CHECK(ins.declaredOwner == QLatin1String("plot"));
        CHECK(ins.user.set && ins.user.value.toDouble() == 5.0);
        CHECK(ins.workspace.set && ins.workspace.value.toDouble() == 7.0);
        CHECK(ins.document.set && ins.document.value.toDouble() == 9.0);
        CHECK(ins.defaultValue.toDouble() == 2.0);
        CHECK(ins.effective.toDouble() == 9.0);
    }
    // 持久化只走 SettingsStore：值确实落在了约定键空间（config/<点号键>）。
    CHECK(settings.contains(
        settings.scopedKey(context::SettingsScope::Document, QStringLiteral("config/plot.lineWidth"))));

    // ===== 判据 2：reset 单键复位（其余键不受影响）=====
    svc.setUserValue(QStringLiteral("plot.accent"), QJsonValue(QStringLiteral("red")));
    svc.reset(QStringLiteral("plot.lineWidth"));
    {
        const auto ins = svc.inspect(QStringLiteral("plot.lineWidth"));
        CHECK(!ins.user.set && !ins.workspace.set && !ins.document.set);
        CHECK(ins.effective.toDouble() == 2.0); // 回落声明默认值
        const auto untouched = svc.inspect(QStringLiteral("plot.accent"));
        CHECK(untouched.user.set && untouched.user.value.toString() == QStringLiteral("red"));
        CHECK(untouched.effective.toString() == QStringLiteral("red"));
    }
    // set Undefined = 清该层覆盖（reset 的单层形式）。
    svc.setDocumentValue(QStringLiteral("plot.lineWidth"), QJsonValue(9.0));
    CHECK(svc.effectiveValue(QStringLiteral("plot.lineWidth")).toDouble() == 9.0);
    svc.setDocumentValue(QStringLiteral("plot.lineWidth"), QJsonValue(QJsonValue::Undefined));
    CHECK(svc.effectiveValue(QStringLiteral("plot.lineWidth")).toDouble() == 2.0);

    // ===== 判据 3：owner unmount 后属性消失、effectiveValue 回落 + 残留诊断 =====
    svc.setUserValue(QStringLiteral("plot.accent"), QJsonValue(QStringLiteral("red")));
    CHECK(toolbox.unmountContribution(QStringLiteral("plot")));
    // 声明面消失。
    CHECK(svc.configurations().isEmpty());
    {
        const auto ins = svc.inspect(QStringLiteral("plot.accent"));
        CHECK(!ins.declared);
        CHECK(ins.declaredOwner.isEmpty());
        CHECK(ins.effective.type() == QJsonValue::Undefined); // 回落（无默认值可回）
        // 用户数据不删：残留值仍在 SettingsStore。
        CHECK(settings.contains(settings.scopedKey(context::SettingsScope::App, QStringLiteral("config/plot.accent"))));
    }
    // 残留诊断（未声明但有残留值的键恰好一个：plot.accent；lineWidth 已 reset）。
    const auto diags = svc.diagnostics();
    CHECK(diags.size() == 1);
    CHECK(hasDiagFor(diags, QStringLiteral("plot.accent"), QStringLiteral("残留")));
    CHECK(hasDiagFor(diags, QStringLiteral("plot.accent"), QStringLiteral("不生效")));

    // remount 后声明恢复：默认值重新可回；user 层残留值（"red"）保留，
    // 声明恢复后 effective 取残留值（保留用户数据，不静默清掉）。
    CHECK(toolbox.mountContribution(doc(R"({
        "configuration": [{"title":"绘图","order":10,
            "properties":{"plot.accent":{"type":"string","default":"blue","scope":"resource","order":2}}}]
    })",
                                        QStringLiteral("plot"))));
    CHECK(svc.effectiveValue(QStringLiteral("plot.accent")).toString() == QStringLiteral("red"));
    CHECK(svc.diagnostics().isEmpty());

    // ===== 未声明键写入 → 诊断 =====
    svc.setUserValue(QStringLiteral("ghost.key"), QJsonValue(1.0));
    CHECK(svc.effectiveValue(QStringLiteral("ghost.key")).type() == QJsonValue::Undefined);
    CHECK(hasDiagFor(svc.diagnostics(), QStringLiteral("ghost.key"), QStringLiteral("残留")));
    svc.reset(QStringLiteral("ghost.key"));
    CHECK(svc.diagnostics().isEmpty());

    // ===== scope 决定参与合并的层（对位 VSCode ConfigurationScope target 语义）=====
    {
        // application/machine：只认 user 层——写 workspace/document 不改变 effective
        //（数据保留在存储里，但被声明 scope 排除在合并之外）。
        CHECK(toolbox.mountContribution(doc(R"({
            "configuration": [{"title":"S","order":1,
                "properties":{
                    "s.application":{"type":"number","default":1,"scope":"application"},
                    "s.machine":{"type":"number","default":2,"scope":"machine"},
                    "s.window":{"type":"number","default":3,"scope":"window"},
                    "s.resource":{"type":"number","default":4,"scope":"resource"}
                }}]
        })",
                                            QStringLiteral("s"))));
        // scope 缺省 window：user+workspace 参与，document 不参与。
        svc.setDocumentValue(QStringLiteral("s.window"), QJsonValue(99.0));
        CHECK(svc.effectiveValue(QStringLiteral("s.window")).toDouble() == 3.0);
        svc.setWorkspaceValue(QStringLiteral("s.window"), QJsonValue(7.0));
        CHECK(svc.effectiveValue(QStringLiteral("s.window")).toDouble() == 7.0);
        svc.reset(QStringLiteral("s.window"));
        // application：workspace 写入不改变 effective；user 写入生效。
        svc.setWorkspaceValue(QStringLiteral("s.application"), QJsonValue(50.0));
        CHECK(svc.effectiveValue(QStringLiteral("s.application")).toDouble() == 1.0);
        svc.setUserValue(QStringLiteral("s.application"), QJsonValue(11.0));
        CHECK(svc.effectiveValue(QStringLiteral("s.application")).toDouble() == 11.0);
        // inspect：数据在 workspace 层可见，但 effective 不取它。
        {
            const auto ins = svc.inspect(QStringLiteral("s.application"));
            CHECK(ins.workspace.set && ins.workspace.value.toDouble() == 50.0);
            CHECK(ins.user.set && ins.effective.toDouble() == 11.0);
            CHECK(ins.scope == QLatin1String("application"));
        }
        svc.reset(QStringLiteral("s.application"));
        // machine：与 application 同口径。
        svc.setDocumentValue(QStringLiteral("s.machine"), QJsonValue(60.0));
        CHECK(svc.effectiveValue(QStringLiteral("s.machine")).toDouble() == 2.0);
        svc.reset(QStringLiteral("s.machine"));
        // resource：三层全参与，document 最外。
        svc.setUserValue(QStringLiteral("s.resource"), QJsonValue(21.0));
        svc.setWorkspaceValue(QStringLiteral("s.resource"), QJsonValue(22.0));
        svc.setDocumentValue(QStringLiteral("s.resource"), QJsonValue(23.0));
        CHECK(svc.effectiveValue(QStringLiteral("s.resource")).toDouble() == 23.0);
        svc.setDocumentValue(QStringLiteral("s.resource"), QJsonValue(QJsonValue::Undefined));
        CHECK(svc.effectiveValue(QStringLiteral("s.resource")).toDouble() == 22.0);
        svc.reset(QStringLiteral("s.resource"));
        CHECK(toolbox.unmountContribution(QStringLiteral("s")));
    }

    // ===== null 是合法值（该层「已设置」），不再被 isNull 误判丢掉 =====
    {
        CHECK(toolbox.mountContribution(doc(R"({
            "configuration": [{"title":"N","order":2,
                "properties":{"n.key":{"type":"string","default":"d"}}}]
        })",
                                            QStringLiteral("n"))));
        svc.setUserValue(QStringLiteral("n.key"), QJsonValue(QJsonValue::Null));
        const auto ins = svc.inspect(QStringLiteral("n.key"));
        CHECK(ins.user.set);                             // null = 该层显式设置（重置语义），不是未设置
        CHECK(ins.effective.type() == QJsonValue::Null); // 覆盖默认值 "d"
        svc.reset(QStringLiteral("n.key"));
        CHECK(toolbox.unmountContribution(QStringLiteral("n")));
    }

    // ===== string + pattern：非法值不写入 + 诊断提示；合法值正常落层 =====
    {
        CHECK(toolbox.mountContribution(doc(R"({
            "configuration": [{"title":"P","order":3,
                "properties":{"p.id":{"type":"string","default":"","pattern":"^[a-z]+$"}}}]
        })",
                                            QStringLiteral("p"))));
        svc.setUserValue(QStringLiteral("p.id"), QJsonValue(QStringLiteral("BAD")));
        CHECK(svc.effectiveValue(QStringLiteral("p.id")).toString().isEmpty()); // 未写入
        CHECK(hasDiagFor(svc.diagnostics(), QStringLiteral("p.id"), QStringLiteral("pattern")));
        svc.setUserValue(QStringLiteral("p.id"), QJsonValue(QStringLiteral("ok")));
        CHECK(svc.effectiveValue(QStringLiteral("p.id")).toString() == QStringLiteral("ok"));
        // 写入合法值后违例诊断仍在（历史违例不静默消失）——reset 不清它。
        CHECK(hasDiagFor(svc.diagnostics(), QStringLiteral("p.id"), QStringLiteral("pattern")));
        svc.reset(QStringLiteral("p.id"));
        CHECK(toolbox.unmountContribution(QStringLiteral("p")));
    }

    if (g_failed == 0)
        std::printf("configuration_service: ALL PASS\n");
    else
        std::printf("configuration_service: %d FAILED\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
