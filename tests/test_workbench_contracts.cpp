#include "Subscription.h"
#include "IWorkspace.h"
#include "ICommands.h"
#include "IWindow.h"
#include "IIngest.h"
#include "NullWindow.h"
#include "CommandAdapter.h"
#include "Intent.h"

#include <QVariantMap>
#include <QObject>

#include <iostream>
#include <type_traits>

using namespace nextpilot::plotscope::workbench;
namespace ds = nextpilot::plotscope::dataset;

namespace
{
int failures = 0;
void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}
} // namespace

int main()
{
    // 所有接口头文件可被独立 include 并具名（编译期契约）。
    IWorkspace *ws = nullptr;
    ICommands *cmds = nullptr;
    IWindow *win = nullptr;
    ds::IIngest *ingest = nullptr;
    check(!ws && !cmds && !win && !ingest, "interface types compile");

    // IIngest 加载生命周期三事件（评论 52436915）：onDidStartLoad / onDidReportProgress /
    // onDidFinishLoad 都在接口上、返回 Subscription——确保 start→progress→finish 通道不退化。
    // 只做编译期签名具名；真实顺序由 ps_gui 端到端断言覆盖。
    using IngestStartFn = Subscription (ds::IIngest::*)(std::function<void()>);
    using IngestProgressFn = Subscription (ds::IIngest::*)(
        std::function<void(int, const QString &)>);
    using IngestFinishFn = Subscription (ds::IIngest::*)(std::function<void(bool)>);
    IngestStartFn startFn = &ds::IIngest::onDidStartLoad;
    IngestProgressFn progressFn = &ds::IIngest::onDidReportProgress;
    IngestFinishFn finishFn = &ds::IIngest::onDidFinishLoad;
    check(startFn && progressFn && finishFn, "IIngest lifecycle events present");

    // ICommands::Handler 签名 = Result<QVariant>(QVariantMap)，与仓库口径一致。
    ICommands::Handler h = [](const QVariantMap &args) -> Result<QVariant> {
        return QVariant(args.size());
    };
    Result<QVariant> r = h(QVariantMap{{QStringLiteral("a"), 1}, {QStringLiteral("b"), 2}});
    check(r.ok() && r.value().toInt() == 2, "Handler invoke");
    // Subscription = QMetaObject::Connection（§1.3.2 类型对齐，不用自建 Disposable）。
    static_assert(std::is_same_v<Subscription, QMetaObject::Connection>,
                  "Subscription must be QMetaObject::Connection");
    Subscription sub; // 默认构造 = 无效连接
    QObject::disconnect(sub); // 退订空句柄安全，不崩
    (void)sub;

    // NullWindow 全 no-op / 返回空。
    NullWindow nw;
    check(!nw.showOpenDialog({}, {}).has_value(), "NullWindow open = none");
    check(!nw.showSaveDialog({}, {}).has_value(), "NullWindow save = none");
    check(!nw.showQuickPick({}, {}).has_value(), "NullWindow quickpick = none");
    check(!nw.showInputBox({}).has_value(), "NullWindow inputbox = none");
    check(nw.activeSource() == -1, "NullWindow activeSource = -1");
    int progress = -1;
    nw.withProgress({}, [&](int p, const QString &) { progress = p; });
    check(progress == 100, "NullWindow progress completes");
    nw.showInformationMessage({});
    nw.showWarningMessage({});
    nw.showErrorMessage({});
    nw.showSource(1);
    nw.setActiveChannel(1, QStringLiteral("t"), QStringLiteral("c"));
    (void)nw.state(); // state() 返回真实引用，编译期可具名

    // asCommandHandler：IIntentHandler → ICommands::Handler 一次性适配（评论落差2）。
    // 验证 commandId→Intent.op、args→Intent.params、execute 结果透传。
    struct ProbeHandler : nextpilot::plotscope::intent::IIntentHandler
    {
        QString seenOp;
        QVariantMap seenParams;
        bool canHandle(const nextpilot::plotscope::intent::Intent &) const override { return true; }
        Result<nextpilot::plotscope::intent::IntentResult> execute(
            const nextpilot::plotscope::intent::Intent &in) override
        {
            seenOp = in.op;
            seenParams = in.params;
            nextpilot::plotscope::intent::IntentResult out;
            out.payload = QVariant(42);
            return out;
        }
    } probe;
    auto adapted = asCommandHandler(&probe, QStringLiteral("data.load"));
    QVariantMap args{{QStringLiteral("path"), QStringLiteral("a.csv")}};
    Result<QVariant> ar = adapted(args);
    check(ar.ok() && ar.value().toInt() == 42, "asCommandHandler forwards result");
    check(probe.seenOp == QStringLiteral("data.load"), "asCommandHandler maps id→Intent.op");
    check(probe.seenParams.value(QStringLiteral("path")).toString() == QStringLiteral("a.csv"),
          "asCommandHandler maps args→Intent.params");

    if (failures)
    {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "ps_workbench_contracts: all checks passed\n";
    return 0;
}
