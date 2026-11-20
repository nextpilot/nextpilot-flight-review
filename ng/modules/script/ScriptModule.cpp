#include "ScriptModule.h"

#include "BuiltinModuleRegistry.h"
#include "IIntentExecutor.h"
#include "Intent.h"
#include "JobRecorder.h"
#include "LuaEngine.h"
#include "ScriptFacade.h"
#include "TransformEvaluator.h"
#include "AppContext.h"
#include "RuntimeHost.h"

#include <QByteArray>
#include <QFile>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;
using namespace nextpilot::plotscope::host;

// script.run 处理器：接受 {source=...} 或 {path=...}，组装门面并执行脚本。
// 门面绑定宿主执行器 + 可选录制器，因此脚本调用与 GUI 点击走同一条执行路径。
class ScriptModule::Handler final : public QObject, public IIntentHandler
{
    Q_OBJECT
public:
    explicit Handler(IIntentExecutor &executor, JobRecorder *recorder,
                     ScriptFacade::ViewActionDispatcher viewDispatcher, QObject *parent)
        : QObject(parent), m_executor(executor), m_recorder(recorder),
          m_viewDispatcher(std::move(viewDispatcher)) {}

    bool canHandle(const Intent &intent) const override
    {
        return intent.op == QStringLiteral("script.run");
    }

    Result<IntentResult> execute(const Intent &intent) override
    {
        QByteArray source;
        if (intent.params.contains(QStringLiteral("source")))
        {
            source = intent.params.value(QStringLiteral("source")).toByteArray();
        }
        else if (intent.params.contains(QStringLiteral("path")))
        {
            const QString path = intent.params.value(QStringLiteral("path")).toString();
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly))
                return fail(QStringLiteral("eio"), QStringLiteral("无法读取脚本: %1").arg(path));
            source = f.readAll();
        }
        else
        {
            return fail(QStringLiteral("eargs"), QStringLiteral("script.run 需要 source 或 path"));
        }

        // 门面是栈对象，仅本次执行有效；脚本拿不到它的指针。
        // 注入 view dispatcher 后，脚本 app.call 可触达 GUI 视图能力。
        ScriptFacade facade(m_executor, m_recorder, m_viewDispatcher);
        nextpilot::plotscope::script::LuaEngine lua(facade);
        Result<void> r = lua.runString(source, QStringLiteral("=script.run"));
        if (!r)
            return fail(r.error().code, r.error().message);

        IntentResult out;
        out.payload = QStringLiteral("ok");
        return out;
    }

private:
    IIntentExecutor &m_executor;
    JobRecorder *m_recorder = nullptr;
    ScriptFacade::ViewActionDispatcher m_viewDispatcher;
};

ModuleInfo ScriptModule::moduleInfo()
{
    return ModuleInfo{
        QStringLiteral("mod.script"),
        QStringLiteral("脚本模块"),
        QStringLiteral("1.0"),
        ModuleKind::Builtin,
        {QStringLiteral("script.runner")},
        {} // 不强依赖具体模块：脚本能力通过门面按需发意图
    };
}

IModule *ScriptModule::create()
{
    return new ScriptModule();
}

ModuleInfo ScriptModule::info() const
{
    return moduleInfo();
}

void ScriptModule::initialize(ModuleContext &ctx)
{
    // view action 派发器：脚本 app.call 在意图通道 eunsupported 时回退到这里，
    // 从而能触发 plot.*/page.*/playback.* 等 GUI 视图能力（headless 下也可注册但通常无视图）。
    RuntimeHost *host = &ctx.host();
    ScriptFacade::ViewActionDispatcher viewDispatcher =
        [host](const QString &id, const QVariantMap &params, Result<QVariant> &out) {
            return host->dispatchViewAction(id, params, out);
        };
    m_handler = new Handler(ctx.executor(), ctx.recorder(), std::move(viewDispatcher), this);
    ctx.registerIntentHandler(m_handler);

    // 统一变换求值：注册进 AppContext，供 host(数据源衍生)/gui(曲线)/protocol(checksum)
    // 调用，而不必反向依赖 script 模块。builtin/expression 内部走 Rust，lua 走 C++。
    ctx.host().registerTransformEvaluator(
        [](const QJsonObject &request, uint32_t cap,
           const QVector<double> &t, const QVector<double> &v) {
            return nextpilot::plotscope::script::TransformEvaluator::evalSeries(request, cap, t, v);
        });

    // 有状态过滤器能力：内置目录注入 InteractionRegistry，供 host(data.filter) 与 GUI 编辑器调用。
    // 目录归模块持有（生命周期）；回调只吃/吐 JSON，调用方不依赖 script 模块类型。
    m_filterCatalogue = nextpilot::plotscope::script::makeBundledFilterCatalogue();
    auto *cat = m_filterCatalogue.get();
    nextpilot::plotscope::uischema::FilterOps filterOps;
    filterOps.list = [cat]() -> QJsonArray {
        QJsonArray arr;
        for (const auto &e : cat->entries())
            arr.append(nextpilot::plotscope::script::FilterCatalogue::descriptorToJson(e.cls));
        return arr;
    };
    filterOps.describe = [cat](const QString &id) -> Result<QJsonObject> {
        const auto *e = cat->find(id);
        if (!e)
            return fail(QStringLiteral("enoent"), QStringLiteral("未知过滤器: %1").arg(id));
        return Result<QJsonObject>(
            nextpilot::plotscope::script::FilterCatalogue::descriptorToJson(e->cls));
    };
    filterOps.applySeries = [cat](const QString &id, const QJsonObject &params,
                                  const QVector<double> &t, const QVector<double> &v) {
        return cat->applySeries(id, params, t, v);
    };
    ctx.host().registerFilterOps(std::move(filterOps));

    // Lua 脚本库：目录可用 NG_SCRIPTS_DIR 覆盖（部署/测试），默认 scripts/ops。
    const QByteArray dirOverride = qgetenv("NG_SCRIPTS_DIR");
    m_scriptLib.setOpsDir(dirOverride.isEmpty() ? QStringLiteral("scripts/ops")
                                                 : QString::fromLocal8Bit(dirOverride.constData()));
    m_scriptLib.loadAll();

    TransformScriptOps ops;
    ops.resolve = [this](const QString &scriptId, const QString &snapshot) {
        return m_scriptLib.resolveSource(scriptId, snapshot);
    };
    ops.save = [this](const QString &name, const QByteArray &source) {
        return m_scriptLib.save(name, source);
    };
    ops.describe = [this](const QByteArray &source, const QString &name) {
        return m_scriptLib.describe(source, name);
    };
    ctx.host().registerTransformScriptOps(ops);
}

#include "ScriptModule.moc"

// 编译期自注册：新增脚本模块无需改 App / 宿主。
NG_REGISTER_MODULE(ScriptModule)
